/* Trivial shared library source for testing the WASTE dynamic loader.
 * Compiled to PIC wasm with: clang --target=wasm32 -fPIC -c -o trivial.o
 * Linked with: wasm-ld -shared --experimental-pic -o trivial.so.wasm trivial.o
 */
int shared_add(int a, int b) { return a + b; }
int shared_mul(int a, int b) { return a * b; }

static int counter;
int shared_increment(void) { return ++counter; }
