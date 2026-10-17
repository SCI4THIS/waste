/* Private native diagnostic, deliberately separate from executable loading. */
#include "engine_internal.h"
#include "wasm/decode.h"
#include "../config.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_string(const char *text) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}
int main(int argc, char **argv) {
    int validate = argc == 3 && strcmp(argv[1], "--validate-functions") == 0;
    if (argc != 2 && !validate) {
        fprintf(stderr, "usage: module-footprint [--validate-functions] FILE.wasm\n");
        return 2;
    }
    const char *path = argv[validate ? 2 : 1];
    FILE *input = fopen(path, "rb");
    if (!input) { perror(path); return 2; }
    if (fseek(input, 0, SEEK_END)) { fclose(input); return 2; }
    long length = ftell(input);
    if (length < 0 || (uint64_t)length > NATIVE_EXEC_BYTES_MAX ||
        fseek(input, 0, SEEK_SET)) { fclose(input); return 2; }
    unsigned char *bytes = malloc(length ? (size_t)length : 1);
    if (!bytes) { fclose(input); return 2; }
    size_t read = fread(bytes, 1, (size_t)length, input);
    fclose(input);
    if (read != (size_t)length) { free(bytes); return 2; }
    wasm_module module;
    wasm_decode_error decode_error = {0};
    wasm_decode_status decoded = wasm_decode_module(bytes, (size_t)length, &module, &decode_error);
    free(bytes);
    exec_module_footprint f = {0};
    exec_error error = {0};
    exec_status status = EXEC_ERROR_FORMAT;
    if (decoded == WASM_DECODE_OK) {
        status = validate ? exec_validate_functions_streamed(&module, &f, &error) :
                            exec_measure_module(&module, &f, &error);
        wasm_module_dispose(&module);
    }
    printf("{\"scope\":\"decoded-function-storage\",\"validationRequested\":%s,\"validated\":%s,\"runnableInstance\":false,\"executed\":false,\"complete\":%s,",
           validate ? "true" : "false", validate && status == EXEC_OK ? "true" : "false",
           status == EXEC_OK ? "true" : "false");
    printf("\"definedFunctions\":%u,\"importedFunctions\":%u,\"measuredFunctions\":%u,\"largestBufferFunction\":%u,",
           f.defined_functions, f.imported_functions, f.measured_functions, f.largest_function);
    printf("\"instructions\":%" PRIu64 ",\"instructionBytes\":%" PRIu64 ",\"localBytes\":%" PRIu64 ",\"payloadBytes\":%" PRIu64 ",",
           f.instructions, f.instruction_bytes, f.local_bytes, f.payload_bytes);
    printf("\"encodedInstructionBytes\":%" PRIu64 ",\"peakFunctionBufferBoundBytes\":%" PRIu64 ",\"metadataLowerBoundBytes\":%" PRIu64 ",\"error\":",
           f.encoded_instruction_bytes, f.peak_function_buffer_bytes, f.metadata_bytes);
    json_string(decoded == WASM_DECODE_OK ? error.message : decode_error.message);
    puts("}");
    return status == EXEC_OK ? 0 : 1;
}
