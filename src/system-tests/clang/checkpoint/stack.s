# Read the actual guest Wasm global, not a linear-memory symbol relocation.
.globaltype __stack_pointer, i32
.functype probe_stack_pointer () -> (i32)
.globl probe_stack_pointer
probe_stack_pointer:
  .functype probe_stack_pointer () -> (i32)
  global.get __stack_pointer
  end_function
