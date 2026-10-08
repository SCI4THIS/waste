#!/usr/bin/env python3
"""Current-directory discovery, live edits, capacities and root confinement."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src/html-rt/tools"))
from runtime_config import read_config
CONFIG = read_config()
runner = str(Path(sys.argv[1] if len(sys.argv) > 1 else 'build/cli-rt/waste-cli').resolve())

with tempfile.TemporaryDirectory(prefix='waste-directory-vfs-', dir=REPO / 'build/engine') as temporary:
    base = Path(temporary)
    root = base / 'vfs'
    root.mkdir()
    (root / 'data').mkdir()
    sample = root / 'data/sample'
    source = base / 'probe.wast'
    module = '''(module
      (import "waste_kernel" "path_access_v1" (func $access (param i32 i32 i32 i32) (result i32)))
      (import "waste_kernel" "path_stat_v1" (func $stat (param i32 i32 i32 i32) (result i32)))
      (memory 1)
      (data (i32.const 0) "/data/sample")
      (data (i32.const 32) "/tmp")
      (data (i32.const 64) "/bin/wast")
      (func (export "access") (param i32 i32) (result i32)
        (call $access (local.get 0) (local.get 1) (i32.const 0) (i32.const 0)))
      (func (export "size") (result i32)
        (if (call $stat (i32.const 0) (i32.const 12) (i32.const 1) (i32.const 256))
          (then (return (i32.const -1))))
        (i32.wrap_i64 (i64.load (i32.const 272)))))
    '''

    def run(ok=True, size=0, missing=False):
        source.write_text(module +
            f'(assert_return (invoke "access" (i32.const 0) (i32.const 12)) (i32.const {-2 if missing else 0}))\n' +
            f'(assert_return (invoke "size") (i32.const {-1 if missing else size}))\n' +
            '(assert_return (invoke "access" (i32.const 32) (i32.const 4)) (i32.const 0))\n' +
            '(assert_return (invoke "access" (i32.const 64) (i32.const 9)) (i32.const 0))\n')
        result = subprocess.run([runner, '--vfs-root', str(root), str(source)],
                                capture_output=True, text=True, timeout=15,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                     'UBSAN_OPTIONS': 'halt_on_error=1'})
        assert (result.returncode == 0) == ok, (result.returncode, result.stdout, result.stderr)
        assert 'AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr

    # No inventory is required; additions, edits and deletions are discovered.
    for size in (0, 1, 55, 56, 63, 64, 65, 127, 128, 4096):
        sample.write_bytes(bytes(i % 251 for i in range(size)))
        run(size=size)
    (root / '.inventory.json').write_text('obsolete, malformed metadata is ignored')
    sample.write_bytes(b'changed')
    run(size=7)
    sample.unlink()
    run(missing=True)
    (base / 'outside').write_bytes(b'payload')
    sample.symlink_to((base / 'outside').resolve())
    run(False)
    sample.unlink()
    (root / 'data').rename(base / 'outside-dir')
    (root / 'data').symlink_to((base / 'outside-dir').resolve(), target_is_directory=True)
    run(False)
    (root / 'data').unlink()
    (base / 'outside-dir').rename(root / 'data')
    os.mkfifo(sample)
    run(False)
    sample.unlink()
    (root / 'bad\nname').write_bytes(b'bad path')
    run(False)
    (root / 'bad\nname').unlink()
    # Sparse files exercise the byte bound without allocating that host space.
    with sample.open('wb') as file:
        file.truncate(CONFIG['VFS_MAX_BYTES'] + 1)
    run(False)
    sample.unlink()
    # Leave space for root plus generated boot nodes when checking the boundary.
    for i in range(CONFIG['VFS_MAX_ENTRIES']):
        (root / f'entry-{i}').touch()
    run(False)
    print('PASS current VFS: additions/edits/deletions, ignored legacy inventory, empty files, virtual boot nodes, capacities, unsafe paths, symlink/FIFO rejection')
