#!/usr/bin/env python3
"""Host-file/argv/exit/stream boundary checks for the native application CLIs."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
FIXTURES = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'src/html-rt/tools'))
from runtime_config import read_config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    suffix = '-sanitize' if args.sanitize else ''
    wasm = ROOT / ('build/cli-rt/wasm' + suffix)
    wat = ROOT / ('build/cli-rt/wat' + suffix)
    output_root = ROOT / 'build/system-tests/cli-runtime/entrypoints'
    output_root.mkdir(parents=True, exist_ok=True)
    results = []
    env = dict(os.environ, ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1'),
               UBSAN_OPTIONS='halt_on_error=1')

    def run(name, command, *, code=0, stdout=None, stderr=None, contains=None, input=b'', cwd=None):
        completed = subprocess.run([str(x) for x in command], input=input, capture_output=True,
                                   cwd=cwd or ROOT, env=env, timeout=20)
        assert completed.returncode == code, (name, completed.returncode, completed.stdout, completed.stderr)
        if stdout is not None:
            assert completed.stdout == stdout, (name, completed.stdout, completed.stderr)
        if stderr is not None:
            assert completed.stderr == stderr, (name, completed.stdout, completed.stderr)
        if contains is not None:
            assert contains in completed.stderr, (name, completed.stderr)
        assert b'Sanitizer' not in completed.stderr and b'runtime error:' not in completed.stderr, (name, completed.stderr)
        results.append({'name': name, 'exit': completed.returncode})
        return completed

    with tempfile.TemporaryDirectory(prefix='run-', dir=output_root) as temporary:
        work = Path(temporary)
        hello = work / 'hello.wasm'
        arguments = work / 'arguments.wasm'
        for source, target in [(FIXTURES / 'hello.wat', hello), (FIXTURES / 'arguments.wat', arguments)]:
            subprocess.run(['wasm-as', str(source), '-o', str(target)], check=True)
        expected = dict(code=7, stdout=b'ctor\nhello\n', stderr=b'error\n')
        run('outside Wasm, separate stdout/stderr', [wasm, hello], **expected)
        run('outside WAT parity', [wat, FIXTURES / 'hello.wat'], **expected)
        run('alternate Wasm entry/start once', [wasm, '--entry', 'probe', hello], stdout=b'ctor\nprobe\n', stderr=b'')
        run('alternate WAT entry/start once', [wat, '--entry', 'probe', FIXTURES / 'hello.wat'], stdout=b'ctor\nprobe\n', stderr=b'')
        run('spaced/options/empty guest argv WAT', [wat, FIXTURES / 'arguments.wat', 'two words', '--env', 'after', ''], stdout=b'', stderr=b'')
        run('spaced/options/empty guest argv Wasm', [wasm, arguments, 'two words', '--env', 'after', ''], stdout=b'', stderr=b'')
        bash = ROOT / 'src/vfs/usr/bin/bash'
        echo = ROOT / 'src/vfs/usr/bin/echo'
        run('extensionless installed utility', [wasm, echo, 'two words', '--entry'], stdout=b'two words --entry\n', stderr=b'')
        run('Bash -c output/status without prompt or JSON', [wasm, bash, '-c', 'printf "hello\\n"; exit 7'], code=7, stdout=b'hello\n', stderr=b'')
        run('default discovery outside cwd', [wasm, echo, 'outside cwd'], cwd=work, stdout=b'outside cwd\n', stderr=b'')
        link = work / 'wasm-link'
        link.symlink_to(wasm)
        run('default discovery through executable symlink', [link, echo, 'symlink'], cwd=work, stdout=b'symlink\n', stderr=b'')
        run('bounded environment override and safe guest PATH',
            [wasm, '--env', 'HOME=/chosen', '--env', 'EXTRA=with space', bash, '--norc', '-c',
             'printf "%s|%s|%s|%s|%s\\n" "$0" "$1" "$HOME" "$PATH" "$EXTRA"', 'spaced zero', 'two words'],
            stdout=b'spaced zero|two words|/chosen|/bin:/usr/bin|with space\n', stderr=b'')
        run('direct interactive SELECT/input and status', [wasm, bash, '--norc', '-i'], code=7,
            input=b'printf "__DIRECT_INPUT__\\n"\nexit 7\n')
        run('default Bash arguments without launcher', [wasm, bash], input=b'exit 0\n')
        staged = work / 'staged input'
        staged.write_bytes(b'staged contents\n')
        run('explicit file staging into Bash', [wasm, '--stage-file', '/tmp/staged-input', '644', staged,
            bash, '-c', '/usr/bin/cat /tmp/staged-input'], stdout=b'staged contents\n', stderr=b'')
        run('staged file does not persist', [wasm, bash, '-c', 'test ! -e /tmp/staged-input'], stdout=b'', stderr=b'')
        run('stage target must be absolute', [wasm, '--stage-file', 'relative', '644', staged, bash], code=2)
        run('stage mode is bounded', [wasm, '--stage-file', '/tmp/staged-input', '7777', staged, bash], code=2)
        run('missing staged input', [wasm, '--stage-file', '/tmp/staged-input', '644', work / 'absent', bash], code=126)
        copied_bash = work / 'supplied-bash'
        shutil.copyfile(bash, copied_bash)
        (work / 'secret').write_text('must stay outside guest')
        run('outside executable only, sibling never mounted', [wasm, copied_bash, '--norc', '-c',
            f'[ -e /tmp/waste-cli-input/secret ] && exit 3; [ -e "{work}/secret" ] && exit 4; echo isolated'],
            stdout=b'isolated\n', stderr=b'')
        run('memoryless export', [wat, FIXTURES.parent / 'memoryless.wat'], stdout=b'', stderr=b'')
        # Make an alternate, independently discovered tree with the production DSO.
        alternate = work / 'root'
        for directory in ['root', 'tmp', 'usr/bin', 'usr/lib']:
            (alternate / directory).mkdir(parents=True, exist_ok=True)
        shutil.copyfile(echo, alternate / 'usr/bin/echo')
        shutil.copyfile(ROOT / 'src/vfs/usr/lib/libc.so.wasm', alternate / 'usr/lib/libc.so.wasm')
        readable = alternate / 'usr/bin/readable'
        shutil.copyfile(hello, readable)
        readable.chmod(0o644)
        run('alternate root maps readable, non-executable input', [wasm, '--vfs-root', alternate, readable], **expected)
        run('alternate root explicit installed libc', [wasm, '--vfs-root', alternate, alternate / 'usr/bin/echo', 'alternate'], stdout=b'alternate\n', stderr=b'')
        shutil.copyfile(FIXTURES / 'hello.wat', alternate / 'usr/bin/readable.wat')
        run('mapped readable WAT', [wat, '--vfs-root', alternate, alternate / 'usr/bin/readable.wat'], **expected)
        neighbor = work / 'root-neighbor'
        neighbor.mkdir()
        shutil.copyfile(hello, neighbor / 'hello')
        run('root prefix collision stages supplied file', [wasm, '--vfs-root', alternate, neighbor / 'hello'], **expected)
        long_directory = alternate / 'root' / ('a' * 120) / ('b' * 120)
        long_directory.mkdir(parents=True)
        shutil.copyfile(hello, long_directory / 'long-name')
        run('bounded guest path', [wasm, '--vfs-root', alternate, long_directory / 'long-name'], code=126, contains=b'path exceeds')
        shutil.rmtree(alternate / 'root' / ('a' * 120))
        (alternate / 'usr/lib/libc.so.wasm').unlink()
        run('missing dependency', [wasm, '--vfs-root', alternate, alternate / 'usr/bin/echo'], code=126, contains=b'libc')
        reserved = alternate / 'tmp/waste-cli-input'
        reserved.mkdir()
        (reserved / 'sentinel').write_text('preserved')
        run('staging collision does not overwrite root', [wasm, '--vfs-root', alternate, hello], code=126, contains=b'namespace')
        assert (reserved / 'sentinel').read_text() == 'preserved'
        run('missing requested export', [wasm, '--entry', 'not_present', hello], code=126, contains=b'export')
        run('wasm rejects WAT', [wasm, FIXTURES / 'hello.wat'], code=126, contains=b'Wasm binary')
        run('wat rejects Wasm', [wat, hello], code=126, contains=b'WAT input')
        run('malformed WAT', [wat, FIXTURES.parent / 'malformed.wat'], code=126)
        bad = work / 'bad.wasm'
        bad.write_bytes(b'\0asm\x01\0\0\0\x01\xff')
        run('truncated Wasm', [wasm, bad], code=126)
        run('missing input', [wasm, work / 'not-there'], code=126, contains=b'input file')
        run('directory input', [wasm, work], code=126, contains=b'regular file')
        fifo = work / 'fifo'
        os.mkfifo(fifo)
        run('FIFO input never blocks', [wasm, fifo], code=126, contains=b'regular file')
        run('FIFO staged input never blocks', [wasm, '--stage-file', '/tmp/fifo', '644', fifo, bash], code=126)
        large = work / 'too-big'
        with large.open('wb') as stream:
            stream.truncate(read_config()['NATIVE_EXEC_BYTES_MAX'] + 1)
        run('bounded input bytes', [wasm, large], code=126, contains=b'byte limit')
        run('missing root override', [wasm, '--vfs-root', work / 'not-there', hello], code=126, contains=b'VFS root')
        run('root override must be directory', [wasm, '--vfs-root', hello, hello], code=126, contains=b'directory')
        dash = work / '-file.wat'
        shutil.copyfile(FIXTURES / 'hello.wat', dash)
        run('end of runtime options', [wat, '--', '-file.wat'], cwd=work, **expected)
        for name, command in [('missing argument', [wasm]), ('unknown option', [wasm, '--bad', 'x']),
                ('invalid environment', [wat, '--env', 'BAD', hello]), ('invalid timeout', [wasm, '--timeout-ms', '0', hello])]:
            run(name, command, code=2, contains=b'usage:')
        report = work / 'result.json'
        run('opt-in report', [wasm, '--result-file', report, hello], **expected)
        data = json.loads(report.read_text())
        assert data['exitStatus'] == 7 and data['total'] == 0 and data['execs'] == 0 and not data['error']
        report_before = report.read_bytes()
        run('report cannot truncate existing file', [wasm, '--result-file', report, hello], code=1, stdout=b'ctor\nhello\n', contains=b'cannot open')
        assert report.read_bytes() == report_before
        run('explicit execution deadline', [wasm, '--timeout-ms', '100', bash, '-c', 'while :; do :; done'], code=124)
    result = output_root / ('results-sanitize.json' if args.sanitize else 'results.json')
    result.write_text(json.dumps({'runtime': suffix or 'native', 'passed': len(results), 'checks': results}, indent=2) + '\n')
    print(f'PASS native application entrypoints: {len(results)} boundary checks ({suffix or "native"})')


if __name__ == '__main__':
    main()
