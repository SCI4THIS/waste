#!/usr/bin/env python3
"""Direct native directory boundaries: inventory metadata, bytes and root confinement."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

runner = str(Path(sys.argv[1] if len(sys.argv) > 1 else 'build/cli-rt/waste-cli').resolve())
with tempfile.TemporaryDirectory(prefix='waste-directory-vfs-') as temporary:
    base = Path(temporary)
    root = base/'vfs'
    root.mkdir()
    (root/'data').mkdir()
    # /root and /tmp deliberately have no physical directories: the inventory
    # preserves empty guest directories after a checkout.
    entries = [dict(path=p, role='directory', kind=2, mode=0o755, uid=0, gid=0,
                    size=0, inode=i+1, mtime_sec=0, mtime_nsec=0)
               for i, p in enumerate(['/', '/root', '/tmp', '/data'])]
    entry = dict(path='/data/sample', role='file', kind=1, mode=0o644, uid=17, gid=23,
                 size=0, inode=5, mtime_sec=-9223372036854775808, mtime_nsec=999999999)
    entries.append(entry)
    manifest = dict(version=1, entries=entries)
    inventory = root/'.inventory.json'
    source = base/'probe.wast'
    source.write_text('(module (func (export "ok") (result i32) (i32.const 1)))\n'
                      '(assert_return (invoke "ok") (i32.const 1))\n')

    def install(data=b'payload'):
        (root/'data/sample').write_bytes(data)
        entry.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        inventory.write_text(json.dumps(manifest))

    def run(ok=True):
        r = subprocess.run([runner, '--vfs-root', str(root), str(source)],
                           capture_output=True, text=True, timeout=15,
                           env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0',
                                'UBSAN_OPTIONS':'halt_on_error=1'})
        assert (r.returncode == 0) == ok, (r.returncode, r.stderr)
        assert 'AddressSanitizer' not in r.stderr and 'runtime error:' not in r.stderr, r.stderr

    for size in [0, 1, 55, 56, 63, 64, 65, 127, 128, 4096]:
        install(bytes(i % 251 for i in range(size)))
        run()
    install()
    for mutate in [
        lambda m: m.update(version=2),
        lambda m: m['entries'][0].update(path='/../escape'),
        lambda m: m['entries'][4].update(path='/data//sample'),
        lambda m: m['entries'][4].update(path='/data/sample\0extra'),
        lambda m: m['entries'][4].update(mode=1024),
        lambda m: m['entries'][4].update(uid=2**32),
        lambda m: m['entries'][4].update(inode=2**64),
        lambda m: m['entries'][4].update(inode=1),
        lambda m: m['entries'][4].update(mtime_sec=-(2**63)-1),
        lambda m: m['entries'][4].update(mtime_nsec=10**9),
        lambda m: m['entries'][4].update(size=64*1024*1024+1),
        lambda m: m['entries'].append(copy.deepcopy(m['entries'][4])),
        lambda m: m['entries'].reverse(),
    ]:
        changed = copy.deepcopy(manifest)
        mutate(changed)
        inventory.write_text(json.dumps(changed))
        run(False)
    install()
    original = inventory.read_bytes()
    for data in [original[:-1], original+b' garbage', b'{"version":1,"entries":[],"extra":"\xff"}']:
        inventory.write_bytes(data)
        run(False)
    install()
    (root/'data/sample').write_bytes(b'changed')
    run(False)
    (root/'data/sample').unlink()
    run(False)
    (base/'outside').write_bytes(b'payload')
    (root/'data/sample').symlink_to(base/'outside')
    run(False)
    (root/'data/sample').unlink()
    install()
    (root/'data').rename(base/'outside-dir')
    (root/'data').symlink_to(base/'outside-dir', target_is_directory=True)
    run(False)
    (root/'data').unlink()
    (base/'outside-dir').rename(root/'data')
    install()
    (root/'data/sample').unlink()
    os.mkfifo(root/'data/sample')
    run(False)
    print('PASS directory VFS: hash boundaries, metadata bounds, malformed inventory, empty directories, missing/changed files, symlink confinement and FIFO rejection')
