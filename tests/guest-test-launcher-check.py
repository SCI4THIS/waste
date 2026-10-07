#!/usr/bin/env python3
"""Native/production-worker boundary parity for the portable guest launcher."""
import argparse
import json
import re
import shutil
from pathlib import Path
import subprocess
import tempfile
import importlib.util
_spec = importlib.util.spec_from_file_location("guest_session_check", Path(__file__).with_name("guest-session-check.py"))
_driver = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_driver)
native_session = _driver.native_session


def reports(output):
    text = output.replace('\r', '')
    decoder = json.JSONDecoder()
    return [decoder.raw_decode(text[match.start():])[0]
            for match in re.finditer(r'(?m)^\{"(?:summary|count|tests)":', text)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', default='build/cli-rt/waste-session')
    parser.add_argument('--vfs-root', default='src/vfs')
    parser.add_argument('--page', default='build/html-rt/bash.html')
    args = parser.parse_args()
    scenario_path = 'tests/guest-session-test-suite.json'
    scenario = json.loads(Path(scenario_path).read_text())
    with tempfile.TemporaryDirectory(prefix='guest-suite-', dir='build/engine') as tmp:
        code, output, result = native_session(args.native, args.vfs_root, None,
            scenario['events'], Path(tmp)/'session.json', timeout=120,
            extra_args=['--timeout-ms', '120000'])
        assert code == 0 and result['passed'] == result['total'] == 7, result
        native_output = output.decode()
        for marker in scenario['contains']:
            assert marker in native_output, native_output
        browser = json.loads(subprocess.check_output(['node', 'tests/guest-session-worker.cjs',
            args.page, scenario_path], text=True))
        assert browser['passed'] == browser['total'] == 7 and browser['exitStatus'] == 0, browser
        Path('build/engine/refactor-stage6b-guest-suite/native-guest.txt').write_bytes(output)
        Path('build/engine/refactor-stage6b-guest-suite/browser-guest.txt').write_text(browser['output'])
        native_reports, browser_reports = reports(native_output), reports(browser['output'])
        assert len(native_reports) == len(browser_reports) == 4, (native_output, browser['output'])
        assert native_reports[0] == native_reports[1]
        assert browser_reports[0] == browser_reports[1]
        assert native_reports[0]['count'] == browser_reports[0]['count'] == 1
        for native, browser_report in zip(native_reports[2:], browser_reports[2:]):
            assert native['summary'] == browser_report['summary'], (native, browser_report)
            assert [(t['identity'], t['status'], t.get('passed') if t['status'] != 'SKIP' else None, t.get('total') if t['status'] != 'SKIP' else None) for t in native['tests']] == [
                (t['identity'], t['status'], t.get('passed') if t['status'] != 'SKIP' else None, t.get('total') if t['status'] != 'SKIP' else None) for t in browser_report['tests']]
        evidence = Path('build/engine/refactor-stage6b-guest-suite')
        evidence.mkdir(exist_ok=True)
        suffix = '-sanitize' if 'sanitize' in args.native else ''
        (evidence/f'native-guest{suffix}.txt').write_bytes(output)
        (evidence/f'browser-guest{suffix}.txt').write_text(browser['output'])
        (evidence/f'guest-parity{suffix}.json').write_text(json.dumps({
            'native':result, 'nativeReports':native_reports, 'browserReports':browser_reports}, indent=2)+'\n')
        mutable_root = Path(tmp)/'mutable-root'
        shutil.copytree(args.vfs_root, mutable_root)
        snapshot_events = [{'after':'bash-5.2# ', 'text':
            'waste-test --list path-runtime.wast; printf "__SNAPSHOT_STATUS__%s\\n" "$?"; exit 0\n'}]
        code, snapshot_output, snapshot_result = native_session(args.native, str(mutable_root), None,
            snapshot_events, Path(tmp)/'snapshot.json', timeout=120,
            extra_args=['--timeout-ms', '120000'],
            before_event=lambda _index, _event: (mutable_root.rename(Path(tmp)/'original-root'), mutable_root.mkdir()))
        assert code == 0 and snapshot_result['passed'] == snapshot_result['total'] == 7, snapshot_result
        assert b'__SNAPSHOT_STATUS__0\r\n' in snapshot_output, snapshot_output
        assert b'PASS? libc-test/path-runtime.wast' in snapshot_output, snapshot_output
        eof_events = [
            {'after':'bash-5.2# ', 'text':'echo __EOF_BATCH_BEGIN__; waste-test address.wast\n'},
            {'after':'__EOF_BATCH_BEGIN__\r\n', 'eof':True, 'duringBatch':True}]
        code, eof_output, eof_result = native_session(args.native, args.vfs_root, None,
            eof_events, Path(tmp)/'eof.json', timeout=120, extra_args=['--timeout-ms', '120000'])
        assert code == 0 and eof_result['passed'] == eof_result['total'] == 7, eof_result
        assert eof_result['exited'] and b'PASS core/address.wast' in eof_output, eof_output
        print('PASS guest launcher: native/production-worker listing, isolation, JSON redirection, timeout, XFAIL, option bounds, pinned directory identity, terminal EOF and parent shell')


if __name__ == '__main__':
    main()
