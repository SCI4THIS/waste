#include "source.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(condition, message) do { \
    checks++; \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); failures++; } \
} while (0)

int main(void) {
    waste_source_view view;
    waste_source_result result;
    static const char crlf[] = "#!/bin/wat --probe\r\n(module)";
    static const char no_final_lf[] = "#!/bin/wast\n(module)";
    static const char indented[] = " #!/bin/wat\n(module)";
    static const char hash[] = "# ordinary text\n(module)";
    static const char relative[] = "#!wat\n(module)";
    static const char empty[] = "#!\n(module)";
    static const char extra[] = "#!/bin/wat one two\n(module)";
    char overlong[SOURCE_SHEBANG_MAX_BYTES + 32];

    result = waste_source_view_init(crlf, sizeof(crlf) - 1, &view);
    CHECK(result == WASTE_SOURCE_OK && view.has_shebang,
          "CRLF shebang is accepted");
    CHECK(strcmp(view.interpreter, "/bin/wat") == 0 &&
          strcmp(view.argument, "--probe") == 0,
          "interpreter and optional argument are captured");
    CHECK(view.body_length == strlen("(module)") &&
          memcmp(view.body, "(module)", view.body_length) == 0 &&
          view.line_offset == 1,
          "CRLF body view skips exactly one source line");
    result = waste_source_view_init(no_final_lf, sizeof(no_final_lf) - 1,
                                    &view);
    CHECK(result == WASTE_SOURCE_OK && strcmp(view.interpreter, "/bin/wast") == 0 &&
          view.body_length == strlen("(module)"),
          "shebang without final LF is accepted");
    CHECK(waste_source_view_init(indented, sizeof(indented) - 1, &view) ==
              WASTE_SOURCE_NO_SHEBANG, "indented shebang is not recognized");
    CHECK(waste_source_view_init(hash, sizeof(hash) - 1, &view) ==
              WASTE_SOURCE_NO_SHEBANG, "ordinary hash is not a shebang");
    CHECK(waste_source_view_init(relative, sizeof(relative) - 1, &view) ==
              WASTE_SOURCE_INVALID_SHEBANG, "relative interpreter is rejected");
    CHECK(waste_source_view_init(empty, sizeof(empty) - 1, &view) ==
              WASTE_SOURCE_INVALID_SHEBANG, "empty interpreter is rejected");
    CHECK(waste_source_view_init(extra, sizeof(extra) - 1, &view) ==
              WASTE_SOURCE_INVALID_SHEBANG, "extra interpreter args are rejected");
    memset(overlong, 'x', sizeof(overlong));
    overlong[0] = '#'; overlong[1] = '!'; overlong[sizeof(overlong) - 1] = '\n';
    CHECK(waste_source_view_init(overlong, sizeof(overlong), &view) ==
              WASTE_SOURCE_SHEBANG_TOO_LONG, "overlong shebang is bounded");
    printf("source loader checks: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
