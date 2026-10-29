/* Guest observation fixture for host API allocation/cleanup checks. */
#include <unistd.h>

__attribute__((export_name("_start")))
void embedding_output(void) {
    (void)write(1, "output", 6);
}
