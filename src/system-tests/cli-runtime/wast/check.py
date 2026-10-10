#!/usr/bin/env python3
"""Private host CLI/report/isolation checks; guest assertions live in WAST."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
FIXTURES = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    suffix = '-sanitize' if args.sanitize else ''
    runner = ROOT / ('build/cli-rt/wast' + suffix)
    output = ROOT / 'build/system-tests/cli-runtime/wast'
    output.mkdir(parents=True, exist_ok=True)
    checks = []
    env = dict(os.environ, ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1'),
               UBSAN_OPTIONS='halt_on_error=1')

    def run(name, flags, *, code=0, contains=None, data=b'', cwd=None, timeout=30):
        p = subprocess.run([str(runner), *map(str, flags)], cwd=cwd or ROOT,
                           input=data, capture_output=True, timeout=timeout, env=env)
        assert p.returncode == code, (name, p.returncode, p.stdout, p.stderr)
        if contains is not None:
            assert contains in p.stdout + p.stderr, (name, p.stdout, p.stderr)
        checks.append(name)
        return p

    with tempfile.TemporaryDirectory(prefix='run-', dir=output) as temporary:
        work = Path(temporary)
        empty = work / 'empty.wast'
        empty.write_text(';; No assertions is a valid script.\n')
        fail = work / 'value.fail.wast'
        fail.write_text('(module (func (export "x") (result i32) i32.const 1))\n'
                        '(assert_return (invoke "x") (i32.const 2))\n')
        setup = work / 'setup.wast'
        setup.write_text('(module (import "unknown" "f" (func)))\n')
        truncated = work / 'truncated.wast'
        truncated.write_text('(module (func (export "x") (result i32) i32.const 1))\n'
                             '(assert_return (invoke "x") (i32.const 1))\n(module')
        recovery = work / 'recovery.wast'
        recovery.write_text('(module (func (export "x") (result i32) i32.const 1))\n'
                            '(assert_return (invoke "x") (i32.const 2))\n'
                            '(assert_return (invoke "x") (i32.const 1))\n')
        language = FIXTURES / 'language.wast'
        runtime = FIXTURES / 'runtime.wast'
        providers = FIXTURES / 'providers.wast'
        unlinkable = FIXTURES / 'unlinkable.wast'
        run('full language commands', ['--verbose', language], contains=b'5 PASS, 0 FAIL')
        p = run('assertion JSON schema', ['--json', language])
        result = json.loads(p.stdout)
        assert result['passed'] == result['total'] == 5 and result['setup']['passed'] == result['setup']['total']
        run('empty stream', ['--verbose', empty], contains=b'0 PASS, 0 FAIL, 0 total')
        p = run('quiet default', [language])
        assert not p.stdout and not p.stderr
        run('failed assertion and first failure', ['--verbose', fail], code=1, contains=b'0 PASS, 1 FAIL')
        p = run('setup-only failure', ['--json', setup], code=1)
        result = json.loads(p.stdout)
        assert result['total'] == 0 and result['setup']['total'] == 1 and result['setup']['passed'] == 0
        p = run('truncated stream retains earlier assertion', ['--json', truncated], code=1)
        result = json.loads(p.stdout)
        assert result['passed'] == 1 and result['total'] == 2 and not result['completed']
        run('assertion recovery', ['--verbose', recovery], code=1, contains=b'1 PASS, 1 FAIL')
        run('standalone installed allocator', ['--verbose', ROOT / 'src/vfs/root/test/aux/libc/allocator.wast'], contains=b'6 PASS, 0 FAIL')
        run('automatic production context', ['--verbose', runtime], contains=b'1 PASS, 0 FAIL')
        binary = work / 'binary.wast'
        binary.write_text('(module binary "\\00asm\\01\\00\\00\\00\\01\\06\\01\\60\\01\\7f\\01\\7f" '
                          '"\\02\\0f\\01\\04libc\\06strlen\\00\\00")\n')
        p = run('binary ordinary module enables production context', ['--json', binary])
        assert json.loads(p.stdout)['setup']['passed'] == 1
        run('explicit production context', ['--context', 'runtime', '--verbose', runtime], contains=b'1 PASS, 0 FAIL')
        run('language context refuses production imports', ['--context', 'language', '--verbose', runtime], code=1, contains=b'Setup:')
        run('script provider precedence', ['--verbose', providers], contains=b'1 PASS, 0 FAIL')
        run('unlinkable retains absent libc', ['--verbose', unlinkable], contains=b'1 PASS, 0 FAIL')
        run('forced context rejects provider collision', ['--context', 'runtime', providers], code=1, contains=b'conflicts')
        run('forced context rejects linkage assertions', ['--context', 'runtime', unlinkable], code=1, contains=b'conflicts')
        combined = work / 'combined.wast'
        combined.write_text(runtime.read_text() + unlinkable.read_text())
        run('linkage assertions keep whole script language namespace', ['--json', combined], code=1)
        p = run('file isolation and failure aggregation', ['--json', providers, setup, language], code=1)
        # JSON is multiline; use the decoder to consume successive objects.
        decoder = json.JSONDecoder()
        records = []
        remaining = p.stdout.decode().strip()
        while remaining:
            item, end = decoder.raw_decode(remaining)
            records.append(item)
            remaining = remaining[end:].lstrip()
        assert len(records) == 3 and records[2]['passed'] == 5
        isolation = work / 'isolation.wast'
        isolation.write_text('(assert_unlinkable (module (import "libc" "strlen" (func (param i32) (result i32)))) "unknown import")\n')
        run('production providers never survive between files', ['--verbose', runtime, isolation], contains=b'1 PASS, 0 FAIL')
        run('native READ resume', ['--context', 'runtime', '--verbose', FIXTURES / 'input.wast'], data=b'x\n', contains=b'1 PASS, 0 FAIL')
        spin = work / 'spin.wast'
        spin.write_text('(module (func (export "spin") (loop br 0)))\n(invoke "spin")\n')
        run('explicit execution timeout', ['--timeout-ms', '10', spin], code=124)
        run('diagnostic count mode', ['--count', language], contains=b'2\n')
        run('diagnostic parse-only', ['--parse-only', language], contains=b'')
        run('diagnostic browser-spec', ['--browser-spec', language], contains=b'"groups"')
        run('outside working directory default root', ['--verbose', runtime], cwd=work, contains=b'1 PASS, 0 FAIL')
        missing = work / 'root'
        for name in ['root', 'tmp', 'usr/lib']:
            (missing / name).mkdir(parents=True, exist_ok=True)
        run('missing installed library', ['--vfs-root', missing, '--verbose', runtime], code=1, contains=b'cannot read shared library')
        run('script-owned providers need no installed libc', ['--vfs-root', missing, '--verbose', providers], contains=b'1 PASS, 0 FAIL')
        run('language assertions need no installed libc', ['--vfs-root', missing, '--verbose', unlinkable], contains=b'1 PASS, 0 FAIL')
        run('missing input', [work / 'absent'], code=1)
        fifo = work / 'fifo'
        os.mkfifo(fifo)
        run('FIFO input rejected without blocking', [fifo], code=1)
        p = run('guest output remains separate from assertion JSON', ['--json', FIXTURES / 'output.wast'])
        assert json.loads(p.stdout)['passed'] == 1 and p.stderr == b'guest\n'
        p = run('ordinary guest stdout', [FIXTURES / 'output.wast'])
        assert p.stdout == b'guest\n' and not p.stderr
        run('standalone scripts cannot launch nested batches', ['--verbose', ROOT / 'tests/guest-test-nested-capability.wast'], contains=b'1 PASS, 0 FAIL')
        run('CLI context error', ['--context', 'invalid', language], code=2)
        run('CLI incompatible output modes', ['--verbose', '--json', language], code=2)
        run('CLI timeout error', ['--timeout-ms', '0', language], code=2)
        # Sanitizer instrumentation needs a larger batch budget than the default.
        run('batch library context', ['--suite', '--group=aux/libc', '--group=system/libc', '--jobs=2',
            '--timeout-ms=30000', f'--results={work}/batch.json'], contains=b'14 PASS, 0 FAIL', timeout=60)
        batch = json.loads((work / 'batch.json').read_text())
        assert batch['summary']['skip'] == 0 and batch['summary']['pass'] == 14
    (output / ('results' + suffix + '.json')).write_text(json.dumps({'passed': len(checks), 'checks': checks}, indent=2) + '\n')
    print(f'PASS standalone WAST: {len(checks)} host boundaries ({suffix or "native"})')


if __name__ == '__main__':
    main()
