#!/usr/bin/env python3
"""Ring 3 UDP/TCP over real SLIRP and an independent fault-injecting TCP peer."""
import argparse
import http.server
import re
import socket
import struct
import threading
import time
from smoke import Guest
from network import Wire, SERVER, CLIENT, checksum, frames


def digest(data):
    value = 0x811c9dc5
    for byte in data:
        value = ((value ^ byte) * 0x01000193) & 0xffffffff
    return f'HTTP bytes/hash: {len(data):08x} {value:08x}'


BODY = (b'0123456789abcdefghijklmnopqrstuvwxyz\n' * 112) + b'Hello from the MiniOS network!\n'
RESPONSE = b'HTTP/1.0 200 OK\r\nContent-Length: ' + str(len(BODY)).encode() + b'\r\nConnection: close\r\n\r\n' + BODY


class HTTP(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    count = 0
    def log_message(self, *_):
        pass
    def do_GET(self):
        assert self.path == '/welcome.txt'
        HTTP.count += 1
        data = RESPONSE
        # Split writes to exercise stream reads independently of TCP boundaries.
        self.wfile.write(data[:7])
        self.wfile.flush()
        self.wfile.write(data[7:])


class Echo:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 9000))
        self.socket.settimeout(.2)
        self.stop = threading.Event()
        self.count = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()
    def run(self):
        while not self.stop.is_set():
            try:
                data, peer = self.socket.recvfrom(2048)
                self.count += 1
                self.socket.sendto(data, peer)
            except socket.timeout:
                pass
            except OSError:
                break
    def close(self):
        self.stop.set(); self.socket.close(); self.thread.join(timeout=2)


