/* Fresh PIC input for real dlopen/dlsym and shared library state. */
static int value = 40;
int platform_add(int increment) {
    value += increment;
    return value;
}
