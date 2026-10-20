#!/usr/bin/env python3
"""Actual RTL8139 network tests; only local QEMU and temporary disk images."""
import argparse
from pathlib import Path
import re
import socket
import struct
import tempfile
import threading
import time
from smoke import Guest, ROOT
from storage import FatImage, create

SERVER = bytes([10, 0, 2, 2])
CLIENT = bytes([10, 0, 2, 15])
MAC = bytes.fromhex('525400aabbcc')


def checksum(data):
    if len(data) & 1:
        data += b'\0'
    total = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


class Wire:
    """Independent Ethernet peer using QEMU's framed socket network backend."""
    def __init__(self):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.error = None
        self.conn = None
        self.files = {'large.bin': bytes((i * 17 + 31) % 256 for i in range(66000)),
                      'exact.bin': bytes(range(256)) * 2, 'empty.txt': b'',
                      'oversize.bin': b'x' * (256 * 1024 + 1)}
        self.downloads = {}
        self.dropped_ack = False
        self.dropped_request = False
        self.dns_queries = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def close(self):
        self.stop.set()
        if self.conn:
            self.conn.close()
        self.listener.close()
        self.thread.join(timeout=3)
        if self.error:
            raise self.error

    def send(self, frame):
        self.conn.sendall(struct.pack('!I', len(frame)) + frame)

    def ip(self, proto, data, target=CLIENT, flags=0, corrupt=False):
        header = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(data),
                                      1, flags, 64, proto, 0, SERVER, target))
        struct.pack_into('!H', header, 10, checksum(header) ^ int(corrupt))
        self.send(self.client_mac + MAC + b'\x08\x00' + header + data)

    def udp(self, source, target, payload, broadcast=False):
        dest = b'\xff' * 4 if broadcast else CLIENT
        udp = bytearray(struct.pack('!HHHH', source, target, len(payload) + 8, 0) + payload)
        pseudo = SERVER + dest + struct.pack('!BBH', 0, 17, len(udp))
        struct.pack_into('!H', udp, 6, checksum(pseudo + udp) or 65535)
        self.ip(17, udp, dest)

    def data(self, port, block):
        content = self.downloads[port]
        payload = struct.pack('!HH', 3, block) + content[(block - 1) * 512:block * 512]
        self.udp(40000, port, payload)

    def receive(self, p):
        if len(p) < 14:
            return
        self.client_mac = p[6:12]
        if p[12:14] == b'\x08\x06':
            if len(p) >= 42 and p[20:22] == b'\0\1':
                a = struct.pack('!HHBBH', 1, 0x800, 6, 4, 2)
                a += MAC + p[38:42] + self.client_mac + p[28:32]
                self.send(self.client_mac + MAC + b'\x08\x06' + a)
            return
        if p[12:14] != b'\x08\x00':
            return
        start = 14 + (p[14] & 15) * 4
        total = struct.unpack_from('!H', p, 16)[0]
        data = p[start:14 + total]
        assert checksum(p[14:start]) == 0
        if p[23] == 1:
            assert checksum(data) == 0
            response = bytearray(data)
            response[0] = 0
            response[2:4] = b'\0\0'
            struct.pack_into('!H', response, 2, checksum(response))
            self.ip(1, response, corrupt=True)  # bad IP checksum must be ignored
            self.ip(1, response, flags=0x2000)  # no fragment reassembly
            self.ip(1, response)
            return
        if p[23] != 17 or len(data) < 8:
            return
        source, target, length, check = struct.unpack_from('!HHHH', data)
        assert length == len(data)
        assert check and checksum(p[26:34] + struct.pack('!BBH', 0, 17, length) + data) == 0
        data = data[8:]
        if target == 67:
            response = bytearray(data[:240])
            response[0] = 2
            response[16:20] = CLIENT
            kind = 2 if data[242] == 1 else 5
            options = bytes([53, 1, kind, 54, 4]) + SERVER
            options += bytes([1, 4, 255, 255, 255, 0, 3, 4]) + SERVER
            options += bytes([6, 4]) + SERVER + bytes([51, 4, 0, 0, 14, 16, 255])
            self.udp(67, 68, response + options[:-2], True)  # truncated TLV
            self.udp(67, 68, response + options, True)
        elif target == 53:
            self.dns_queries += 1
            if b'silent' in data:
                return
            response = bytearray(data)
            response[2:4] = b'\x81\x80'
            if b'missing' in data:
                response[3] = 0x83
                self.udp(53, source, response)
                return
            response[6:8] = b'\0\2'
            # CNAME followed by compressed A owner. First inject a pointer cycle.
            alias = b'\x06target\x04test\0'
            response += b'\xc0\x0c' + struct.pack('!HHIH', 5, 1, 60, len(alias))
            offset = len(response)
            response += alias
            response += struct.pack('!H', 0xc000 | offset) + struct.pack('!HHIH', 1, 1, 60, 4) + SERVER
            malformed = bytearray(response)
            malformed[12:14] = b'\xc0\x0c'
            self.udp(53, source, malformed)
            self.udp(53, source, response)
        elif target == 69:
            assert data[:2] == b'\0\1'
            name = data[2:].split(b'\0')[0].decode()
            if name == 'timeout':
                return
            if name not in self.files:
                self.udp(40000, source, b'\0\5\0\1missing\0')
                return
            if name == 'large.bin' and not self.dropped_request:
                self.dropped_request = True
                return
            self.downloads[source] = self.files[name]
            self.data(source, 1)
        elif target == 40000:
            assert data[:2] == b'\0\4'
            block = struct.unpack_from('!H', data, 2)[0]
            content = self.downloads[source]
            if len(content) < block * 512:
                return
            if block == 2 and not self.dropped_ack:
                self.dropped_ack = True
                self.data(source, block)  # lost ACK: duplicate block must be re-ACKed
                return
            self.udp(40001, source, struct.pack('!HH', 3, block + 1) + b'wrong TID')
            self.data(source, block + 1)

    def run(self):
        try:
            self.listener.settimeout(1)
            while not self.stop.is_set():
                try:
                    self.conn, _ = self.listener.accept()
                    break
                except socket.timeout:
                    pass
            if self.conn is None:
                return
            self.conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.conn.settimeout(.2)
            buffer = b''
            while not self.stop.is_set():
                try:
                    p = self.conn.recv(65536)
                except socket.timeout:
                    continue
                if not p:
                    break
                buffer += p
                while len(buffer) >= 4:
                    size = struct.unpack_from('!I', buffer)[0]
                    assert size <= 65536
                    if len(buffer) < size + 4:
                        break
                    self.receive(buffer[4:4 + size])
                    buffer = buffer[4 + size:]
        except (OSError, AssertionError) as e:
            if not self.stop.is_set():
                self.error = e


