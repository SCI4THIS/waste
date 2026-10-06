(register "waste-libc" $waste_libc)
(module $account_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "waste_allocator_init" (func $init (param i32) (result i32)))
  (import "waste-libc" "waste_identity_set" (func $identity_set (param i32 i32 i32 i32 i32)))
  (import "waste-libc" "waste_passwd_set" (func $passwd_set (param i32 i32 i32 i32 i32 i32 i32)))
  (import "waste-libc" "waste_group_set" (func $group_set (param i32 i32 i32 i32)))
  (import "waste-libc" "waste_groups_set" (func $groups_set (param i32 i32) (result i32)))
  (import "waste-libc" "getuid" (func $getuid (result i32)))
  (import "waste-libc" "geteuid" (func $geteuid (result i32)))
  (import "waste-libc" "getgid" (func $getgid (result i32)))
  (import "waste-libc" "getgroups" (func $getgroups (param i32 i32) (result i32)))
  (import "waste-libc" "getpwuid" (func $getpwuid (param i32) (result i32)))
  (import "waste-libc" "getpwnam" (func $getpwnam (param i32) (result i32)))
  (import "waste-libc" "setpwent" (func $setpwent))
  (import "waste-libc" "getpwent" (func $getpwent (result i32)))
  (import "waste-libc" "gethostname" (func $gethostname (param i32 i32) (result i32)))
  (data (i32.const 180000) "alice\00x\00Alice\00/home/alice\00/bin/bash\00waste\00users\00")
  (func $configure
    i32.const 220000 call $init drop
    i32.const 1000 i32.const 1000 i32.const 100 i32.const 100 i32.const 180036 call $identity_set
    i32.const 180000 i32.const 180006 i32.const 1000 i32.const 100
    i32.const 180008 i32.const 180014 i32.const 180026 call $passwd_set
    i32.const 180042 i32.const 180006 i32.const 100 i32.const 181100 call $group_set
    i32.const 181100 i32.const 180000 i32.store
    i32.const 181104 i32.const 0 i32.store
    i32.const 181200 i32.const 100 i32.store
    i32.const 181204 i32.const 27 i32.store
    i32.const 2 i32.const 181200 call $groups_set drop)
  (func (export "identity-and-passwd") (result i32)
    (local $record i32)
    call $configure
    call $getuid i32.const 1000 i32.eq
    call $geteuid i32.const 1000 i32.eq i32.and
    call $getgid i32.const 100 i32.eq i32.and
    i32.const 1000 call $getpwuid local.tee $record i32.eqz i32.eqz i32.and
    local.get $record i32.load i32.const 180000 i32.eq i32.and
    local.get $record i32.load offset=8 i32.const 1000 i32.eq i32.and
    i32.const 180000 call $getpwnam local.get $record i32.eq i32.and
    call $setpwent call $getpwent local.get $record i32.eq i32.and)
  (func (export "groups-and-hostname") (result i32)
    call $configure
    i32.const 0 i32.const 0 call $getgroups i32.const 2 i32.eq
    i32.const 2 i32.const 181300 call $getgroups i32.const 2 i32.eq i32.and
    i32.const 181300 i32.load i32.const 100 i32.eq i32.and
    i32.const 181304 i32.load i32.const 27 i32.eq i32.and
    i32.const 181400 i32.const 32 call $gethostname i32.eqz i32.and
    i32.const 181400 i32.load i32.const 0x74736177 i32.eq i32.and))
(assert_return (invoke $account_tests "identity-and-passwd") (i32.const 1))
(assert_return (invoke $account_tests "groups-and-hostname") (i32.const 1))
