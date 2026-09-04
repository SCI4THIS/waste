#include "waste_exec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int run(waste_exec_engine *engine, const char *name, int32_t a, int32_t b,
               int32_t expected, exec_status expected_status) {
    wasm_value args[2] = {{.type=WASM_VALTYPE_I32, .i32=a}, {.type=WASM_VALTYPE_I32, .i32=b}};
    wasm_value result[1];
    uint32_t function;
    int count = 0;
    exec_error error = {0};
    exec_status status = exec_find_export(engine, name, &function, &error);
    if (status == EXEC_OK) status = exec_invoke(engine, function, args, 2, result, &count, &error);
    if (status != expected_status || (status == EXEC_OK && (count != 1 || result[0].i32 != expected))) {
        fprintf(stderr, "%s failed: status=%d result=%d error=%s\n", name, status,
                count ? result[0].i32 : 0, error.message);
        return 0;
    }
    return 1;
}

static int run_pair(waste_exec_engine *engine) {
    wasm_value args[2] = {{.type=WASM_VALTYPE_I32, .i32=11}, {.type=WASM_VALTYPE_I32, .i32=22}};
    wasm_value results[WAST_MAX_RESULTS]; uint32_t function; int count = 0; exec_error error = {0};
    exec_status status = exec_find_export(engine, "pair", &function, &error);
    if (status == EXEC_OK) status = exec_invoke(engine, function, args, 2, results, &count, &error);
    if (status != EXEC_OK || count != 2 || results[0].i32 != 11 || results[1].i32 != 22) {
        fprintf(stderr, "pair failed: status=%d count=%d error=%s\n", status, count, error.message);
        return 0;
    }
    return 1;
}

static int run_scalar_globals(waste_exec_engine *engine) {
    const char *names[] = {"global-i64", "global-f32", "global-f64"};
    wasm_valtype types[] = {WASM_VALTYPE_I64, WASM_VALTYPE_F32, WASM_VALTYPE_F64};
    wasm_value args[2] = {{.type=WASM_VALTYPE_I32}, {.type=WASM_VALTYPE_I32}};
    for (int i = 0; i < 3; i++) {
        uint32_t function; wasm_value result[1]; int count=0; exec_error error={0};
        exec_status status=exec_find_export(engine,names[i],&function,&error);
        if (status==EXEC_OK) status=exec_invoke(engine,function,args,2,result,&count,&error);
        int match = result[0].type == types[i];
        if (i==0) match = match && result[0].i64 == INT64_C(0x1122334455667788);
        if (i==1) match = match && result[0].f32 == -3.5f;
        if (i==2) match = match && result[0].f64 == 9.25;
        if (status!=EXEC_OK || count!=1 || !match) { fprintf(stderr,"%s failed: %s\n",names[i],error.message); return 0; }
    }
    return 1;
}

static exec_status host_add(void *data, const wasm_value *args, int count,
                            wasm_value *results, int *result_count, exec_error *error) {
    (void)data; (void)error;
    if (count != 2) return EXEC_ERROR_TRAP;
    results[0].type=WASM_VALTYPE_I32;
    results[0].i32=(int32_t)((uint32_t)args[0].i32 + (uint32_t)args[1].i32);
    *result_count=1; return EXEC_OK;
}

static int test_imports(const char *path) {
    FILE *file=fopen(path,"rb"); if(!file) return 0;
    fseek(file,0,SEEK_END); long length=ftell(file); rewind(file);
    uint8_t *bytes=malloc((size_t)length); if(!bytes) return 0;
    if(fread(bytes,1,(size_t)length,file)!=(size_t)length) {
        free(bytes); fclose(file); return 0;
    }
    fclose(file);
    exec_host_import binding={"host","add",host_add,NULL,NULL,0,0};
    exec_imports imports={.functions=&binding,.function_count=1};
    waste_exec_engine *engine=NULL; exec_error error={0};
    exec_status status=exec_load_with_imports(bytes,(size_t)length,&imports,&engine,&error); free(bytes);
    if(status!=EXEC_OK) { fprintf(stderr,"import load: %s\n",error.message); return 0; }
    int ok=run(engine,"direct",20,22,42,EXEC_OK) && run(engine,"wrapped",19,23,42,EXEC_OK);
    exec_free(engine); return ok;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file=fopen(path,"rb"); if(!file) return NULL;
    if(fseek(file,0,SEEK_END)!=0) { fclose(file); return NULL; }
    long length=ftell(file); if(length<0) { fclose(file); return NULL; } rewind(file);
    uint8_t *bytes=malloc((size_t)length); if(!bytes) { fclose(file); return NULL; }
    if(fread(bytes,1,(size_t)length,file)!=(size_t)length) { free(bytes); fclose(file); return NULL; }
    fclose(file); *size=(size_t)length; return bytes;
}

