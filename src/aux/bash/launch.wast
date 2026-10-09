;; Authored bootstrap: load the installed executable through the engine kernel.
;; Keep the initial process memory/table available to the dynamic exec loader.
(module $launch
  (import "env" "execve" (func $execve (param i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 2)
  (table (export "__indirect_function_table") 1 funcref)
  (global (export "__stack_pointer") (mut i32) (i32.const 131072))
  ;; argv: /usr/bin/bash --norc -i; env: HOME USER LOGNAME PWD PATH TERM PS1.
  (data (i32.const 1024)
    "/usr/bin/bash\00--norc\00-i\00"
    "HOME=/root\00USER=root\00LOGNAME=root\00PWD=/root\00"
    "PATH=/bin:/usr/bin\00TERM=xterm\00PS1=# \00")
  (data (i32.const 256) "\00\04\00\00\0e\04\00\00\15\04\00\00\00\00\00\00")
  (data (i32.const 272) "\18\04\00\00\23\04\00\00\2d\04\00\00\3a\04\00\00\44\04\00\00\57\04\00\00\62\04\00\00\00\00\00\00")
  (func (export "main")
    (drop (call $execve (i32.const 1024) (i32.const 256) (i32.const 272)))
    ;; Successful exec never returns to this module.
    (call $exit (i32.const 127))))
(register "waste-runtime" $launch)
(invoke $launch "main")
