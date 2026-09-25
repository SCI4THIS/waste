/* Minimal WASTE application CRT.  The engine owns startup memory and exit;
 * this object only translates that ABI into a normal C main call. */
typedef unsigned int u32;
typedef int i32;
#ifdef WASTE_MAIN_TWO_ARGS
typedef i32 (*waste_main_entry)(i32, char **);
#else
typedef i32 (*waste_main_entry)(i32, char **, char **);
#endif

__attribute__((import_module("waste_kernel"), import_name("startup_v1")))
extern i32 waste_kernel_startup_v1(void);
__attribute__((import_module("env"), import_name("exit")))
extern void waste_env_exit(i32 status);

/* Linkers may provide this when constructors are present. */
extern void __wasm_call_ctors(void) __attribute__((weak));

i32 __waste_startup_call(waste_main_entry entry) {
  i32 block;
  if (!entry) return 127;
  block = waste_kernel_startup_v1();
  if (block < 0) return 127;
#ifdef WASTE_MAIN_TWO_ARGS
  return entry(*(i32 *)(u32)block,
               (char **)(u32)*(u32 *)(u32)(block + 4));
#else
  return entry(*(i32 *)(u32)block,
               (char **)(u32)*(u32 *)(u32)(block + 4),
               (char **)(u32)*(u32 *)(u32)(block + 12));
#endif
}

#ifdef WASTE_MAIN_TWO_ARGS
extern i32 main(i32, char **);
#else
extern i32 main(i32, char **, char **);
#endif

__attribute__((export_name("_start")))
void _start(void) {
  i32 status;
  if (__wasm_call_ctors) __wasm_call_ctors();
  status = __waste_startup_call(main);
  waste_env_exit(status);
}
