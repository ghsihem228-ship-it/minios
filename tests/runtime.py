#!/usr/bin/env python3
"""Actual freestanding C startup/library, argument ownership and variable HTTP."""
import argparse
import http.server
from pathlib import Path
import re
import tempfile
import threading
from smoke import Guest, ROOT
from storage import FatImage, create
from sockets import digest, drained
from network import frames

BODY = b'Configurable C HTTP works!\n'
RESPONSE = b'HTTP/1.0 200 OK\r\nContent-Length: ' + str(len(BODY)).encode() + b'\r\n\r\n' + BODY


class Handler(http.server.BaseHTTPRequestHandler):
    count = 0
    def log_message(self, *_):
        pass
    def do_GET(self):
        assert self.path == '/other.txt?x=1', self.path
        assert self.headers['Host'] == f'10.0.2.2:{self.server.server_port}'
        Handler.count += 1
        self.wfile.write(RESPONSE)


def run(g, command, marker, status=0):
    s = g.command(command, '[exit]')
    assert marker in s and f'status={status}\n' in s, s
    return s


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu', default='qemu-system-i386')
    a = p.parse_args()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix='minios-runtime-') as folder:
            for ram in (2, 64):
                disk = Path(folder) / f'c{ram}.img'
                create(disk)
                args = ['-netdev', 'user,id=n', '-device', 'rtl8139,netdev=n']
                with Guest(a.qemu, ram, disk=disk, net_args=args) as g:
                    before = frames(g)
                    run(g, 'run /bin/ctest', 'C runtime passed argc=1')
                    run(g, 'run /bin/ctest extra ""', 'C runtime passed argc=3')
                    s = run(g, 'run /bin/args "hello world" "" \'single space\' a\\ b', 'argc=5')
                    for expected in ('argv[0]=</bin/args>', 'argv[1]=<hello world>',
                                     'argv[2]=<>', 'argv[3]=<single space>', 'argv[4]=<a b>'):
                        assert expected in s, s
                    run(g, 'run /bin/args a b c d e f g', 'argc=8')
                    g.command('run /bin/args a b c d e f g h', 'Too many arguments')
                    run(g, 'run /bin/args ' + 'x' * 63, 'argv[1]=<' + 'x' * 63 + '>')
                    g.command('run /bin/args ' + 'x' * 64, 'Error errno=7')
                    g.command('run /bin/args "unterminated', 'Invalid quoting')
                    g.command('run /bin/args trailing\\', 'Invalid quoting')
                    g.command('write /tmp/quoted "hello world"', 'Wrote bytes=11')
                    g.command('cat /tmp/quoted', 'hello world')
                    g.command('rm /tmp/quoted', 'Removed file')
                    g.command('spawn /bin/args --delay 200 retained', 'Started PID')
                    g.command('write /tmp/change overwritten', 'Wrote bytes=11')
                    g.wait_for('argv[3]=<retained>')
                    g.command('rm /tmp/change', 'Removed file')
                    g.command('cp /bin/args /disk/args.elf', 'Copied bytes=')
                    run(g, 'run /disk/args.elf disk argument', 'argv[2]=<argument>')
                    run(g, 'run /bin/http --help', 'Usage:')
                    for tail in ('bad 8080 /x', '10.0.2.2 0 /x', '10.0.2.2 65536 /x',
                                 '10.0.2.2 -1 /x', '10.0.2.2 8080 relative'):
                        run(g, 'run /bin/http ' + tail, 'Usage:', status=2)
                    g.command('dhcp', 'DHCP bound')
                    s = run(g, f'run /bin/http 10.0.2.2 {server.server_port} /other.txt?x=1',
                            'HTTP download complete')
                    assert digest(RESPONSE) in s and BODY.decode().strip() in s, s
                    drained(g)
                    g.command('trim', 'Released heap bytes=')
                    assert frames(g) == before
                assert FatImage(disk).read('ARGS.ELF') == (ROOT / 'build/user/args.elf').read_bytes()
                with Guest(a.qemu, ram, disk=disk) as g:
                    run(g, 'run /disk/args.elf after reboot', 'argv[2]=<reboot>')
                print(f'PASS C {ram} MiB: startup/alignment/argv ownership, quotes/limits, formatter/strings, file/socket ABI, variable HTTP and disk ELF reboot', flush=True)
        assert Handler.count == 2
    finally:
        server.shutdown(); server.server_close()


if __name__ == '__main__':
    main()