def frames(g):
    s = g.command('mem', 'Paging: ON')
    assert 'integrity=OK' in s
    return int(re.search(r'PMM: total=\d+ free=(\d+)', s).group(1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-i386')
    a = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='minios-net-') as folder:
        p = Path(folder)
        disk = p / 'disk.img'
        create(disk)
        (p / 'welcome.txt').write_bytes(b'Hello from the MiniOS network!\n')
        elf = (ROOT / 'build/user/hello.elf').read_bytes()
        (p / 'hello.elf').write_bytes(elf)
        args = ['-netdev', f'user,id=n,tftp={p}', '-device', 'rtl8139,netdev=n,id=nic']
        for ram in (2, 64):
            with Guest(a.qemu, ram, disk=disk, net_args=args) as g:
                g.command('pci', '0x10EC8139')
                g.command('dhcp', 'DHCP bound 10.0.2.15')
                g.command('ping 10.0.2.2', 'Ping received=3/3')
                before = frames(g)
                g.command(f'fetch 10.0.2.2 hello.elf /disk/net{ram}.elf', 'Fetched bytes=')
                s = g.command(f'run /disk/net{ram}.elf', '[exit]')
                assert 'Hello from Ring 3!' in s and 'status=0' in s, s
                g.command('fetch 10.0.2.2 welcome.txt /message', 'Fetched bytes=31')
                g.command('cat /message', 'Hello from the MiniOS network!')
                g.command('fetch 10.0.2.2 hello.elf /message', 'Error errno=17')
                assert frames(g) == before
                g.qmp('set_link', name='nic', up=False)
                g.command('dhcp', 'Error errno=100')
            assert FatImage(disk).read(f'NET{ram}.ELF') == elf
        print('PASS network: real SLIRP DHCP/ARP/ICMP/TFTP, Ring 3 disk ELF, 2/64 MiB, link-down', flush=True)
        wire = Wire()
        try:
            args = ['-netdev', f'socket,id=n,connect=127.0.0.1:{wire.port}',
                    '-device', 'rtl8139,netdev=n']
            with Guest(a.qemu, 64, disk=disk, net_args=args) as g:
                g.command('dhcp', 'DHCP bound 10.0.2.15')
                g.command('ping 10.0.2.2', 'Ping received=3/3')
                g.command('resolve alias.test', 'DNS alias.test = 10.0.2.2')
                g.command('resolve alias.test', 'DNS cached alias.test')
                assert wire.dns_queries == 1
                g.command('resolve missing.test', 'Error errno=2')
                g.command('resolve silent.test', 'Error errno=110')
                before = frames(g)
                s = g.command('spawn /bin/spin', 'Started PID')
                pid = re.search(r'Started PID (\d+)', s).group(1)
                for remote, local in [('large.bin', 'large.bin'), ('exact.bin', 'exact.bin'), ('empty.txt', 'empty.txt')]:
                    g.command(f'fetch 10.0.2.2 {remote} /disk/{local}',
                              f'Fetched bytes={len(wire.files[remote])}')
                s = g.command('ps', 'PID STATE')
                assert re.search(r'^' + pid + r' (ready|running) ', s, re.MULTILINE), s
                g.command('kill ' + pid, 'Killed PID')
                g.command('trim', 'Released heap bytes=')
                g.command('fetch 10.0.2.2 oversize.bin /disk/big.bin', 'Error errno=27', timeout=35)
                g.command('stat /disk/big.bin', 'Error errno=2')
                g.command('fetch 10.0.2.2 large.bin /short', 'Error errno=28')
                g.command('stat /short', 'Error errno=2')
                g.command('fetch 10.0.2.2 absent /disk/absent', 'Error errno=2')
                g.command('fetch 10.0.2.2 timeout /disk/timeout', 'Error errno=110')
                g.command('stat /disk/timeout', 'Error errno=2')
                g.type('clear\nfetch 10.0.2.2 timeout /disk/cancel\n')
                time.sleep(.3)
                g.type('\x1b')
                g.wait_for('Error errno=4')
                g.command('stat /disk/cancel', 'Error errno=2')
                assert frames(g) == before
                s = g.command('net', 'driver-errors=')
                assert int(re.search(r'dropped=(\d+)', s).group(1)) >= 10, s
                assert 'driver-errors=0' in s, s
                g.command('fetch 10.0.2.2 large.bin /disk/LARGE.BIN', 'Error errno=17')
            image = FatImage(disk)
            for name in ('large.bin', 'exact.bin', 'empty.txt'):
                assert image.read(name.upper()) == wire.files[name], name
            image.check()
            assert wire.dropped_request and wire.dropped_ack
        finally:
            wire.close()
        with Guest(a.qemu, 64, disk=disk) as g:
            g.command('dhcp', 'Error errno=100')
            g.command('stat /disk/large.bin', 'Size bytes=66000')
            g.command('run /disk/net64.elf', 'Hello from Ring 3!')
        print('PASS network: DNS CNAME/cache/NXDOMAIN, malformed packets, retries, RX wrap, wrong TID, size limits, cancel, background scheduling, heap recovery and reboot persistence', flush=True)


if __name__ == '__main__':
    main()
