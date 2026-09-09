#include "wast_encode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *data; size_t len, cap; int failed; } writer;
typedef struct {
    wasm_valtype params[WAST_MAX_PARAMS], results[WAST_MAX_RESULTS];
    int param_count, result_count;
    int is_func;
} func_sig;

static void reserve(writer *w, size_t n) {
    if (w->failed || n > (size_t)-1 - w->len) { w->failed=1; return; }
    size_t need=w->len+n, cap=w->cap?w->cap:256;
    if (need<=w->cap) return;
    while(cap<need) { if(cap>(size_t)-1/2){cap=need;break;} cap*=2; }
    uint8_t *p=(uint8_t*)realloc(w->data,cap);
    if(!p){w->failed=1;return;} w->data=p; w->cap=cap;
}
static void byte(writer *w,uint8_t v){reserve(w,1);if(!w->failed)w->data[w->len++]=v;}
static void bytes(writer *w,const void *p,size_t n){reserve(w,n);if(!w->failed&&n){memcpy(w->data+w->len,p,n);w->len+=n;}}
static void u32(writer *w,uint32_t v){do{uint8_t b=(uint8_t)(v&0x7f);v>>=7;if(v)b|=0x80;byte(w,b);}while(v);}
static void u64(writer *w,uint64_t v){do{uint8_t b=(uint8_t)(v&0x7f);v>>=7;if(v)b|=0x80;byte(w,b);}while(v);}
static void s33(writer *w,int64_t v){int more=1;while(more){uint8_t b=(uint8_t)(v&0x7f);v>>=7;more=!((v==0&&!(b&0x40))||(v==-1&&(b&0x40)));if(more)b|=0x80;byte(w,b);}}
static void name(writer *w,const char *s){size_t n=strlen(s);if(n>UINT32_MAX){w->failed=1;return;}u32(w,(uint32_t)n);bytes(w,s,n);}
static void section(writer *out,uint8_t id,writer *s){
    if(s->failed||s->len>UINT32_MAX)out->failed=1;
    else if(s->len){byte(out,id);u32(out,(uint32_t)s->len);bytes(out,s->data,s->len);}
    free(s->data);memset(s,0,sizeof(*s));
}
static uint8_t vt(wasm_valtype t){
    switch(t){case WASM_VALTYPE_I32:return 0x7f;case WASM_VALTYPE_I64:return 0x7e;
    case WASM_VALTYPE_F32:return 0x7d;case WASM_VALTYPE_F64:return 0x7c;
    case WASM_VALTYPE_V128:return 0x7b;case WASM_VALTYPE_FUNCREF:return 0x70;
    case WASM_VALTYPE_EXTERNREF:return 0x6f;
    case WASM_VALTYPE_ANYREF:return 0x6e;case WASM_VALTYPE_EQREF:return 0x6d;
    case WASM_VALTYPE_I31REF:return 0x6c;case WASM_VALTYPE_STRUCTREF:return 0x6b;
    case WASM_VALTYPE_ARRAYREF:return 0x6a;
    case WASM_VALTYPE_NULLREF:return 0x71;case WASM_VALTYPE_NULLFUNCREF:return 0x73;
    case WASM_VALTYPE_EXNREF:return 0x69;
    case WASM_VALTYPE_NULLEXNREF:return 0x74;case WASM_VALTYPE_NULLEXTERNREF:return 0x72;
    case WASM_VALTYPE_FUNCREF_NONNULL:case WASM_VALTYPE_EXTERNREF_NONNULL:
    case WASM_VALTYPE_ANYREF_NONNULL:case WASM_VALTYPE_EQREF_NONNULL:
    case WASM_VALTYPE_I31REF_NONNULL:case WASM_VALTYPE_STRUCTREF_NONNULL:
    case WASM_VALTYPE_ARRAYREF_NONNULL:case WASM_VALTYPE_EXNREF_NONNULL:return 0;}return 0;
}
static void put_vt(writer *w,wasm_valtype t){
    if(t==WASM_VALTYPE_FUNCREF_NONNULL){byte(w,0x64);s33(w,-16);}
    else if(t==WASM_VALTYPE_EXTERNREF_NONNULL){byte(w,0x64);s33(w,-17);}
    else if(t>=WASM_VALTYPE_ANYREF_NONNULL&&t<=WASM_VALTYPE_ARRAYREF_NONNULL){
        static const int8_t heap[]={-18,-19,-20,-21,-22};
        byte(w,0x64);s33(w,heap[(int)t-(int)WASM_VALTYPE_ANYREF_NONNULL]);
    }
    else if(t==WASM_VALTYPE_EXNREF_NONNULL){byte(w,0x64);s33(w,-23);}
    else if(WASM_VALTYPE_IS_TYPE_REF(t)){byte(w,(unsigned)t<WASM_VALTYPE_TYPE_REF_BASE?0x63:0x64);s33(w,(int64_t)WASM_VALTYPE_TYPE_REF_INDEX(t));}
    else byte(w,vt(t));
}
static int sig_eq(const func_sig*a,const func_sig*b){
    return a->is_func&&b->is_func&&a->param_count==b->param_count&&a->result_count==b->result_count&&
      !memcmp(a->params,b->params,(size_t)a->param_count*sizeof(a->params[0]))&&
      !memcmp(a->results,b->results,(size_t)a->result_count*sizeof(a->results[0]));
}
static func_sig type_sig(const wast_type*t){func_sig s={0};s.is_func=t->kind==WAST_TYPE_FUNC;s.param_count=t->param_count;s.result_count=t->result_count;
    memcpy(s.params,t->params,(size_t)s.param_count*sizeof(s.params[0]));memcpy(s.results,t->results,(size_t)s.result_count*sizeof(s.results[0]));return s;}