static int test_extern_aliases(const char *provider_path, const char *consumer_path) {
    size_t size; uint8_t *bytes=read_file(provider_path,&size); exec_error error={0};
    waste_exec_engine *provider=NULL,*consumer=NULL;
    if(!bytes || exec_load(bytes,size,&provider,&error)!=EXEC_OK) { free(bytes); fprintf(stderr,"provider load: %s\n",error.message); return 0; }
    free(bytes);
    exec_global *global=NULL; exec_memory *memory=NULL; exec_table *table=NULL;
    if(exec_find_export_global(provider,"global",&global,&error)!=EXEC_OK ||
       exec_find_export_memory(provider,"memory",&memory,&error)!=EXEC_OK ||
       exec_find_export_table(provider,"table",&table,&error)!=EXEC_OK) {
        fprintf(stderr,"provider export: %s\n",error.message); exec_free(provider); return 0;
    }
    exec_global_import gi={"provider","global",global};
    exec_memory_import mi={"provider","memory",memory};
    exec_table_import ti={"provider","table",table};
    exec_imports imports={.globals=&gi,.global_count=1,.memories=&mi,.memory_count=1,.tables=&ti,.table_count=1};
    bytes=read_file(consumer_path,&size);
    if(!bytes || exec_load_with_imports(bytes,size,&imports,&consumer,&error)!=EXEC_OK) {
        free(bytes); fprintf(stderr,"consumer load: %s\n",error.message); exec_free(provider); return 0;
    }
    free(bytes);
    exec_global *global2=NULL; exec_memory *memory2=NULL; exec_table *table2=NULL;
    int ok=exec_find_export_global(consumer,"global",&global2,&error)==EXEC_OK && global2==global &&
           exec_find_export_memory(consumer,"memory",&memory2,&error)==EXEC_OK && memory2==memory &&
           exec_find_export_table(consumer,"table",&table2,&error)==EXEC_OK && table2==table &&
           run(consumer,"set-global",73,0,73,EXEC_OK) && run(provider,"read-global",0,0,73,EXEC_OK) &&
           run(consumer,"store",24,0x12345678,0x12345678,EXEC_OK) &&
           run(provider,"read-memory",24,0,0x12345678,EXEC_OK) &&
           run(consumer,"grow",1,0,1,EXEC_OK) && memory->pages==2;
    exec_free(consumer);
    if(!ok) fprintf(stderr,"extern alias test failed: %s\n",error.message);
    exec_free(provider); return ok;
}

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) return 2;
    fclose(file);
    waste_exec_engine *engine = NULL; exec_error error = {0};
    exec_status status = exec_load(bytes, (size_t)length, &engine, &error); free(bytes);
    if (status != EXEC_OK) { fprintf(stderr, "load: %s\n", error.message); return 1; }
    int ok = run(engine, "add", INT32_MAX, 1, INT32_MIN, EXEC_OK) &&
             run(engine, "rotl", 1, 31, INT32_MIN, EXEC_OK) &&
             run(engine, "locals", 11, 22, 22, EXEC_OK) &&
             run(engine, "choose", 7, 9, 7, EXEC_OK) &&
             run(engine, "call", 20, 22, 42, EXEC_OK) &&
             run(engine, "early", 7, 9, 7, EXEC_OK) &&
             run(engine, "ifelse", 1, 9, 1, EXEC_OK) &&
             run(engine, "ifelse", 0, 9, 9, EXEC_OK) &&
             run(engine, "branch", 7, 9, 7, EXEC_OK) &&
             run(engine, "branch-if", 7, 1, 7, EXEC_OK) &&
             run(engine, "branch-if", 7, 0, 0, EXEC_OK) &&
             run(engine, "countdown", 5, 0, 0, EXEC_OK) &&
             run(engine, "branch-table", 33, 0, 33, EXEC_OK) &&
             run(engine, "branch-table", 44, 9, 44, EXEC_OK) &&
             run(engine, "multi-call", 44, 9, 35, EXEC_OK) &&
             run(engine, "multi-block", 44, 9, 35, EXEC_OK) &&
             run_pair(engine) &&
             run_scalar_globals(engine) &&
             run(engine, "global-null", 0, 0, 1, EXEC_OK) &&
             run(engine, "load-data", 8, 0, 0x12345678, EXEC_OK) &&
             run(engine, "load8-s", 12, 0, -128, EXEC_OK) &&
             run(engine, "store-load", 16, 0x76543210, 0x76543210, EXEC_OK) &&
             run(engine, "global", 91, 0, 91, EXEC_OK) &&
             run(engine, "size", 0, 0, 1, EXEC_OK) &&
             run(engine, "grow", 1, 0, 1, EXEC_OK) &&
             run(engine, "size", 0, 0, 2, EXEC_OK) &&
             run(engine, "grow", 1, 0, -1, EXEC_OK) &&
             run(engine, "load-data", 131071, 0, 0, EXEC_ERROR_TRAP) &&
             run(engine, "div_s", INT32_MIN, -1, 0, EXEC_ERROR_TRAP) &&
             run(engine, "div_u", 1, 0, 0, EXEC_ERROR_TRAP);
    exec_free(engine);
    return ok && test_imports(argv[2]) && test_extern_aliases(argv[3],argv[4]) ? 0 : 1;
}