class TCPWire(Wire):
    def __init__(self, mode='faults'):
        self.connections = {}
        self.mode = mode
        self.syns = 0
        self.partial = False
        self.duplicate = False
        self.resets = 0
        self.data_retries = 0
        self.fin_retries = 0
        self.zero_probes = 0
        super().__init__()

    def tcp(self, port, flags, seq, ack, data=b'', window=2048, options=b'', corrupt=False):
        p = bytearray(struct.pack('!HHIIBBHHH', 8080, port, seq & 0xffffffff,
                                  ack & 0xffffffff, ((20 + len(options)) // 4) << 4,
                                  flags, window, 0, 0) + options + data)
        pseudo = SERVER + CLIENT + struct.pack('!BBH', 0, 6, len(p))
        struct.pack_into('!H', p, 16, checksum(pseudo + p) ^ int(corrupt))
        self.ip(6, p)

    def receive(self, p):
        if len(p) < 34 or p[12:14] != b'\x08\x00' or p[23] != 6:
            # Add a UDP echo service alongside the inherited DHCP/DNS/ARP peer.
            if len(p) >= 42 and p[23] == 17 and struct.unpack_from('!H', p, 36)[0] == 9000:
                self.client_mac = p[6:12]
                source, _, length, check = struct.unpack_from('!HHHH', p, 34)
                data = p[34:34 + length]
                assert check and checksum(p[26:34] + struct.pack('!BBH', 0, 17, length) + data) == 0
                self.udp(9001, source, b'wrong peer')
                self.udp(9000, source, data[8:])
                return
            super().receive(p)
            return
        self.client_mac = p[6:12]
        size = struct.unpack_from('!H', p, 16)[0]
        data = p[34:14 + size]
        port, destination, seq, ack, offset, flags, window, check, urgent = struct.unpack_from('!HHIIBBHHH', data)
        assert destination == 8080
        assert checksum(p[14:34]) == 0
        assert checksum(p[26:34] + struct.pack('!BBH', 0, 6, len(data)) + data) == 0
        payload = data[(offset >> 4) * 4:]
        if flags & 4:
            self.resets += 1
            return
        if flags & 2:
            self.syns += 1
            if self.mode == 'silent':
                return
            if self.mode == 'refuse':
                self.tcp(port, 0x14, 0, seq + 1)
                return
            if self.mode == 'faults' and self.syns == 1:
                return
            c = self.connections.setdefault(port, {'client': (seq + 1) & 0xffffffff,
                'server': 0xfffffff1, 'request': b'', 'responded': False,
                'sent_fin': False, 'dropped_data': False, 'fin_seq': None})
            # Advertise a small MSS, forcing the request into several segments.
            self.tcp(port, 0x12, 0xfffffff0, c['client'], window=0 if self.mode == 'zero' else 2048,
                     options=b'\x02\x04\x00\x20')
            return
        c = self.connections[port]
        if self.mode == 'reset':
            self.tcp(port, 0x14, c['server'], c['client'])
            return
        if self.mode == 'zero' and seq == (c['client'] - 1) & 0xffffffff and not payload:
            self.zero_probes += 1
            self.tcp(port, 0x10, c['server'], c['client'], window=2048)
            return
        if payload:
            if self.mode == 'faults' and not c['dropped_data']:
                c['dropped_data'] = True
                return
            if seq == c['client']:
                # First ACK is partial: client must retransmit only the suffix.
                count = min(7, len(payload)) if self.mode == 'faults' and not self.partial else len(payload)
                self.partial = True
                c['request'] += payload[:count]
                c['client'] = (c['client'] + count) & 0xffffffff
                if count != len(payload):
                    self.data_retries += 1
            else:
                self.data_retries += 1
            self.tcp(port, 0x10, c['server'], c['client'])
            if b'\r\n\r\n' in c['request'] and not c['responded']:
                assert c['request'].startswith(b'GET /welcome.txt HTTP/1.0\r\n')
                c['responded'] = True
                body = b'Hello from the MiniOS network!\n'
                response = b'HTTP/1.0 200 OK\r\nContent-Length: 31\r\n\r\n' + body
                if self.mode == 'faults':
                    self.tcp(port, 0x14, c['server'] + 100, c['client'])  # invalid reset sequence
                    self.tcp(port, 0x18, c['server'] + 40, c['client'], b'out of order')
                    self.tcp(port, 0x18, c['server'], c['client'], response, corrupt=True)
                    self.tcp(port, 0x18, c['server'], c['client'] + 1000, b'bad ack')
                self.tcp(port, 0x19, c['server'], c['client'], response)
                self.tcp(port, 0x19, c['server'], c['client'], response)  # duplicate DATA/FIN
                self.duplicate = True
                c['server'] = (c['server'] + len(response) + 1) & 0xffffffff
                c['sent_fin'] = True
        if flags & 1:
            if c['fin_seq'] is None:
                c['fin_seq'] = seq
                if self.mode == 'faults':
                    return  # force FIN retransmission
            else:
                self.fin_retries += 1
            self.tcp(port, 0x10, c['server'], seq + 1)


def run(g, path, marker, status=0, timeout=15):
    s = g.command('run /bin/' + path, '[exit]', timeout=timeout)
    assert marker in s and f'status={status}\n' in s, s
    return s


def drained(g):
    deadline = time.monotonic() + 8
    while True:
        s = g.command('sockets', 'Sockets active=')
        if 'Sockets active=0/8' in s:
            return s
        assert time.monotonic() < deadline, s
        time.sleep(.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-i386')
    a = parser.parse_args()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 8080), HTTP)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    echo = Echo()
    try:
        for ram in (2, 64):
            args = ['-netdev', 'user,id=n', '-device', 'rtl8139,netdev=n,id=nic']
            with Guest(a.qemu, ram, net_args=args) as g:
                g.command('dhcp', 'DHCP bound')
                before = frames(g)
                run(g, 'sockprobe', 'socket validation passed')
                drained(g)
                run(g, 'udpecho', 'UDP echo passed')
                s = run(g, 'http', 'HTTP download complete')
                assert digest(RESPONSE) in s and 'Hello from the MiniOS network!' in s, s
                drained(g)
                g.command('trim', 'Released heap bytes=')
                assert frames(g) == before
                s = g.command('spawn /bin/sockhold', 'Started PID')
                pid = re.search(r'Started PID (\d+)', s).group(1)
                time.sleep(.3)
                s = g.command('sockets', 'Sockets active=1/8')
                assert 'established' in s, s
                run(g, 'sockprobe', 'socket validation passed')  # another PID cannot access fd 0
                g.command('kill ' + pid, 'Killed PID')
                drained(g)
                assert frames(g) == before
                g.qmp('set_link', name='nic', up=False)
                run(g, 'http', 'HTTP failed', status=1)
                drained(g)
        assert HTTP.count >= 2 and echo.count == 2
    finally:
        echo.close(); server.shutdown(); server.server_close()
    print('PASS sockets: real SLIRP Ring 3 HTTP/UDP at 2/64 MiB, ownership/ABI checks, quotas, kill/exit cleanup, link-down and frame recovery', flush=True)
    for mode in ('faults', 'zero', 'refuse', 'silent', 'reset'):
        wire = TCPWire(mode)
        try:
            args = ['-netdev', f'socket,id=n,connect=127.0.0.1:{wire.port}', '-device', 'rtl8139,netdev=n']
            with Guest(a.qemu, 64, net_args=args) as g:
                g.command('dhcp', 'DHCP bound')
                if mode == 'faults':
                    run(g, 'udpecho', 'UDP echo passed')
                good = mode in ('faults', 'zero')
                s = run(g, 'http', 'HTTP download complete' if good else 'HTTP failed', status=0 if good else 1)
                if good:
                    response = b'HTTP/1.0 200 OK\r\nContent-Length: 31\r\n\r\nHello from the MiniOS network!\n'
                    assert digest(response) in s, s
                s = drained(g)
                if mode == 'faults':
                    assert int(re.search(r'retries=(\d+)', s).group(1)) >= 3, s
                    assert int(re.search(r'rejected=(\d+)', s).group(1)) >= 4, s
                    assert wire.partial and wire.duplicate and wire.fin_retries
                if mode == 'zero':
                    assert wire.zero_probes
            print('PASS TCP peer: ' + mode, flush=True)
        finally:
            wire.close()


if __name__ == '__main__':
    main()