static func_sig func_sig_of(const wast_func*f){func_sig s={0};s.is_func=1;s.param_count=f->param_count;s.result_count=f->result_count;
    memcpy(s.params,f->params,(size_t)s.param_count*sizeof(s.params[0]));memcpy(s.results,f->results,(size_t)s.result_count*sizeof(s.results[0]));return s;}
static void put_sig(writer*w,const func_sig*s){byte(w,0x60);u32(w,(uint32_t)s->param_count);
    for(int i=0;i<s->param_count;i++)put_vt(w,s->params[i]);
    u32(w,(uint32_t)s->result_count);
    for(int i=0;i<s->result_count;i++)put_vt(w,s->results[i]);}
static void put_field(writer*w,const wast_type*t,int i){
    if(t->field_packed[i]==1)byte(w,0x78);
    else if(t->field_packed[i]==2)byte(w,0x77);
    else put_vt(w,t->fields[i]);
    byte(w,t->field_mutable[i]?1:0);
}
static void put_composite_type(writer*w,const wast_type*t){
    if(t->kind==WAST_TYPE_FUNC){func_sig s=type_sig(t);put_sig(w,&s);return;}
    if(t->kind==WAST_TYPE_STRUCT){
        byte(w,0x5f);u32(w,(uint32_t)t->field_count);
        for(int i=0;i<t->field_count;i++)put_field(w,t,i);
        return;
    }
    byte(w,0x5e);put_field(w,t,0);
}
static void put_type(writer*w,const wast_type*t){
    if(t->is_final&&t->supertype<0){put_composite_type(w,t);return;}
    byte(w,t->is_final?0x4f:0x50);
    u32(w,t->supertype>=0?1u:0u);
    if(t->supertype>=0)u32(w,(uint32_t)t->supertype);
    put_composite_type(w,t);
}
static void limits(writer*w,const wast_limits*l){uint32_t f=(l->has_max?1u:0u)|(l->is_shared?2u:0u)|(l->is_64?4u:0u);u32(w,f);u64(w,l->min);if(l->has_max)u64(w,l->max);}
static void table_type(writer*w,const wast_table*t){put_vt(w,t->reftype);limits(w,&t->limits);}
static void global_type(writer*w,const wast_global*g){put_vt(w,g->valtype);byte(w,g->is_mutable?1:0);}
static void export_(writer*w,const char*n,uint8_t k,uint32_t i){name(w,n);byte(w,k);u32(w,i);}
static void put_heap_type(writer*w,wasm_valtype t){
    int32_t heap;
    if(WASM_VALTYPE_IS_TYPE_REF(t)){s33(w,(int32_t)WASM_VALTYPE_TYPE_REF_INDEX(t));return;}
    switch(t){
    case WASM_VALTYPE_FUNCREF:case WASM_VALTYPE_FUNCREF_NONNULL:heap=-16;break;
    case WASM_VALTYPE_EXTERNREF:case WASM_VALTYPE_EXTERNREF_NONNULL:heap=-17;break;
    case WASM_VALTYPE_ANYREF:case WASM_VALTYPE_ANYREF_NONNULL:heap=-18;break;
    case WASM_VALTYPE_EQREF:case WASM_VALTYPE_EQREF_NONNULL:heap=-19;break;
    case WASM_VALTYPE_I31REF:case WASM_VALTYPE_I31REF_NONNULL:heap=-20;break;
    case WASM_VALTYPE_STRUCTREF:case WASM_VALTYPE_STRUCTREF_NONNULL:heap=-21;break;
    case WASM_VALTYPE_ARRAYREF:case WASM_VALTYPE_ARRAYREF_NONNULL:heap=-22;break;
    case WASM_VALTYPE_EXNREF:case WASM_VALTYPE_EXNREF_NONNULL:heap=-23;break;
    case WASM_VALTYPE_NULLREF:heap=-15;break;
    case WASM_VALTYPE_NULLEXTERNREF:heap=-14;break;
    case WASM_VALTYPE_NULLFUNCREF:heap=-13;break;
    case WASM_VALTYPE_NULLEXNREF:heap=-12;break;
    default:heap=-16;break;
    }
    s33(w,heap);
}
static void elem_expr(writer*w,wasm_valtype t,uint8_t opcode,uint32_t ref){
    if(opcode==0x23){byte(w,0x23);u32(w,ref);}
    else if(opcode==0xd0||ref==UINT32_MAX){byte(w,0xd0);put_heap_type(w,t);}
    else{byte(w,0xd2);u32(w,ref);}
    byte(w,0x0b);
}

