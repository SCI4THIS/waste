;; Setup must compile /tmp/windows-hello.obj and link /tmp/windows-hello.exe.
;; Bounded format checks, not Windows execution or a replacement linker.
(module $artifacts
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/tmp/windows-hello.obj\00")
  (data (i32.const 180064) "/tmp/windows-hello.exe\00")
  (data (i32.const 180128) "KERNEL32.dll\00")
  (data (i32.const 180160) "USER32.dll\00")
  (data (i32.const 180256) "/tmp/windows-changed.exe\00")
  (data (i32.const 180320) "Hello from WASTE!")
  (data (i32.const 180384) "Hello again from WASTE!")
  (data (i32.const 181000) "ExitProcess\00")
  (data (i32.const 181064) "GetModuleHandleA\00")
  (data (i32.const 181128) "CreateWindowExA\00")
  (data (i32.const 181192) "DefWindowProcA\00")
  (data (i32.const 181256) "DestroyWindow\00")
  (data (i32.const 181320) "DispatchMessageA\00")
  (data (i32.const 181384) "GetMessageA\00")
  (data (i32.const 181448) "LoadCursorA\00")
  (data (i32.const 181512) "PostQuitMessage\00")
  (data (i32.const 181576) "RegisterClassA\00")
  (data (i32.const 181640) "TranslateMessage\00")
  (data (i32.const 182000) "STATIC\00BUTTON\00Hello\00WASTE Windows demo\00")
  ;; File bytes start at 190000, below the end of the imported four pages.
  (global $size (mut i32) (i32.const 0))
  (global $opt (mut i32) (i32.const 0))
  (global $sections (mut i32) (i32.const 0))
  (global $count (mut i32) (i32.const 0))
  (global $executable (mut i32) (i32.const 0))
  (func $load (param $path i32) (result i32)
    (local $fd i32) (local $n i32)
    i32.const 0 global.set $size
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    block $end
      loop $read
        global.get $size i32.const 65536 i32.eq br_if $end
        local.get $fd i32.const 190000 global.get $size i32.add
        i32.const 65536 global.get $size i32.sub call $read local.tee $n
        i32.const 0 i32.le_s br_if $end
        global.get $size local.get $n i32.add global.set $size
        br $read
      end
    end
    local.get $fd call $close drop
    local.get $n i32.const 0 i32.eq
    global.get $size i32.const 0 i32.gt_u i32.and)
  (func $range (param $offset i32) (param $length i32) (result i32)
    local.get $offset global.get $size i32.gt_u
    if i32.const 0 return end
    local.get $length global.get $size local.get $offset i32.sub i32.le_u)
  (func $u16 (param $offset i32) (result i32)
    i32.const 190000 local.get $offset i32.add i32.load16_u)
  (func $u32 (param $offset i32) (result i32)
    i32.const 190000 local.get $offset i32.add i32.load)
  (func (export "coff-amd64") (result i32)
    i32.const 180000 call $load i32.eqz if i32.const 0 return end
    i32.const 0 i32.const 20 call $range i32.eqz if i32.const 0 return end
    i32.const 0 call $u16 i32.const 0x8664 i32.eq
    i32.const 2 call $u16 i32.const 0 i32.gt_u i32.and
    i32.const 2 call $u16 i32.const 32 i32.le_u i32.and
    i32.const 16 call $u16 i32.eqz i32.and)
  (func $headers (result i32)
    (local $pe i32) (local $optional_size i32)
    i32.const 0 i32.const 64 call $range i32.eqz if i32.const 0 return end
    i32.const 0 call $u16 i32.const 0x5a4d i32.ne if i32.const 0 return end
    i32.const 60 call $u32 local.tee $pe
    i32.const 24 call $range i32.eqz if i32.const 0 return end
    local.get $pe call $u32 i32.const 0x4550 i32.ne if i32.const 0 return end
    local.get $pe i32.const 4 i32.add call $u16 i32.const 0x8664 i32.ne
    if i32.const 0 return end
    local.get $pe i32.const 6 i32.add call $u16 global.set $count
    global.get $count i32.eqz if i32.const 0 return end
    global.get $count i32.const 32 i32.gt_u if i32.const 0 return end
    local.get $pe i32.const 20 i32.add call $u16 local.set $optional_size
    local.get $optional_size i32.const 128 i32.lt_u if i32.const 0 return end
    local.get $pe i32.const 24 i32.add global.set $opt
    global.get $opt local.get $optional_size call $range i32.eqz
    if i32.const 0 return end
    global.get $opt call $u16 i32.const 0x20b i32.ne if i32.const 0 return end
    global.get $opt local.get $optional_size i32.add global.set $sections
    global.get $sections global.get $count i32.const 40 i32.mul call $range)
  (func (export "pe32-plus-amd64") (result i32)
    i32.const 180064 call $load i32.eqz if i32.const 0 return end
    call $headers)
  (func $rva (param $rva i32) (param $length i32) (result i32)
    (local $i i32) (local $section i32) (local $va i32)
    (local $delta i32) (local $raw i32) (local $raw_size i32)
    i32.const 0 global.set $executable
    loop $scan
      local.get $i global.get $count i32.ge_u if i32.const 0 return end
      global.get $sections local.get $i i32.const 40 i32.mul i32.add local.set $section
      local.get $section i32.const 12 i32.add call $u32 local.set $va
      local.get $rva local.get $va i32.ge_u if
        local.get $rva local.get $va i32.sub local.set $delta
        local.get $section i32.const 16 i32.add call $u32 local.set $raw_size
        local.get $section i32.const 20 i32.add call $u32 local.set $raw
        local.get $delta local.get $raw_size i32.le_u if
          local.get $length local.get $raw_size local.get $delta i32.sub i32.le_u if
            local.get $raw local.get $raw_size call $range if
              local.get $section i32.const 36 i32.add call $u32
              i32.const 0x20000000 i32.and global.set $executable
              local.get $raw local.get $delta i32.add return
            end
          end
        end
      end
      local.get $i i32.const 1 i32.add local.set $i
      br $scan
    end
    i32.const 0)
  (func $entry (export "entrypoint-executable") (result i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 16 i32.add call $u32
    i32.const 1 call $rva i32.const 0 i32.gt_u
    global.get $executable i32.const 0 i32.ne i32.and)
  (func $gui (export "gui-subsystem") (result i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 68 i32.add call $u16 i32.const 2 i32.eq)
  (func $import_id (param $offset i32) (result i32)
    local.get $offset i32.const 12 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181000 i32.const 12
      call $memcmp i32.eqz if i32.const 1 return end
    end
    local.get $offset i32.const 17 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181064 i32.const 17
      call $memcmp i32.eqz if i32.const 2 return end
    end
    local.get $offset i32.const 16 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181128 i32.const 16
      call $memcmp i32.eqz if i32.const 4 return end
    end
    local.get $offset i32.const 15 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181192 i32.const 15
      call $memcmp i32.eqz if i32.const 8 return end
    end
    local.get $offset i32.const 14 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181256 i32.const 14
      call $memcmp i32.eqz if i32.const 16 return end
    end
    local.get $offset i32.const 17 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181320 i32.const 17
      call $memcmp i32.eqz if i32.const 32 return end
    end
    local.get $offset i32.const 12 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181384 i32.const 12
      call $memcmp i32.eqz if i32.const 64 return end
    end
    local.get $offset i32.const 12 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181448 i32.const 12
      call $memcmp i32.eqz if i32.const 128 return end
    end
    local.get $offset i32.const 16 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181512 i32.const 16
      call $memcmp i32.eqz if i32.const 256 return end
    end
    local.get $offset i32.const 15 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181576 i32.const 15
      call $memcmp i32.eqz if i32.const 512 return end
    end
    local.get $offset i32.const 17 call $range if
      local.get $offset i32.const 190000 i32.add i32.const 181640 i32.const 17
      call $memcmp i32.eqz if i32.const 1024 return end
    end
    i32.const 0)
  (func $imports (export "exact-windows-imports") (result i32)
    (local $directory i32) (local $name i32) (local $table i32)
    (local $i i32) (local $thunk i32) (local $id i32) (local $seen i32)
    (local $descriptor i32) (local $dll i32) (local $dlls i32)
    (local $allowed i32) (local $expected i32) (local $index i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 108 i32.add call $u32 i32.const 2 i32.lt_u
    if i32.const 0 return end
    global.get $opt i32.const 124 i32.add call $u32 i32.const 60 i32.lt_u
    if i32.const 0 return end
    global.get $opt i32.const 120 i32.add call $u32
    i32.const 60 call $rva local.tee $directory i32.eqz if i32.const 0 return end
    ;; Exactly two descriptors, followed by a completely zero descriptor.
    loop $terminator
      local.get $directory i32.const 40 i32.add local.get $i i32.add call $u32
      if i32.const 0 return end
      local.get $i i32.const 4 i32.add local.tee $i i32.const 20 i32.lt_u
      br_if $terminator
    end
    loop $dll_loop
      local.get $directory local.get $index i32.const 20 i32.mul i32.add local.set $descriptor
      local.get $descriptor i32.const 12 i32.add call $u32
      i32.const 13 call $rva local.tee $name i32.eqz if i32.const 0 return end
      local.get $name i32.const 190000 i32.add i32.const 180128 i32.const 13 call $memcmp i32.eqz
      if
        i32.const 1 local.set $dll
        i32.const 3 local.set $allowed
        i32.const 2 local.set $expected
      else
        local.get $name i32.const 190000 i32.add i32.const 180160 i32.const 11 call $memcmp
        if i32.const 0 return end
        i32.const 2 local.set $dll
        i32.const 2044 local.set $allowed
        i32.const 9 local.set $expected
      end
      local.get $dlls local.get $dll i32.and if i32.const 0 return end
      local.get $dlls local.get $dll i32.or local.set $dlls
      local.get $descriptor call $u32 local.tee $table i32.eqz if
        local.get $descriptor i32.const 16 i32.add call $u32 local.set $table
      end
      local.get $table local.get $expected i32.const 1 i32.add i32.const 8 i32.mul
      call $rva local.tee $table i32.eqz if i32.const 0 return end
      i32.const 0 local.set $i
      loop $symbols
        local.get $table local.get $i i32.const 8 i32.mul i32.add local.set $thunk
        local.get $thunk i32.const 4 i32.add call $u32 if i32.const 0 return end
        ;; Hint plus the longest admitted symbol name, bounded by its section.
        local.get $thunk call $u32 i32.const 20 call $rva local.tee $name i32.eqz
        if i32.const 0 return end
        local.get $name i32.const 2 i32.add call $import_id local.tee $id i32.eqz
        if i32.const 0 return end
        local.get $allowed local.get $id i32.and i32.eqz if i32.const 0 return end
        local.get $seen local.get $id i32.and if i32.const 0 return end
        local.get $seen local.get $id i32.or local.set $seen
        local.get $i i32.const 1 i32.add local.tee $i local.get $expected i32.lt_u
        br_if $symbols
      end
      local.get $table local.get $expected i32.const 8 i32.mul i32.add local.tee $thunk
      call $u32 if i32.const 0 return end
      local.get $thunk i32.const 4 i32.add call $u32 if i32.const 0 return end
      local.get $index i32.const 1 i32.add local.tee $index i32.const 2 i32.lt_u
      br_if $dll_loop
    end
    local.get $seen i32.const 2047 i32.eq
    local.get $dlls i32.const 3 i32.eq i32.and)
  (func (export "console-subsystem-rejected") (result i32)
    (local $address i32) (local $saved i32) (local $ok i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 68 i32.add i32.const 190000 i32.add local.tee $address
    i32.load16_u local.set $saved
    local.get $address i32.const 3 i32.store16
    call $gui i32.eqz local.set $ok
    local.get $address local.get $saved i32.store16
    local.get $ok)
  (func (export "unexpected-dll-rejected") (result i32)
    (local $address i32) (local $saved i32) (local $ok i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 120 i32.add call $u32 i32.const 60 call $rva
    i32.const 12 i32.add call $u32 i32.const 13 call $rva
    i32.const 190000 i32.add local.tee $address i32.load8_u local.set $saved
    local.get $address i32.const 88 i32.store8
    call $imports i32.eqz local.set $ok
    local.get $address local.get $saved i32.store8
    local.get $ok)
  (func (export "truncated-header-rejected") (result i32)
    (local $saved i32) (local $ok i32)
    global.get $size local.set $saved
    i32.const 63 global.set $size
    call $headers i32.eqz local.set $ok
    local.get $saved global.set $size
    local.get $ok)
  (func (export "overflowing-pe-offset-rejected") (result i32)
    (local $saved i32) (local $ok i32)
    i32.const 60 call $u32 local.set $saved
    i32.const 190060 i32.const -1 i32.store
    call $headers i32.eqz local.set $ok
    i32.const 190060 local.get $saved i32.store
    local.get $ok)
  (func (export "invalid-import-rva-rejected") (result i32)
    (local $address i32) (local $saved i32) (local $ok i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 120 i32.add local.set $address
    local.get $address call $u32 local.set $saved
    local.get $address i32.const 190000 i32.add i32.const -1 i32.store
    call $imports i32.eqz local.set $ok
    local.get $address i32.const 190000 i32.add local.get $saved i32.store
    local.get $ok)
  (func (export "ordinal-import-rejected") (result i32)
    (local $table i32) (local $saved i32) (local $ok i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 120 i32.add call $u32
    i32.const 40 call $rva local.tee $table i32.eqz if i32.const 0 return end
    local.get $table call $u32 i32.const 32 call $rva local.tee $table i32.eqz
    if i32.const 0 return end
    local.get $table i32.const 4 i32.add call $u32 local.set $saved
    local.get $table i32.const 190004 i32.add i32.const 0x80000000 i32.store
    call $imports i32.eqz local.set $ok
    local.get $table i32.const 190004 i32.add local.get $saved i32.store
    local.get $ok)
  (func (export "duplicate-import-rejected") (result i32)
    (local $table i32) (local $saved i32) (local $ok i32)
    call $headers i32.eqz if i32.const 0 return end
    global.get $opt i32.const 120 i32.add call $u32
    i32.const 40 call $rva local.tee $table i32.eqz if i32.const 0 return end
    local.get $table call $u32 i32.const 32 call $rva local.tee $table i32.eqz
    if i32.const 0 return end
    local.get $table i32.const 8 i32.add call $u32 local.set $saved
    local.get $table i32.const 190008 i32.add local.get $table call $u32 i32.store
    call $imports i32.eqz local.set $ok
    local.get $table i32.const 190008 i32.add local.get $saved i32.store
    local.get $ok)
  (func $contains (param $text i32) (param $length i32) (result i32)
    (local $offset i32)
    block $end
      loop $scan
        local.get $offset local.get $length call $range i32.eqz br_if $end
        i32.const 190000 local.get $offset i32.add local.get $text local.get $length
        call $memcmp i32.eqz if i32.const 1 return end
        local.get $offset i32.const 1 i32.add local.set $offset
        br $scan
      end
    end
    i32.const 0)
  (func (export "window-controls") (result i32)
    i32.const 180064 call $load i32.eqz if i32.const 0 return end
    i32.const 182000 i32.const 7 call $contains
    i32.const 182007 i32.const 7 call $contains i32.and
    i32.const 182014 i32.const 6 call $contains i32.and
    i32.const 182020 i32.const 19 call $contains i32.and)
  (func (export "original-greeting") (result i32)
    i32.const 180064 call $load i32.eqz if i32.const 0 return end
    i32.const 180320 i32.const 17 call $contains)
  (func (export "changed-greeting-artifact") (result i32)
    i32.const 180256 call $load i32.eqz if i32.const 0 return end
    call $headers i32.eqz if i32.const 0 return end
    call $entry i32.eqz if i32.const 0 return end
    call $gui i32.eqz if i32.const 0 return end
    call $imports i32.eqz if i32.const 0 return end
    i32.const 180384 i32.const 23 call $contains
    i32.const 180320 i32.const 17 call $contains i32.eqz i32.and))
(assert_return (invoke $artifacts "coff-amd64") (i32.const 1))
(assert_return (invoke $artifacts "pe32-plus-amd64") (i32.const 1))
(assert_return (invoke $artifacts "entrypoint-executable") (i32.const 1))
(assert_return (invoke $artifacts "gui-subsystem") (i32.const 1))
(assert_return (invoke $artifacts "exact-windows-imports") (i32.const 1))
(assert_return (invoke $artifacts "truncated-header-rejected") (i32.const 1))
(assert_return (invoke $artifacts "overflowing-pe-offset-rejected") (i32.const 1))
(assert_return (invoke $artifacts "invalid-import-rva-rejected") (i32.const 1))
(assert_return (invoke $artifacts "ordinal-import-rejected") (i32.const 1))
(assert_return (invoke $artifacts "duplicate-import-rejected") (i32.const 1))
(assert_return (invoke $artifacts "exact-windows-imports") (i32.const 1))
(assert_return (invoke $artifacts "original-greeting") (i32.const 1))
(assert_return (invoke $artifacts "changed-greeting-artifact") (i32.const 1))

(assert_return (invoke $artifacts "console-subsystem-rejected") (i32.const 1))
(assert_return (invoke $artifacts "unexpected-dll-rejected") (i32.const 1))
(assert_return (invoke $artifacts "window-controls") (i32.const 1))
