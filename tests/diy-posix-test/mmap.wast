;; Anonymous mappings exercise the process VM ABI used by the browser POSIX
;; resolver.  The host address zero is a valid request for the first free page.
(module
  (import "env" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "close" (func $close (param i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "env" "lseek" (func $lseek (param i32 i64 i32) (result i64)))
  (import "env" "ftruncate" (func $ftruncate (param i32 i64) (result i32)))
  (import "env" "shm_open" (func $shm_open (param i32 i32 i32) (result i32)))
  (import "env" "shm_unlink" (func $shm_unlink (param i32) (result i32)))
  (import "env" "unlink" (func $unlink (param i32) (result i32)))
  (import "env" "mmap" (func $mmap (param i32 i32 i32 i32 i32 i64) (result i32)))
  (import "env" "munmap" (func $munmap (param i32 i32) (result i32)))
  (import "env" "msync" (func $msync (param i32 i32 i32) (result i32)))
  (import "env" "mprotect" (func $mprotect (param i32 i32 i32) (result i32)))
  (memory 3)
  (data (i32.const 128) "/mmap-file\00")
  (data (i32.const 65664) "/shm-object\00")

  (func (export "__errno_location") (result i32)
    i32.const 65600)

  (func (export "mmap-anonymous") (result i32)
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4096
    i32.const 3
    i32.const 34
    i32.const -1
    i64.const 0
    call $mmap)

  (func (export "mprotect-readonly") (result i32)
    i32.const 0
    i32.const 1
    i32.const 1
    call $mprotect)

  (func (export "mprotect-readwrite") (result i32)
    i32.const 0
    i32.const 65536
    i32.const 3
    call $mprotect)

  (func (export "invalid-mmap-flags") (result i32)
    i32.const 0
    i32.const 4096
    i32.const 3
    i32.const 0
    i32.const -1
    i64.const 0
    call $mmap)

  (func (export "invalid-mmap-errno") (result i32)
    i32.const 0
    i32.const 4096
    i32.const 3
    i32.const 0
    i32.const -1
    i64.const 0
    call $mmap
    drop
    i32.const 65600
    i32.load)

  (func (export "split-mapping") (result i32)
    i32.const 0
    i32.const 131072
    call $munmap
    drop
    i32.const 0
    i32.const 196608
    i32.const 3
    i32.const 34
    i32.const -1
    i64.const 0
    call $mmap
    drop
    i32.const 65536
    i32.const 65536
    call $munmap
    drop
    i32.const 65536
    i32.const 4096
    i32.const 3
    i32.const 34
    i32.const -1
    i64.const 0
    call $mmap)

  (func (export "fixed-collision") (result i32)
    i32.const 65536
    i32.const 4096
    i32.const 3
    i32.const 34
    i32.const -1
    i64.const 0
    call $mmap)

  (func (export "fixed-noreplace-errno") (result i32)
    i32.const 65536
    i32.const 4096
    i32.const 3
    i32.const 1048610
    i32.const -1
    i64.const 0
    call $mmap
    drop
    i32.const 65600
    i32.load)

  (func (export "private-file-mapping") (result i32)
    (local $fd i32)
    (local $mapping i32)
    (local $value i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1768303984
    i32.store
    i32.const 65672
    i32.const 25964
    i32.store
    i32.const 65664
    i32.const 0
    i32.const 0
    call $open
    local.set $fd
    local.get $fd
    i32.const 0
    i32.lt_s
    if
      local.get $fd
      return
    end
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 65536
    i32.const 3
    i32.const 2
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.const 0
    i32.lt_s
    if
      i32.const 65600
      i32.load
      return
    end
    local.get $mapping
    i32.load8_u
    local.set $value
    local.get $fd
    call $close
    drop
    local.get $value)

  (func (export "private-partial-file-mapping") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1752378736
    i32.store
    i32.const 65672
    i32.const 7631471
    i32.store
    i32.const 65664
    i32.const 0
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4
    i32.const 1
    i32.const 2
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.load8_u
    local.get $mapping
    i32.load8_u offset=4
    i32.add)

  (func (export "shared-file-mapping") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1768303984
    i32.store
    i32.const 65672
    i32.const 25964
    i32.store
    i32.const 65664
    i32.const 0
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4
    i32.const 1
    i32.const 1
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.load8_u)

  (func (export "shared-readonly-mprotect-write-errno") (result i32)
    i32.const 0
    i32.const 4
    i32.const 3
    call $mprotect
    drop
    i32.const 65600
    i32.load)

  (func (export "shared-file-msync") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1768303984
    i32.store
    i32.const 65672
    i32.const 25964
    i32.store
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 65664
    i32.const 2
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 4
    i32.const 3
    i32.const 1
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $fd
    call $close
    drop
    local.get $mapping
    i32.const 90
    i32.store8
    local.get $mapping
    i32.const 4
    i32.const 4
    call $msync
    if
      i32.const 65600
      i32.load
      return
    end
    local.get $mapping
    i32.const 65536
    call $munmap
    drop
    i32.const 65664
    i32.const 0
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 4
    i32.const 1
    i32.const 2
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.load8_u)

  (func (export "private-file-msync-isolated") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1752378736
    i32.store
    i32.const 65672
    i32.const 7631471
    i32.store
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 65664
    i32.const 2
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 4
    i32.const 3
    i32.const 2
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.const 81
    i32.store8
    local.get $mapping
    i32.const 4
    i32.const 4
    call $msync
    drop
    local.get $mapping
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4
    i32.const 1
    i32.const 2
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.load8_u)

  (func (export "shared-file-unmap-writeback") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1752378736
    i32.store
    i32.const 65672
    i32.const 7631471
    i32.store
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 65664
    i32.const 2
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 4
    i32.const 3
    i32.const 1
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $fd
    call $close
    drop
    local.get $mapping
    i32.const 85
    i32.store8
    local.get $mapping
    i32.const 65536
    call $munmap
    drop
    i32.const 65664
    i32.const 0
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 4
    i32.const 1
    i32.const 2
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.load8_u)

  (func (export "shared-file-unlink-writeback") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1752378736
    i32.store
    i32.const 65672
    i32.const 7631471
    i32.store
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 65664
    i32.const 2
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 4
    i32.const 3
    i32.const 1
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    i32.const 65664
    call $unlink
    drop
    local.get $mapping
    i32.const 86
    i32.store8
    local.get $mapping
    i32.const 4
    i32.const 4
    call $msync
    drop
    local.get $fd
    i64.const 0
    i32.const 0
    call $lseek
    drop
    local.get $fd
    i32.const 65680
    i32.const 1
    call $read
    drop
    i32.const 65680
    i32.load8_u)

  ;; A mapping retains its file object, but a truncation makes its tail
  ;; invalid.  Until fault delivery is wired into guest memory accesses,
  ;; msync is the deterministic operation that reports this condition.
  (func (export "truncated-shared-mapping-errno") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1768303984
    i32.store
    i32.const 65672
    i32.const 25964
    i32.store
    i32.const 65664
    i32.const 2
    i32.const 0
    call $open
    local.set $fd
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4
    i32.const 3
    i32.const 1
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $fd
    i64.const 2
    call $ftruncate
    drop
    local.get $mapping
    i32.const 4
    i32.const 4
    call $msync
    drop
    i32.const 65600
    i32.load)

  ;; Ordinary Wasm memory access reaches the process-owned validator too.
  (func (export "truncated-shared-mapping-access") (result i32)
    (local $fd i32)
    (local $mapping i32)
    i32.const 65664
    i32.const 1634561327
    i32.store
    i32.const 65668
    i32.const 1768303984
    i32.store
    i32.const 65672
    i32.const 25964
    i32.store
    i32.const 65664
    i32.const 2
    i32.const 0
    call $open
    local.set $fd
    local.get $fd
    i64.const 4
    call $ftruncate
    drop
    local.get $fd
    i32.const 0
    i32.lt_s
    if
      i32.const 65600
      i32.load
      return
    end
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4
    i32.const 3
    i32.const 1
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.const 0
    i32.lt_s
    if
      i32.const 65600
      i32.load
      return
    end
    local.get $fd
    i64.const 2
    call $ftruncate
    drop
    local.get $mapping
    i32.load8_u)

  (func (export "named-shared-memory") (result i32)
    (local $fd i32)
    (local $mapping i32)
    ;; Earlier mapping cases intentionally reuse low scratch memory.  Rebuild
    ;; this pathname at the point of use so the named-object test is isolated.
    i32.const 65664
    i32.const 1835561775
    i32.store
    i32.const 65668
    i32.const 1784835885
    i32.store
    i32.const 65672
    i32.const 7627621
    i32.store
    i32.const 65664
    i32.const 66
    i32.const 384
    call $shm_open
    local.set $fd
    local.get $fd
    i64.const 4
    call $ftruncate
    drop
    local.get $fd
    i32.const 0
    i32.lt_s
    if
      i32.const 65600
      i32.load
      return
    end
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4
    i32.const 3
    i32.const 1
    local.get $fd
    i64.const 0
    call $mmap
    local.set $mapping
    local.get $mapping
    i32.const 0
    i32.lt_s
    if
      i32.const 65600
      i32.load
      return
    end
    local.get $mapping
    i32.const 90
    i32.store8
    local.get $mapping
    i32.const 4
    i32.const 4
    call $msync
    drop
    i32.const 65664
    call $shm_unlink
    drop
    local.get $mapping
    i32.load8_u)

  (func (export "invalid-msync-flags") (result i32)
    i32.const 0
    i32.const 4
    i32.const 0
    call $msync)

  (func (export "unmapped-msync-errno") (result i32)
    i32.const 0
    i32.const 65536
    call $munmap
    drop
    i32.const 0
    i32.const 4
    i32.const 4
    call $msync
    drop
    i32.const 65600
    i32.load)

  (func (export "invalid-munmap-errno") (result i32)
    i32.const 1
    i32.const 1
    call $munmap
    drop
    i32.const 65600
    i32.load)

  (func (export "invalid-mprotect-errno") (result i32)
    i32.const 1
    i32.const 1
    i32.const 1
    call $mprotect
    drop
    i32.const 65600
    i32.load))

(assert_return (invoke "mmap-anonymous") (i32.const 0))
(assert_return (invoke "mprotect-readonly") (i32.const 0))
(assert_return (invoke "mprotect-readwrite") (i32.const 0))
(assert_return (invoke "invalid-mmap-flags") (i32.const -1))
(assert_return (invoke "invalid-mmap-errno") (i32.const 22))
(assert_return (invoke "split-mapping") (i32.const 65536))
(assert_return (invoke "fixed-collision") (i32.const -1))
(assert_return (invoke "fixed-noreplace-errno") (i32.const 17))
(assert_return (invoke "named-shared-memory") (i32.const 90))
(assert_return (invoke "invalid-munmap-errno") (i32.const 22))
(assert_return (invoke "invalid-mprotect-errno") (i32.const 22))
(assert_return (invoke "private-file-mapping") (i32.const 109))
(assert_return (invoke "private-partial-file-mapping") (i32.const 116))
(assert_return (invoke "shared-file-mapping") (i32.const 109))
(assert_return (invoke "shared-readonly-mprotect-write-errno") (i32.const 13))
(assert_return (invoke "shared-file-msync") (i32.const 90))
(assert_return (invoke "private-file-msync-isolated") (i32.const 116))
(assert_return (invoke "shared-file-unmap-writeback") (i32.const 85))
(assert_return (invoke "shared-file-unlink-writeback") (i32.const 86))
(assert_return (invoke "invalid-msync-flags") (i32.const -1))
(assert_return (invoke "unmapped-msync-errno") (i32.const 12))
(assert_return (invoke "truncated-shared-mapping-errno") (i32.const 5))
(assert_trap (invoke "truncated-shared-mapping-access")
            "access past truncated file mapping")