uint8_t *wast_encode_module(const wast_module *m,size_t *size_out,char *error){
    writer out={0},s={0};func_sig *sigs=NULL;uint32_t *ft=NULL,*tt=NULL;
    if(!m||!size_out)return NULL;
    *size_out=0;
    int sig_cap=m->type_count+m->func_count+m->tag_count,sig_count=m->type_count;
    if(sig_cap){sigs=(func_sig*)calloc((size_t)sig_cap,sizeof(*sigs));ft=(uint32_t*)calloc((size_t)m->func_count,sizeof(*ft));tt=(uint32_t*)calloc((size_t)m->tag_count,sizeof(*tt));
        if(!sigs||(m->func_count&&!ft)||(m->tag_count&&!tt))goto oom;}
    for(int i=0;i<m->type_count;i++)sigs[i]=type_sig(&m->types[i]);
    for(int i=0;i<m->func_count;i++){const wast_func*f=&m->funcs[i];
        if(f->type_index>=0){if(f->type_index>=sig_count||(f->type_index<m->type_count&&m->types[f->type_index].kind!=WAST_TYPE_FUNC)){if(error)snprintf(error,256,"function %d has invalid function type index %d",i,f->type_index);goto fail;}
            const func_sig*t=&sigs[f->type_index];
            if((f->has_inline_params||f->has_inline_results)&&
               (f->param_count!=t->param_count||f->result_count!=t->result_count||
                memcmp(f->params,t->params,(size_t)f->param_count*sizeof(f->params[0]))||
                memcmp(f->results,t->results,(size_t)f->result_count*sizeof(f->results[0])))){
                if(error)snprintf(error,256,"function %d type use does not match inline signature",i);
                goto fail;
            }
            ft[i]=(uint32_t)f->type_index;}
        else{func_sig fs=func_sig_of(f);int found=-1;for(int j=0;j<sig_count;j++)if((j>=m->type_count||m->types[j].rec_group_size<=1)&&sig_eq(&sigs[j],&fs)){found=j;break;}
            if(found<0){found=sig_count;sigs[sig_count++]=fs;}ft[i]=(uint32_t)found;}}
    for(int i=0;i<m->tag_count;i++){
        const wast_tag*t=&m->tags[i];
        if(t->type_index>=0){
            if(t->type_index>=m->type_count||m->types[t->type_index].kind!=WAST_TYPE_FUNC||
               m->types[t->type_index].result_count!=0){if(error)snprintf(error,256,"tag %d has invalid function type",i);goto fail;}
            const func_sig*declared=&sigs[t->type_index];
            if(t->has_inline_params&&
               (t->param_count!=declared->param_count||
                memcmp(t->params,declared->params,(size_t)t->param_count*sizeof(t->params[0])))){
                if(error)snprintf(error,256,"tag %d type use does not match inline signature",i);
                goto fail;
            }
            tt[i]=(uint32_t)t->type_index;
        }else{
            func_sig ts={0};int found=-1;ts.is_func=1;
            ts.param_count=t->param_count;
            memcpy(ts.params,t->params,(size_t)ts.param_count*sizeof(ts.params[0]));
            for(int j=0;j<sig_count;j++)if((j>=m->type_count||m->types[j].rec_group_size<=1)&&sig_eq(&sigs[j],&ts)){found=j;break;}
            if(found<0){found=sig_count;sigs[sig_count++]=ts;}tt[i]=(uint32_t)found;
        }
    }
    bytes(&out,"\0asm\1\0\0\0",8);

    if(sig_count){
        uint32_t entries=0;
        for(int i=0;i<sig_count;){
            uint32_t group=(i<m->type_count&&m->types[i].rec_group_size>1&&
                            m->types[i].rec_group_start==(uint32_t)i)?
                           m->types[i].rec_group_size:1u;
            entries++;i+=(int)group;
        }
        u32(&s,entries);
        for(int i=0;i<sig_count;){
            uint32_t group=(i<m->type_count&&m->types[i].rec_group_size>1&&
                            m->types[i].rec_group_start==(uint32_t)i)?
                           m->types[i].rec_group_size:1u;
            if(group>1){byte(&s,0x4e);u32(&s,group);for(uint32_t j=0;j<group;j++)put_type(&s,&m->types[i+(int)j]);}
            else if(i<m->type_count)put_type(&s,&m->types[i]);
            else put_sig(&s,&sigs[i]);
            i+=(int)group;
        }
        section(&out,1,&s);
    }

    uint32_t imports=0;for(int i=0;i<m->func_count;i++)imports+=m->funcs[i].is_import!=0;
    for(int i=0;i<m->table_count;i++)imports+=m->tables[i].is_import!=0;
    for(int i=0;i<m->memory_count;i++)imports+=m->memories[i].is_import!=0;
    for(int i=0;i<m->global_count;i++)imports+=m->globals[i].is_import!=0;
    for(int i=0;i<m->tag_count;i++)imports+=m->tags[i].is_import!=0;
    if(imports){u32(&s,imports);
        for(int i=0;i<m->func_count;i++)if(m->funcs[i].is_import){const wast_func*f=&m->funcs[i];name(&s,f->import_module);name(&s,f->import_name);byte(&s,0);u32(&s,ft[i]);}
        for(int i=0;i<m->table_count;i++)if(m->tables[i].is_import){const wast_table*t=&m->tables[i];name(&s,t->import_module);name(&s,t->import_name);byte(&s,1);table_type(&s,t);}
        for(int i=0;i<m->memory_count;i++)if(m->memories[i].is_import){const wast_memory*x=&m->memories[i];name(&s,x->import_module);name(&s,x->import_name);byte(&s,2);limits(&s,&x->limits);}
        for(int i=0;i<m->global_count;i++)if(m->globals[i].is_import){const wast_global*g=&m->globals[i];name(&s,g->import_module);name(&s,g->import_name);byte(&s,3);global_type(&s,g);}
        for(int i=0;i<m->tag_count;i++)if(m->tags[i].is_import){const wast_tag*t=&m->tags[i];name(&s,t->import_module);name(&s,t->import_name);byte(&s,4);u32(&s,0);u32(&s,tt[i]);}
        section(&out,2,&s);}

    uint32_t defs=0;for(int i=0;i<m->func_count;i++)defs+=!m->funcs[i].is_import;
    if(defs){u32(&s,defs);for(int i=0;i<m->func_count;i++)if(!m->funcs[i].is_import)u32(&s,ft[i]);section(&out,3,&s);}
    uint32_t n=0;for(int i=0;i<m->table_count;i++)n+=!m->tables[i].is_import;
    if(n){u32(&s,n);for(int i=0;i<m->table_count;i++)if(!m->tables[i].is_import){const wast_table*t=&m->tables[i];
            if(t->has_explicit_init){byte(&s,0x40);byte(&s,0x00);table_type(&s,t);bytes(&s,t->init_expr,(size_t)t->init_len);byte(&s,0x0b);}
            else table_type(&s,t);
        }section(&out,4,&s);}
    for(int i=0;i<m->memory_count;i++){const wast_limits*ml=&m->memories[i].limits;
        const uint64_t page_limit=ml->is_64?(UINT64_C(1)<<48):UINT64_C(65536);
        if(ml->min>page_limit||(ml->has_max&&ml->max>page_limit)){if(error)snprintf(error,256,"memory size exceeds address type");goto fail;}}
    n=0;for(int i=0;i<m->memory_count;i++)n+=!m->memories[i].is_import;
    if(n){u32(&s,n);for(int i=0;i<m->memory_count;i++)if(!m->memories[i].is_import)limits(&s,&m->memories[i].limits);section(&out,5,&s);}
    n=0;for(int i=0;i<m->tag_count;i++)n+=!m->tags[i].is_import;
    if(n){u32(&s,n);for(int i=0;i<m->tag_count;i++)if(!m->tags[i].is_import){u32(&s,0);u32(&s,tt[i]);}section(&out,13,&s);}
    n=0;for(int i=0;i<m->global_count;i++)n+=!m->globals[i].is_import;
    if(n){u32(&s,n);for(int i=0;i<m->global_count;i++)if(!m->globals[i].is_import){const wast_global*g=&m->globals[i];global_type(&s,g);bytes(&s,g->init_expr,(size_t)g->init_len);byte(&s,0x0b);}section(&out,6,&s);}

    uint32_t exports=0;for(int i=0;i<m->export_count;i++)exports+=m->exports[i].kind<=4;
    for(int i=0;i<m->func_count;i++)exports+=m->funcs[i].has_export_name!=0;
    for(int i=0;i<m->table_count;i++)exports+=m->tables[i].has_export_name!=0;
    for(int i=0;i<m->memory_count;i++)exports+=m->memories[i].has_export_name!=0;
    for(int i=0;i<m->global_count;i++)exports+=m->globals[i].has_export_name!=0;
    if(exports){u32(&s,exports);
        for(int i=0;i<m->func_count;i++)if(m->funcs[i].has_export_name)export_(&s,m->funcs[i].export_name,0,(uint32_t)i);
        for(int i=0;i<m->table_count;i++)if(m->tables[i].has_export_name)export_(&s,m->tables[i].export_name,1,(uint32_t)i);
        for(int i=0;i<m->memory_count;i++)if(m->memories[i].has_export_name)export_(&s,m->memories[i].export_name,2,(uint32_t)i);
        for(int i=0;i<m->global_count;i++)if(m->globals[i].has_export_name)export_(&s,m->globals[i].export_name,3,(uint32_t)i);
        for(int i=0;i<m->export_count;i++)if(m->exports[i].kind<=4)export_(&s,m->exports[i].name,(uint8_t)m->exports[i].kind,m->exports[i].index);
        section(&out,7,&s);}
    if(m->start_func>=0){u32(&s,(uint32_t)m->start_func);section(&out,8,&s);}

    if(m->elem_count){u32(&s,(uint32_t)m->elem_count);for(int i=0;i<m->elem_count;i++){const wast_elem_seg*e=&m->elem[i];
        uint32_t mode=e->is_declarative?7u:e->is_passive?5u:
            (e->table_index || e->reftype != WASM_VALTYPE_FUNCREF)?6u:4u;
        u32(&s,mode);
        if(mode==6)u32(&s,(uint32_t)e->table_index);
        if(mode==4||mode==6)bytes(&s,e->offset_expr,(size_t)e->offset_len);
        if(mode!=4)put_vt(&s,e->reftype);
        u32(&s,(uint32_t)e->ref_count);
        for(int j=0;j<e->ref_count;j++){
            if(e->ref_expr_lens[j]>0)bytes(&s,e->ref_exprs[j],(size_t)e->ref_expr_lens[j]);
            else elem_expr(&s,e->ref_opcodes[j]==0xd0?e->ref_types[j]:e->reftype,
                           e->ref_opcodes[j],e->refs[j]);
        }}section(&out,9,&s);}

    /* The DataCount section precedes code and is mandatory when a function
     * uses memory.init or data.drop.  The text representation deliberately
     * keeps function bodies as raw bytes, so emit the (always valid) section
     * for every encoded module instead of rescanning instruction streams. */
    u32(&s,(uint32_t)m->data_count);
    section(&out,12,&s);

    if(defs){u32(&s,defs);for(int i=0;i<m->func_count;i++)if(!m->funcs[i].is_import){const wast_func*f=&m->funcs[i];writer body={0};
        u32(&body,(uint32_t)f->local_count);for(int j=0;j<f->local_count;j++){u32(&body,1);put_vt(&body,f->locals[j]);}
        bytes(&body,f->code,(size_t)f->code_len);if(body.failed||body.len>UINT32_MAX){s.failed=1;free(body.data);break;}
        u32(&s,(uint32_t)body.len);bytes(&s,body.data,body.len);free(body.data);}section(&out,10,&s);}
    if(m->data_count){u32(&s,(uint32_t)m->data_count);for(int i=0;i<m->data_count;i++){const wast_data_seg*d=&m->data[i];
        uint32_t mode=d->is_passive?1u:d->memory_index?2u:0u;u32(&s,mode);if(mode==2)u32(&s,(uint32_t)d->memory_index);
        if(mode!=1)bytes(&s,d->offset_expr,(size_t)d->offset_len);
        u32(&s,(uint32_t)d->len);bytes(&s,d->bytes,(size_t)d->len);}section(&out,11,&s);}
    free(sigs);free(ft);free(tt);if(out.failed)goto oom_out;*size_out=out.len;return out.data;
oom:if(error)snprintf(error,256,"allocation failed while preparing module");
fail:free(sigs);free(ft);free(tt);free(out.data);free(s.data);return NULL;
oom_out:if(error)snprintf(error,256,"allocation failed while encoding module");free(out.data);return NULL;
}
