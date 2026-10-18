#!/usr/bin/env python3
"""Private inherited-stdio/PTY boundary checks; guest fixtures stay in WAT."""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = Path(__file__).with_name('stdio.wat')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--idle-seconds', type=float, default=0)
    args = parser.parse_args()
    suffix = '-sanitize' if args.sanitize else ''
    wat = str(ROOT / ('build/cli-rt/wat' + suffix))
    wasm = str(ROOT / ('build/cli-rt/wasm' + suffix))
    bash = str(ROOT / 'src/vfs/usr/bin/bash')
    output = ROOT / 'build/system-tests/cli-runtime/terminal'
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    checks = []

    def run(name, command, *, data=b'', code=0, stdout=b'', stderr=b'', **kwargs):
        p = subprocess.run(command, input=data, capture_output=True, timeout=30, env=env, **kwargs)
        assert (p.returncode, p.stdout, p.stderr) == (code, stdout, stderr), (name, p.returncode, p.stdout, p.stderr)
        checks.append(name)
        print('PASS ' + name, flush=True)
        return p

    class Terminal:
        def __init__(self, command, *, stdout_tty=True, readonly_stdin=False):
            self.master, self.slave = pty.openpty()
            fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 31, 97, 0, 0))
            self.original = termios.tcgetattr(self.slave)
            self.bytes = bytearray()
            source = os.open(os.ttyname(self.slave), os.O_RDONLY | os.O_NOCTTY) if readonly_stdin else self.slave
            try:
                self.process = subprocess.Popen(command, stdin=source,
                    stdout=self.slave if stdout_tty else subprocess.PIPE, stderr=self.slave, env=env)
            finally:
                if readonly_stdin:
                    os.close(source)

        def drain(self, timeout=0.05):
            if not select.select([self.master], [], [], timeout)[0]:
                return
            try:
                self.bytes.extend(os.read(self.master, 65536))
                assert len(self.bytes) < 1024 * 1024, ('excessive terminal output', bytes(self.bytes[-2000:]))
            except OSError as error:
                if error.errno != errno.EIO:
                    raise

        def until(self, needle, timeout=20):
            end = time.monotonic() + timeout
            while needle not in self.bytes and time.monotonic() < end:
                self.drain()
                if self.process.poll() is not None:
                    self.drain()
                    break
            assert needle in self.bytes, (needle, bytes(self.bytes), self.process.poll())

        def send(self, data):
            os.write(self.master, data)

        def input_wait(self, after=0, since=0, timeout=20, pid=None):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                waits = re.findall(rb'\{"wait":(\d+),"pid":(\d+),"kind":[12]\}', self.bytes[since:])
                waits = [int(count) for count, process in waits if pid is None or int(process) == pid]
                if waits and waits[-1] > after:
                    return waits[-1]
                self.drain()
                assert self.process.poll() is None, bytes(self.bytes)
            raise AssertionError(('Bash did not return to input wait', bytes(self.bytes[-4000:])))

        def finish(self, name, code=0):
            self.process.wait(timeout=20)
            for _ in range(3):
                self.drain()
            assert self.process.returncode == code, (name, self.process.returncode, bytes(self.bytes))
            assert termios.tcgetattr(self.slave) == self.original, (name, 'terminal not restored')
            assert b'AddressSanitizer' not in self.bytes and b'runtime error:' not in self.bytes, bytes(self.bytes)
            checks.append(name)
            print('PASS ' + name, flush=True)

        def close(self):
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(timeout=5)
            termios.tcsetattr(self.slave, termios.TCSANOW, self.original)
            os.close(self.master)
            os.close(self.slave)
            if self.process.stdout:
                self.process.stdout.close()

    def terminal(name, command, action, *, code=0, stdout_tty=True, readonly_stdin=False):
        t = Terminal(command, stdout_tty=stdout_tty, readonly_stdin=readonly_stdin)
        try:
            action(t)
            t.finish(name, code)
            return bytes(t.bytes)
        finally:
            t.close()

    def probe(entry):
        return [wat, '--entry', entry, str(FIXTURE)]

    run('pipe stdio identity', probe('identity'), stdout=b'000\n')
    run('pipe fstat type', probe('fifo'))
    run('stdout alias survives closing fd 1', probe('alias'), stdout=b'alias\n')
    run('stderr alias keeps original destination', probe('stderr-alias'), stderr=b'alias\n')
    run('stdin alias and EOF after closing fd 0', probe('read-alias'), data=b'x')
    payload = bytes(range(256)) * 200
    run('bounded input queue preserves all binary bytes', probe('copy'), data=payload, stdout=payload)
    run('bare Bash consumes piped commands without prompt', [wasm, bash], data=b'printf "pipe\\n"\n', stdout=b'pipe\n')
    run('guest stdout redirected to stderr', [wasm, bash, '-c', 'echo error >&2'], stderr=b'error\n')
    run('Bash explicit stderr alias', [wasm, bash, '-c', 'exec 2>&1; echo merged >&2'], stdout=b'merged\n')
    run('guest file redirection and subsequent host input', [wasm, bash, '-c',
        'echo guest > /tmp/native-input; read -r line < /tmp/native-input; echo "$line"; read -r line; echo "$line"'],
        data=b'host\n', stdout=b'guest\nhost\n')
    run('guest pipeline with inherited streams', [wasm, bash, '-c', 'printf "ab\\n" | /usr/bin/wc -c'], stdout=b'3\n')
    with tempfile.TemporaryDirectory(prefix='run-', dir=output) as temporary:
        work = Path(temporary)
        regular = work / 'stdin'
        regular.write_bytes(payload)
        with regular.open('rb') as source:
            p = subprocess.run(probe('copy'), stdin=source, capture_output=True, timeout=30, env=env)
        assert p.returncode == 0 and p.stdout == payload and not p.stderr, p
        checks.append('inherited regular-file input')
        result = work / 'stdout'
        with result.open('wb') as sink:
            p = subprocess.run(probe('alias'), stdout=sink, stderr=subprocess.PIPE, timeout=30, env=env)
        assert p.returncode == 0 and result.read_bytes() == b'alias\n' and not p.stderr, p
        checks.append('inherited regular-file output through alias')
    terminal('TTY stdio identity and restoration', probe('identity'), lambda t: t.until(b'111\r\n'))
    # Validate the independently inherited stdout identity with a pipe capture.
    t = Terminal(probe('identity'), stdout_tty=False)
    try:
        captured = t.process.stdout.read()
        assert captured == b'101\n', captured
        t.finish('mixed stdout remains a pipe')
    finally:
        t.close()
    terminal('TTY alias retains output capability', probe('alias'), lambda t: t.until(b'alias\r\n'))
    terminal('TTY access modes remain distinct', probe('readonly'), lambda t: None, readonly_stdin=True)
    terminal('missing entry restores TTY', [wat, '--entry', 'missing', str(FIXTURE)], lambda t: None, code=126)
    terminal('timeout restores TTY', [wat, '--timeout-ms', '20', '--entry', 'spin', str(FIXTURE)], lambda t: None, code=124)
    for signum, entry in [(signal.SIGINT, 'wait'), (signal.SIGTERM, 'spin'), (signal.SIGHUP, 'wait'), (signal.SIGQUIT, 'wait')]:
        def interrupt(t, signum=signum, entry=entry):
            if entry == 'wait':
                t.until(b'alias\r\n')
            else:
                end = time.monotonic() + 10
                while termios.tcgetattr(t.slave) == t.original and time.monotonic() < end:
                    time.sleep(0.01)
                assert termios.tcgetattr(t.slave) != t.original
            os.kill(t.process.pid, signum)
        terminal(f'{signal.Signals(signum).name} restores TTY', probe(entry), interrupt, code=128 + signum)

    terminal('initial host TTY ioctl dimensions', probe('size'), lambda t: t.until(struct.pack('HH', 31, 97)))
    def resize(t):
        t.until(b'alias\r\n')
        fcntl.ioctl(t.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 41, 121, 0, 0))
        os.kill(t.process.pid, signal.SIGWINCH)
        t.send(b'x\n')
        t.until(struct.pack('HH', 41, 121))
    terminal('SIGWINCH updates kernel ioctl dimensions', probe('resized'), resize)

    def canonical(t):
        t.input_wait()
        t.send(b'abX\x7fc\n')
        t.until(b'abX\b \bc\r\nabc\r\n')
        checks.append('canonical erase, echo and line delivery')
        t.send(b'bad\x15ok\n')
        t.until(b'bad\b \b\b \b\b \bok\r\nok\r\n')
        checks.append('canonical kill and echo')
        t.send(b'\x04')
    terminal('canonical EOF and TTY restoration',
             [wat, '--trace-waits', '--entry', 'copy', str(FIXTURE)], canonical)

    def shell(t):
        t.until(b'# ')
        mode = termios.tcgetattr(t.slave)
        assert not mode[3] & (termios.ECHO | termios.ICANON | termios.ISIG), mode
        checks.append('host TTY transfers line editing to guest')
        t.send(b'printf "EDIT_%s\\n" xX\x7fy\n')
        t.until(b'EDIT_xy\r\n')
        checks.append('interactive Bash line editing without duplicate echo')
        # The typed command contains this token once; the output contains it once.
        assert bytes(t.bytes).count(b'EDIT_xy\r\n') == 1, bytes(t.bytes)
        t.send(b'printf "SIZE_%s_%s\\n" "$LINES" "$COLUMNS"\n')
        t.until(b'SIZE_31_97\r\n')
        checks.append('initial host TTY dimensions')
        # Synchronize with the actual input wait: output alone may precede it.
        position = t.bytes.index(b'SIZE_31_97\r\n') + len(b'SIZE_31_97\r\n')
        waiting = t.input_wait(since=position)
        fcntl.ioctl(t.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 41, 121, 0, 0))
        os.kill(t.process.pid, signal.SIGWINCH)
        t.input_wait(waiting)
        t.send(b'printf "RESIZE_%s_%s\\n" "$LINES" "$COLUMNS"\n')
        t.until(b'RESIZE_41_121\r\n')
        checks.append('Bash handles SIGWINCH during input wait and updates dimensions')
        if args.idle_seconds:
            time.sleep(args.idle_seconds)
            assert t.process.poll() is None, bytes(t.bytes)
            t.send(b'printf "__AFTER_IDLE__\\n"\n')
            t.until(b'__AFTER_IDLE__\r\n')
            checks.append(f'idle shell remains usable after {args.idle_seconds:g} seconds')
        t.send(b'\x04')
    terminal('interactive Bash EOF and TTY restoration', [wasm, '--trace-waits', bash, '--norc', '-i'], shell)
    def interruptions(t):
        t.until(b'# ')
        waiting = t.input_wait()
        t.send(b'echo SHOULD_NOT_RUN\x03')
        t.input_wait(waiting)
        t.send(b'printf "IDLE_STATUS_%s\\n" "$?"\n')
        t.until(b'IDLE_STATUS_130\r\n')
        checks.append('Ctrl-C cancels input and preserves the shell')
        position = len(t.bytes)
        t.send(b'/usr/bin/cat\n')
        waiting = t.input_wait(since=position, pid=2)
        t.send(b'\x03')
        t.input_wait(waiting, pid=1)
        t.send(b'printf "CHILD_STATUS_%s\\n" "$?"\n')
        t.until(b'CHILD_STATUS_130\r\n')
        checks.append('Ctrl-C terminates foreground cat and preserves the shell')
        t.send(b'/usr/bin/bash -c \'echo CPU_READY; while :; do :; done\'\n')
        t.until(b'CPU_READY\r\n')
        position = len(t.bytes)
        t.send(b'\x03')
        t.input_wait(since=position, pid=1)
        t.send(b'printf "CPU_STATUS_%s\\n" "$?"\n')
        t.until(b'CPU_STATUS_130\r\n')
        checks.append('Ctrl-C interrupts CPU-bound child at an evaluator checkpoint')
        t.send(b'trap \'echo CAUGHT_INT\' INT; echo TRAP_READY\n')
        t.until(b'TRAP_READY\r\n')
        position = t.bytes.index(b'TRAP_READY\r\n') + len(b'TRAP_READY\r\n')
        waiting = t.input_wait(since=position, pid=1)
        os.kill(t.process.pid, signal.SIGINT)
        t.input_wait(waiting, pid=1)
        t.until(b'CAUGHT_INT\r\n')
        checks.append('host SIGINT reaches the caught guest handler')
        t.send(b'exit 0\n')
    terminal('Bash interruption and TTY restoration',
             [wasm, '--trace-waits', bash, '--norc', '-i'], interruptions)
    destination = output / ('results' + suffix + '.json')
    destination.write_text(json.dumps({'passed': len(checks), 'checks': checks}, indent=2) + '\n')
    print(f'PASS inherited stdio/TTY: {len(checks)} host boundaries ({suffix or "native"})')


if __name__ == '__main__':
    main()
