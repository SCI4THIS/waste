#include "test_suite.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lib/json.h"

static int canonical(const char *p, int absolute) {
    if (!*p || (absolute && *p++ != '/')) return 0;
    const char *start = p;
    for (;;) {
        unsigned char ch = (unsigned char)*p;
        if (!ch || ch == '/') {
            size_t n = (size_t)(p - start);
            if (!n || (n == 1 && *start == '.') ||
                (n == 2 && start[0] == '.' && start[1] == '.')) return 0;
            if (!ch) return 1;
            start = p + 1;
        } else if (ch < 32 || ch == '\\') return 0;
        p++;
    }
}

static int asset_decode(json_view object, waste_suite_asset *asset) {
    json_view mode;
    if (!field(object, "path", asset->path, sizeof(asset->path)) ||
        strncmp(asset->path, WASTE_SUITE_SUPPORT_PREFIX,
                sizeof(WASTE_SUITE_SUPPORT_PREFIX) - 1u) || !canonical(asset->path, 1) ||
        !field(object, "mountPath", asset->mount_path, sizeof(asset->mount_path)) ||
        !canonical(asset->mount_path, 1) || !member(object, "mode", &mode)) return 0;
    uint32_t n = 0;
    for (const char *p = mode.start; p < mode.end; p++) {
        if (*p < '0' || *p > '9' || n > 0777u) return 0;
        n = n * 10u + (unsigned)(*p - '0');
    }
    if (n > 0777u) return 0;
    asset->mode = n;
    return 1;
}

static int test_decode(json_view object, waste_suite_test *test) {
    json_view spec, assets;
    if (!field(object, "id", test->identity, sizeof(test->identity)) ||
        !canonical(test->identity, 0) ||
        !field(object, "group", test->group, sizeof(test->group)) ||
        !canonical(test->group, 0) ||
        !field(object, "path", test->path, sizeof(test->path)) ||
        strncmp(test->path, WASTE_SUITE_TEST_PREFIX,
                sizeof(WASTE_SUITE_TEST_PREFIX) - 1u) ||
        strcmp(test->path + sizeof(WASTE_SUITE_TEST_PREFIX) - 1u, test->identity) ||
        !boolean(object, "unsupported", &test->unsupported) ||
        !boolean(object, "expectFailure", &test->expect_failure) ||
        !member(object, "executionSpec", &spec) ||
        !field(spec, "mode", test->mode, sizeof(test->mode)) ||
        !field(spec, "file", test->file, sizeof(test->file)) ||
        !member(object, "assets", &assets) || assets.type != '[') return 0;
    size_t group_length = strlen(test->group);
    if (strncmp(test->identity, test->group, group_length) ||
        test->identity[group_length] != '/') return 0;
    if (test->unsupported) {
        if (!field(object, "unsupportedReason", test->skip_reason,
                   sizeof(test->skip_reason)) || !test->skip_reason[0]) return 0;
    } else if (strcmp(test->mode, "wast-stream")) {
        snprintf(test->skip_reason, sizeof(test->skip_reason),
                 "execution mode %s requires browser compatibility backend", test->mode);
    }
    json_reader r = {assets.start + 1, assets.end - 1};
    whitespace(&r);
    while (r.at < r.end) {
        json_view asset;
        if (test->asset_count >= WASTE_SUITE_MAX_ASSETS || !value(&r, &asset, 0)) return 0;
        char kind[64];
        if (!field(asset, "kind", kind, sizeof(kind))) return 0;
        if (!strcmp(kind, "vfs-file")) {
            waste_suite_asset *next = realloc(test->assets,
                (test->asset_count + 1u) * sizeof(*next));
            if (!next) return 0;
            test->assets = next;
            if (!asset_decode(asset, &next[test->asset_count])) return 0;
            test->asset_count++;
        } else if (!test->skip_reason[0]) return 0;
        whitespace(&r);
        if (r.at < r.end) r.at++;
        whitespace(&r);
    }
    return 1;
}

void waste_suite_free(waste_suite *suite) {
    if (!suite) return;
    for (unsigned i = 0; i < suite->count; i++) free(suite->tests[i].assets);
    free(suite->tests);
    memset(suite, 0, sizeof(*suite));
}

int waste_suite_decode(const char *bytes, size_t length, waste_suite *suite,
                        char *error, size_t error_length) {
    if (!suite) return -1;
    memset(suite, 0, sizeof(*suite));
    if (!bytes || !length || length > WASTE_SUITE_MAX_BYTES) goto invalid;
    json_reader r = {bytes, bytes + length};
    json_view root, tests, format;
    if (!value(&r, &root, 0)) goto invalid;
    whitespace(&r);
    if (r.at != r.end || !member(root, "format", &format) || !literal(format, "1") ||
        !member(root, "tests", &tests) || tests.type != '[') goto invalid;
    r.at = tests.start + 1;
    r.end = tests.end - 1;
    whitespace(&r);
    while (r.at < r.end) {
        json_view object;
        if (suite->count >= WASTE_SUITE_MAX_TESTS || !value(&r, &object, 0)) goto invalid;
        waste_suite_test *next = realloc(suite->tests, (suite->count + 1u) * sizeof(*next));
        if (!next) goto invalid;
        suite->tests = next;
        waste_suite_test *test = &next[suite->count++];
        memset(test, 0, sizeof(*test));
        if (!test_decode(object, test)) goto invalid;
        for (unsigned i = 0; i + 1 < suite->count; i++)
            if (!strcmp(next[i].identity, test->identity)) goto invalid;
        whitespace(&r);
        if (r.at < r.end) r.at++;
        whitespace(&r);
    }
    if (!suite->count) goto invalid;
    return 0;
invalid:
    if (error && error_length)
        snprintf(error, error_length, "invalid or out-of-bounds test manifest (format 1)");
    waste_suite_free(suite);
    return -1;
}
