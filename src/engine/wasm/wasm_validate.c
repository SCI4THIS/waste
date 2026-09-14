#include "wasm_validate.h"
#include "wasm/wasm_opcode.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int value_type_is_defined(const waste_exec_engine *eng,
                                 wasm_valtype type) {
    return !WASM_VALTYPE_IS_TYPE_REF(type) ||
           WASM_VALTYPE_TYPE_REF_INDEX(type) < eng->type_count;
}

typedef struct {
    const waste_exec_engine *left_engine;
    const waste_exec_engine *right_engine;
    uint32_t left_group;
    uint32_t right_group;
} exec_type_pair;

typedef struct {
    exec_type_pair pairs[WAST_MAX_TYPES];
    uint32_t count;
} exec_type_compare;

static int same_value_type_ctx(const waste_exec_engine *left_engine,
                               wasm_valtype left,
                               const waste_exec_engine *right_engine,
                               wasm_valtype right,
                               exec_type_compare *compare);

static int same_type_index_ctx(const waste_exec_engine *left_engine,
                               uint32_t left_index,
                               const waste_exec_engine *right_engine,
                               uint32_t right_index,
                               exec_type_compare *compare) {
    if (!left_engine || !right_engine ||
        left_index >= left_engine->type_count ||
        right_index >= right_engine->type_count)
        return 0;
    const exec_func_type *left = &left_engine->types[left_index];
    const exec_func_type *right = &right_engine->types[right_index];
    uint32_t left_start = left->rec_group_start;
    uint32_t right_start = right->rec_group_start;
    if (!left->rec_group_size || !right->rec_group_size ||
        left_start + left->rec_group_size > left_engine->type_count ||
        right_start + right->rec_group_size > right_engine->type_count ||
        left->rec_group_size != right->rec_group_size ||
        left_index - left_start != right_index - right_start)
        return 0;

    for (uint32_t i = 0; i < compare->count; i++) {
        const exec_type_pair *p = &compare->pairs[i];
        if (p->left_engine == left_engine && p->right_engine == right_engine &&
            p->left_group == left_start && p->right_group == right_start)
            return 1;
        /* Structural equivalence is symmetric */
        if (p->left_engine == right_engine && p->right_engine == left_engine &&
            p->left_group == right_start && p->right_group == left_start)
            return 1;
    }
    if (compare->count >= WAST_MAX_TYPES) return 0;
    compare->pairs[compare->count].left_engine = left_engine;
    compare->pairs[compare->count].right_engine = right_engine;
    compare->pairs[compare->count].left_group = left_start;
    compare->pairs[compare->count].right_group = right_start;
    compare->count++;

    int equivalent = 1;
    for (uint32_t member = 0; member < left->rec_group_size; member++) {
        const exec_func_type *a = &left_engine->types[left_start + member];
        const exec_func_type *b = &right_engine->types[right_start + member];
        if (a->kind != b->kind || a->is_final != b->is_final ||
            ((a->supertype >= 0) != (b->supertype >= 0))) {
            equivalent = 0; break;
        }
        if (a->supertype >= 0 &&
            !same_value_type_ctx(left_engine,
                (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE+
                               (uint32_t)a->supertype),
                right_engine,
                (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE+
                               (uint32_t)b->supertype),
                compare))
            { equivalent = 0; break; }
        if (a->kind == WAST_TYPE_FUNC) {
            if (a->param_count != b->param_count ||
                a->result_count != b->result_count) {
                equivalent = 0; break;
            }
            for (int i = 0; i < a->param_count; i++)
                if (!same_value_type_ctx(left_engine, a->params[i],
                                         right_engine, b->params[i], compare)) {
                    equivalent = 0; break;
                }
            if (!equivalent) break;
            for (int i = 0; i < a->result_count; i++)
                if (!same_value_type_ctx(left_engine, a->results[i],
                                         right_engine, b->results[i], compare)) {
                    equivalent = 0; break;
                }
            if (!equivalent) break;
        } else {
            if (a->field_count != b->field_count) {
                equivalent = 0; break;
            }
            for (int i = 0; i < a->field_count; i++)
                if (a->field_mutable[i] != b->field_mutable[i] ||
                    a->field_packed[i] != b->field_packed[i] ||
                    !same_value_type_ctx(left_engine, a->fields[i],
                                         right_engine, b->fields[i], compare)) {
                    equivalent = 0; break;
                }
            if (!equivalent) break;
        }
    }
    compare->count--;
    return equivalent;
}

static int same_value_type_ctx(const waste_exec_engine *left_engine,
                               wasm_valtype left,
                               const waste_exec_engine *right_engine,
                               wasm_valtype right,
                               exec_type_compare *compare) {
    int left_indexed = WASM_VALTYPE_IS_TYPE_REF(left);
    int right_indexed = WASM_VALTYPE_IS_TYPE_REF(right);
    if (!left_indexed || !right_indexed) return left == right;
    if (((unsigned)left < WASM_VALTYPE_TYPE_REF_BASE) !=
        ((unsigned)right < WASM_VALTYPE_TYPE_REF_BASE))
        return 0;
    uint32_t li = WASM_VALTYPE_TYPE_REF_INDEX(left);
    uint32_t ri = WASM_VALTYPE_TYPE_REF_INDEX(right);
    /* When comparing inside a rec group, check whether each type ref is
     * intra-group (referencing a member of a group currently being compared)
     * or extra-group.  Intra-group refs must match by relative position;
     * mixed intra/extra is a mismatch. */
    for (uint32_t p = 0; p < compare->count; p++) {
        const exec_type_pair *pair = &compare->pairs[p];
        int l_intra = (left_engine == pair->left_engine &&
                       li >= pair->left_group &&
                       li < pair->left_group +
                            pair->left_engine->types[pair->left_group].rec_group_size);
        int r_intra = (right_engine == pair->right_engine &&
                       ri >= pair->right_group &&
                       ri < pair->right_group +
                            pair->right_engine->types[pair->right_group].rec_group_size);
        if (l_intra || r_intra) {
            if (l_intra != r_intra) return 0;
            return (li - pair->left_group) == (ri - pair->right_group);
        }
    }
    if (left_engine == right_engine && li == ri) return 1;
    return same_type_index_ctx(left_engine, li, right_engine, ri, compare);
}

int same_value_type(const waste_exec_engine *left_engine, wasm_valtype left,
                           const waste_exec_engine *right_engine, wasm_valtype right,
                           unsigned depth) {
    exec_type_compare compare = {0};
    (void)depth;
    return same_value_type_ctx(left_engine, left, right_engine, right, &compare);
}

int same_func_type(const waste_exec_engine *left_engine, uint32_t left_index,
                          const waste_exec_engine *right_engine, uint32_t right_index) {
    exec_type_compare compare = {0};
    if (!left_engine || !right_engine ||
        left_index >= left_engine->type_count ||
        right_index >= right_engine->type_count ||
        left_engine->types[left_index].kind != WAST_TYPE_FUNC ||
        right_engine->types[right_index].kind != WAST_TYPE_FUNC)
        return 0;
    return same_type_index_ctx(left_engine, left_index, right_engine, right_index,
                               &compare);
}

int type_index_is_subtype(const waste_exec_engine *actual_engine,
                                 uint32_t actual_index,
                                 const waste_exec_engine *required_engine,
                                 uint32_t required_index) {
    if (!actual_engine || !required_engine ||
        actual_index >= actual_engine->type_count ||
        required_index >= required_engine->type_count)
        return 0;
    uint32_t current = actual_index;
    for (uint32_t steps = 0; steps <= actual_engine->type_count; steps++) {
        exec_type_compare compare = {0};
        if (same_type_index_ctx(actual_engine, current, required_engine,
                                required_index, &compare)) {
            return 1;
        }
        int32_t parent = actual_engine->types[current].supertype;
        if (parent < 0 || (uint32_t)parent >= actual_engine->type_count ||
            (uint32_t)parent == current)
            break;
        current = (uint32_t)parent;
    }
    return 0;
}

int func_type_is_subtype(const waste_exec_engine *actual_engine,
                                uint32_t actual_index,
                                const waste_exec_engine *required_engine,
                                uint32_t required_index) {
    return actual_index < actual_engine->type_count &&
           required_index < required_engine->type_count &&
           actual_engine->types[actual_index].kind == WAST_TYPE_FUNC &&
           required_engine->types[required_index].kind == WAST_TYPE_FUNC &&
           type_index_is_subtype(actual_engine, actual_index,
                                 required_engine, required_index);
}

/* Check if 'actual' value type is a subtype of 'required' value type.
 * For mutable globals, use same_value_type (exact structural equality).
 * For immutable globals, applies Wasm GC subtype rules:
 *   FUNCREF_NONNULL    <: FUNCREF
 *   EXTERNREF_NONNULL  <: EXTERNREF
 *   (ref null T)       <: (ref null func)    for any func heap type T
 *   (ref T)            <: (ref null func)    for any func heap type T
 *   (ref T)            <: (ref func)         for any func heap type T
 *   (ref T)            <: (ref null T)       (non-null subtype of nullable, same type)
 */
int global_type_is_compat(const waste_exec_engine *aeng, wasm_valtype actual,
                                  const waste_exec_engine *reng, wasm_valtype required,
                                  int mutable_) {
    if (mutable_) return same_value_type(aeng, actual, reng, required, 0);
    /* Structural equality covers exact-match cases */
    if (same_value_type(aeng, actual, reng, required, 0)) return 1;
    /* Non-null builtins <: nullable counterparts */
    if (actual == WASM_VALTYPE_FUNCREF_NONNULL && required == WASM_VALTYPE_FUNCREF) return 1;
    if (actual == WASM_VALTYPE_EXTERNREF_NONNULL && required == WASM_VALTYPE_EXTERNREF) return 1;
    if (actual == WASM_VALTYPE_ANYREF_NONNULL && required == WASM_VALTYPE_ANYREF) return 1;
    if (actual == WASM_VALTYPE_EQREF_NONNULL && required == WASM_VALTYPE_EQREF) return 1;
    if (actual == WASM_VALTYPE_I31REF_NONNULL && required == WASM_VALTYPE_I31REF) return 1;
    if (actual == WASM_VALTYPE_STRUCTREF_NONNULL && required == WASM_VALTYPE_STRUCTREF) return 1;
    if (actual == WASM_VALTYPE_ARRAYREF_NONNULL && required == WASM_VALTYPE_ARRAYREF) return 1;
    /* Bottom heap types are subtypes of the corresponding nullable reference
     * type.  Runtime assertion arguments retain these precise types so typed
     * select must accept them just as the validator does. */
    if (actual == WASM_VALTYPE_NULLFUNCREF && required == WASM_VALTYPE_FUNCREF) return 1;
    if (actual == WASM_VALTYPE_NULLFUNCREF && WASM_VALTYPE_IS_TYPE_REF(required))
        return reng && WASM_VALTYPE_TYPE_REF_INDEX(required) < reng->type_count;
    if (actual == WASM_VALTYPE_NULLEXTERNREF &&
        required == WASM_VALTYPE_EXTERNREF) return 1;
    if (actual == WASM_VALTYPE_NULLEXNREF &&
        required == WASM_VALTYPE_EXNREF) return 1;
    if (actual == WASM_VALTYPE_EXNREF_NONNULL &&
        required == WASM_VALTYPE_EXNREF) return 1;
    if (actual == WASM_VALTYPE_NULLREF &&
        (required == WASM_VALTYPE_ANYREF || required == WASM_VALTYPE_EQREF ||
         required == WASM_VALTYPE_I31REF || required == WASM_VALTYPE_STRUCTREF ||
         required == WASM_VALTYPE_ARRAYREF)) return 1;
    int a_isref = WASM_VALTYPE_IS_TYPE_REF(actual);
    int r_isref = WASM_VALTYPE_IS_TYPE_REF(required);
    if (a_isref && r_isref && aeng && reng) {
        int actual_nonnull = (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE;
        int required_nonnull = (unsigned)required >= WASM_VALTYPE_TYPE_REF_BASE;
        if (!required_nonnull || actual_nonnull) {
            uint32_t current = WASM_VALTYPE_TYPE_REF_INDEX(actual);
            uint32_t target = WASM_VALTYPE_TYPE_REF_INDEX(required);
            if (type_index_is_subtype(aeng, current, reng, target)) return 1;
        }
    }
    if (a_isref && aeng && WASM_VALTYPE_TYPE_REF_INDEX(actual) < aeng->type_count) {
        const exec_func_type *actual_heap =
            &aeng->types[WASM_VALTYPE_TYPE_REF_INDEX(actual)];
        int actual_nonnull =
            (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE;
        if (actual_heap->kind == WAST_TYPE_ARRAY &&
            (required == WASM_VALTYPE_ARRAYREF ||
             (required == WASM_VALTYPE_ARRAYREF_NONNULL && actual_nonnull)))
            return 1;
        if (actual_heap->kind == WAST_TYPE_STRUCT &&
            (required == WASM_VALTYPE_STRUCTREF ||
             (required == WASM_VALTYPE_STRUCTREF_NONNULL && actual_nonnull)))
            return 1;
        if ((actual_heap->kind == WAST_TYPE_ARRAY ||
             actual_heap->kind == WAST_TYPE_STRUCT) &&
            (required == WASM_VALTYPE_EQREF ||
             required == WASM_VALTYPE_ANYREF ||
             (required == WASM_VALTYPE_EQREF_NONNULL && actual_nonnull) ||
             (required == WASM_VALTYPE_ANYREF_NONNULL && actual_nonnull)))
            return 1;
    }
    if ((actual == WASM_VALTYPE_I31REF ||
         actual == WASM_VALTYPE_I31REF_NONNULL) &&
        (required == WASM_VALTYPE_EQREF || required == WASM_VALTYPE_ANYREF ||
         (actual == WASM_VALTYPE_I31REF_NONNULL &&
          (required == WASM_VALTYPE_I31REF_NONNULL ||
           required == WASM_VALTYPE_EQREF_NONNULL ||
           required == WASM_VALTYPE_ANYREF_NONNULL)))) return 1;
    if ((actual == WASM_VALTYPE_STRUCTREF ||
         actual == WASM_VALTYPE_STRUCTREF_NONNULL) &&
        (required == WASM_VALTYPE_EQREF || required == WASM_VALTYPE_ANYREF ||
         (actual == WASM_VALTYPE_STRUCTREF_NONNULL &&
          (required == WASM_VALTYPE_EQREF_NONNULL ||
           required == WASM_VALTYPE_ANYREF_NONNULL)))) return 1;
    if ((actual == WASM_VALTYPE_ARRAYREF ||
         actual == WASM_VALTYPE_ARRAYREF_NONNULL) &&
        (required == WASM_VALTYPE_EQREF || required == WASM_VALTYPE_ANYREF ||
         (actual == WASM_VALTYPE_ARRAYREF_NONNULL &&
          (required == WASM_VALTYPE_EQREF_NONNULL ||
           required == WASM_VALTYPE_ANYREF_NONNULL)))) return 1;
    /* Any (ref null T) or (ref T) where T is a func type <: (ref null func) */
    if (a_isref && required == WASM_VALTYPE_FUNCREF) return 1;
    /* (ref T) <: (ref func) */
    if (a_isref && required == WASM_VALTYPE_FUNCREF_NONNULL)
        return (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE;
    /* (ref T) <: (ref null T) — non-null subtype of nullable, same structural type */
    if (a_isref && r_isref &&
        (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE &&
        (unsigned)required < WASM_VALTYPE_TYPE_REF_BASE) {
        wasm_valtype actual_nullable = (wasm_valtype)((unsigned)actual - 0x100u);
        return same_value_type(aeng, actual_nullable, reng, required, 0);
    }
    return 0;
}

int is_reference_type(wasm_valtype type) {
    return ((unsigned)type >= (unsigned)WASM_VALTYPE_FUNCREF &&
            (unsigned)type <= (unsigned)WASM_VALTYPE_NULLEXTERNREF) ||
           WASM_VALTYPE_IS_TYPE_REF(type);
}

int is_nullable_reference_type(wasm_valtype type) {
    return type == WASM_VALTYPE_FUNCREF ||
           type == WASM_VALTYPE_EXTERNREF ||
           type == WASM_VALTYPE_ANYREF ||
           type == WASM_VALTYPE_EQREF ||
           type == WASM_VALTYPE_I31REF ||
           type == WASM_VALTYPE_STRUCTREF ||
           type == WASM_VALTYPE_ARRAYREF ||
           type == WASM_VALTYPE_EXNREF ||
           type == WASM_VALTYPE_NULLREF ||
           type == WASM_VALTYPE_NULLFUNCREF ||
           type == WASM_VALTYPE_NULLEXNREF ||
           type == WASM_VALTYPE_NULLEXTERNREF ||
           (WASM_VALTYPE_IS_TYPE_REF(type) &&
            (unsigned)type < WASM_VALTYPE_TYPE_REF_BASE);
}

int is_function_reference_type(wasm_valtype type) {
    return type == WASM_VALTYPE_FUNCREF ||
           type == WASM_VALTYPE_FUNCREF_NONNULL ||
           type == WASM_VALTYPE_NULLFUNCREF ||
           WASM_VALTYPE_IS_TYPE_REF(type);
}

int is_eq_reference_type(const waste_exec_engine *eng,
                                wasm_valtype type) {
    if (type == WASM_VALTYPE_EQREF || type == WASM_VALTYPE_EQREF_NONNULL ||
        type == WASM_VALTYPE_I31REF || type == WASM_VALTYPE_I31REF_NONNULL ||
        type == WASM_VALTYPE_STRUCTREF || type == WASM_VALTYPE_STRUCTREF_NONNULL ||
        type == WASM_VALTYPE_ARRAYREF || type == WASM_VALTYPE_ARRAYREF_NONNULL ||
        type == WASM_VALTYPE_NULLREF)
        return 1;
    return WASM_VALTYPE_IS_TYPE_REF(type) && eng &&
           WASM_VALTYPE_TYPE_REF_INDEX(type) < eng->type_count &&
           eng->types[WASM_VALTYPE_TYPE_REF_INDEX(type)].kind != WAST_TYPE_FUNC;
}

wasm_valtype nonnullable_reference_type(wasm_valtype type) {
    if (type == WASM_VALTYPE_FUNCREF) return WASM_VALTYPE_FUNCREF_NONNULL;
    if (type == WASM_VALTYPE_EXTERNREF) return WASM_VALTYPE_EXTERNREF_NONNULL;
    if (type == WASM_VALTYPE_ANYREF) return WASM_VALTYPE_ANYREF_NONNULL;
    if (type == WASM_VALTYPE_EQREF) return WASM_VALTYPE_EQREF_NONNULL;
    if (type == WASM_VALTYPE_I31REF) return WASM_VALTYPE_I31REF_NONNULL;
    if (type == WASM_VALTYPE_STRUCTREF) return WASM_VALTYPE_STRUCTREF_NONNULL;
    if (type == WASM_VALTYPE_ARRAYREF) return WASM_VALTYPE_ARRAYREF_NONNULL;
    if (type == WASM_VALTYPE_EXNREF) return WASM_VALTYPE_EXNREF_NONNULL;
    if (WASM_VALTYPE_IS_TYPE_REF(type) &&
        (unsigned)type < WASM_VALTYPE_TYPE_REF_BASE)
        return (wasm_valtype)((unsigned)type + 0x100u);
    return type;
}

int validate_declared_subtype(waste_exec_engine *eng,
                                     uint32_t index) {
    exec_func_type *sub = &eng->types[index];
    if (sub->supertype < 0) return 1;
    uint32_t super_index = (uint32_t)sub->supertype;
    if (super_index >= eng->type_count || super_index == index) return 0;
    exec_func_type *super = &eng->types[super_index];
    if (super->is_final || sub->kind != super->kind) return 0;
    if (sub->kind == WAST_TYPE_FUNC) {
        if (sub->param_count != super->param_count ||
            sub->result_count != super->result_count) return 0;
        for (int i=0;i<sub->param_count;i++)
            if (!global_type_is_compat(eng,super->params[i],eng,
                                       sub->params[i],0)) return 0;
        for (int i=0;i<sub->result_count;i++)
            if (!global_type_is_compat(eng,sub->results[i],eng,
                                       super->results[i],0)) return 0;
        return 1;
    }
    if (sub->kind == WAST_TYPE_STRUCT) {
        if (sub->field_count < super->field_count) return 0;
        for (int i=0;i<super->field_count;i++) {
            if (sub->field_mutable[i] != super->field_mutable[i] ||
                sub->field_packed[i] != super->field_packed[i]) return 0;
            if (sub->field_mutable[i]) {
                if (!same_value_type(eng,sub->fields[i],eng,
                                     super->fields[i],0)) return 0;
            } else if (!global_type_is_compat(eng,sub->fields[i],eng,
                                              super->fields[i],0)) return 0;
        }
        return 1;
    }
    if (sub->kind == WAST_TYPE_ARRAY) {
        if (sub->field_mutable[0] != super->field_mutable[0] ||
            sub->field_packed[0] != super->field_packed[0]) return 0;
        return sub->field_mutable[0] ?
            same_value_type(eng,sub->fields[0],eng,super->fields[0],0) :
            global_type_is_compat(eng,sub->fields[0],eng,
                                  super->fields[0],0);
    }
    return 0;
}

int nullable_reference_for_heap(const waste_exec_engine *eng,
                                       int32_t heap_type,
                                       wasm_valtype *type) {
    switch (heap_type) {
        case -16: *type = WASM_VALTYPE_FUNCREF; return 1;
        case -17: *type = WASM_VALTYPE_EXTERNREF; return 1;
        case -18: *type = WASM_VALTYPE_ANYREF; return 1;
        case -19: *type = WASM_VALTYPE_EQREF; return 1;
        case -20: *type = WASM_VALTYPE_I31REF; return 1;
        case -21: *type = WASM_VALTYPE_STRUCTREF; return 1;
        case -22: *type = WASM_VALTYPE_ARRAYREF; return 1;
        case -23: *type = WASM_VALTYPE_EXNREF; return 1;
        case -15: *type = WASM_VALTYPE_NULLREF; return 1;
        case -14: *type = WASM_VALTYPE_NULLEXTERNREF; return 1;
        case -13: *type = WASM_VALTYPE_NULLFUNCREF; return 1;
        case -12: *type = WASM_VALTYPE_NULLEXNREF; return 1;
        default:
            if (heap_type < 0 || (uint32_t)heap_type >= eng->type_count)
                return 0;
            *type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE +
                                  (uint32_t)heap_type);
            return 1;
    }
}

#define WASM_VALIDATION_STACK 256
#define WASM_BOTTOM_TYPE ((wasm_valtype)0x7fff)

typedef struct {
    const waste_exec_engine *engine;
    int height;
    int unreachable;
    int tail_call_seen;
    uint8_t kind;
    uint8_t has_else;
    int param_count;
    int result_count;
    wasm_valtype params[WAST_MAX_PARAMS];
    wasm_valtype results[WAST_MAX_RESULTS];
    uint8_t entry_initialized[(EXEC_MAX_LOCALS + 7) / 8];
} wasm_validation_control;

static int validation_local_initialized(const uint8_t *bits, uint32_t index) {
    return (bits[index / 8] & (uint8_t)(1u << (index % 8))) != 0;
}

static void validation_initialize_local(uint8_t *bits, uint32_t index) {
    bits[index / 8] |= (uint8_t)(1u << (index % 8));
}

static int validation_pop(wasm_valtype *stack, int *top,
                                 const wasm_validation_control *control,
                                 wasm_valtype *value) {
    if (*top > control->height) {
        *value = stack[--*top];
        return 1;
    }
    if (control->unreachable) {
        *value = WASM_BOTTOM_TYPE;
        return 1;
    }
    return 0;
}

static int validation_pop_type(wasm_valtype *stack, int *top,
                                      const wasm_validation_control *control,
                                      wasm_valtype expected) {
    wasm_valtype actual;
    if (!validation_pop(stack, top, control, &actual)) return 0;
    if (actual == WASM_BOTTOM_TYPE ||
        global_type_is_compat(control->engine, actual, control->engine,
                              expected, 0)) return 1;
    if (actual == WASM_VALTYPE_FUNCREF_NONNULL &&
        expected == WASM_VALTYPE_FUNCREF) return 1;
    if (actual == WASM_VALTYPE_EXTERNREF_NONNULL &&
        expected == WASM_VALTYPE_EXTERNREF) return 1;
    if (WASM_VALTYPE_IS_TYPE_REF(actual) &&
        expected == WASM_VALTYPE_FUNCREF) return 1;
    if (WASM_VALTYPE_IS_TYPE_REF(actual) &&
        WASM_VALTYPE_IS_TYPE_REF(expected) &&
        (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE &&
        (unsigned)expected < WASM_VALTYPE_TYPE_REF_BASE &&
        WASM_VALTYPE_TYPE_REF_INDEX(actual) ==
        WASM_VALTYPE_TYPE_REF_INDEX(expected)) return 1;
    return 0;
}

static int validation_push(wasm_valtype *stack, int *top,
                                  wasm_valtype value) {
    if (*top >= WASM_VALIDATION_STACK) return 0;
    stack[(*top)++] = value;
    return 1;
}

static int validation_unary(wasm_valtype *stack, int *top,
                                   const wasm_validation_control *control,
                                   wasm_valtype operand,
                                   wasm_valtype result) {
    return validation_pop_type(stack, top, control, operand) &&
           validation_push(stack, top, result);
}

static int validation_binary(wasm_valtype *stack, int *top,
                                    const wasm_validation_control *control,
                                    wasm_valtype operand,
                                    wasm_valtype result) {
    return validation_pop_type(stack, top, control, operand) &&
           validation_pop_type(stack, top, control, operand) &&
           validation_push(stack, top, result);
}

static int validation_local_type(const exec_func_type *signature,
                                        const exec_func *func,
                                        uint32_t index,
                                        wasm_valtype *type) {
    if (index < (uint32_t)signature->param_count) {
        *type = signature->params[index];
        return 1;
    }
    index -= (uint32_t)signature->param_count;
    if (index >= func->local_count) return 0;
    *type = func->locals[index];
    return 1;
}

/* Incremental core validator.  It is deliberately conservative: a function
 * containing an instruction or control signature outside the covered slice
 * is INCONCLUSIVE, while any error in a modeled instruction is INVALID.  This
 * lets assert_invalid observe real validation failures without guessing about
 * proposal instructions that the native executor does not validate yet. */
static wasm_validation_status validate_function_body(
    const waste_exec_engine *eng, const exec_func *func,
    const exec_instr *code, uint32_t code_size,
    uint32_t *failure_instruction) {
    if (func->type_index >= eng->type_count) return WASM_VALIDATION_INVALID;
    const exec_func_type *signature = &eng->types[func->type_index];
    wasm_valtype stack[WASM_VALIDATION_STACK];
    int top = 0;
    wasm_validation_control controls[EXEC_MAX_CONTROL + 1];
    uint8_t initialized[(EXEC_MAX_LOCALS + 7) / 8];
    int control_top = 0;
    memset(&controls[0], 0, sizeof(controls[0]));
    memset(initialized, 0, sizeof(initialized));
    for (int i = 0; i < signature->param_count; i++)
        validation_initialize_local(initialized, (uint32_t)i);
    for (uint32_t i = 0; i < func->local_count; i++) {
        wasm_valtype type = func->locals[i];
        if (!is_reference_type(type) || is_nullable_reference_type(type))
            validation_initialize_local(
                initialized, (uint32_t)signature->param_count + i);
    }
    controls[0].engine = eng;

    for (uint32_t pc = 0; pc < code_size; pc++) {
        const exec_instr *instr = &code[pc];
        *failure_instruction = pc;
        wasm_validation_control *control = &controls[control_top];

        if (getenv("WAST_DEBUG_VALIDATE_TRACE"))
            fprintf(stderr, "validate pc=%u op=%02x top=%d control=%d height=%d unreachable=%d\n",
                    pc, instr->opcode, top, control_top, control->height,
                    control->unreachable);
        switch (instr->opcode) {
            case 0x00:
                top = control->height;
                control->unreachable = 1;
                break;
            case 0x01:
                break;
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x1f: {
                const wasm_valtype *params = NULL;
                const wasm_valtype *results = NULL;
                int param_count = 0;
                int result_count = 0;
                wasm_valtype direct_result;
                if (instr->block_type_index >= 0) {
                    if ((uint32_t)instr->block_type_index >= eng->type_count)
                        return WASM_VALIDATION_INVALID;
                    const exec_func_type *block_type =
                        &eng->types[instr->block_type_index];
                    params = block_type->params;
                    param_count = block_type->param_count;
                    results = block_type->results;
                    result_count = block_type->result_count;
                } else if (instr->has_block_result_type) {
                    direct_result = instr->block_result_type;
                    results = &direct_result;
                    result_count = 1;
                }
                if (instr->opcode == 0x04 &&
                    !validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                if (instr->opcode == 0x1f) {
                    for (uint32_t i = 0; i < instr->catch_count; i++) {
                        const exec_catch *catch_ = &instr->catches[i];
                        if (catch_->depth > (uint32_t)control_top)
                            return WASM_VALIDATION_INVALID;

                        const wasm_valtype *label_types;
                        int label_count;
                        if (catch_->depth == (uint32_t)control_top) {
                            label_types = signature->results;
                            label_count = signature->result_count;
                        } else {
                            const wasm_validation_control *target =
                                &controls[control_top -
                                          (int)catch_->depth];
                            label_types = target->kind == 0x03 ?
                                target->params : target->results;
                            label_count = target->kind == 0x03 ?
                                target->param_count : target->result_count;
                        }

                        const exec_func_type *tag_type = NULL;
                        int catch_value_count = (catch_->kind & 1u) ? 1 : 0;
                        if (catch_->kind < 2) {
                            if (catch_->tag_index >= eng->tag_count)
                                return WASM_VALIDATION_INVALID;
                            uint32_t tag_type_index =
                                eng->tag_types[catch_->tag_index];
                            if (tag_type_index >= eng->type_count)
                                return WASM_VALIDATION_INVALID;
                            tag_type = &eng->types[tag_type_index];
                            catch_value_count += tag_type->param_count;
                        }
                        if (catch_value_count != label_count)
                        {

                            if (getenv("WAST_DEBUG_VALIDATION"))
                                fprintf(stderr, "try catch arity pc=%u kind=%u depth=%u values=%d labels=%d\n",
                                        pc, catch_->kind, catch_->depth,
                                        catch_value_count, label_count);
                            return WASM_VALIDATION_INVALID;
                        }
                        for (int value = 0;
                             tag_type && value < tag_type->param_count;
                             value++)
                            if (!global_type_is_compat(
                                    eng, tag_type->params[value], eng,
                                    label_types[value], 0))
                            {

                                if (getenv("WAST_DEBUG_VALIDATION"))
                                    fprintf(stderr, "try catch payload pc=%u kind=%u value=%d actual=%u expected=%u\n",
                                            pc, catch_->kind, value,
                                            (unsigned)tag_type->params[value],
                                            (unsigned)label_types[value]);
                                return WASM_VALIDATION_INVALID;
                            }
                        if ((catch_->kind & 1u) &&
                            !global_type_is_compat(
                                eng, WASM_VALTYPE_EXNREF_NONNULL, eng,
                                label_types[label_count - 1], 0))
                        {

                            if (getenv("WAST_DEBUG_VALIDATION"))
                                fprintf(stderr, "try catch ref pc=%u kind=%u actual=%u expected=%u\n",
                                        pc, catch_->kind,
                                        (unsigned)WASM_VALTYPE_EXNREF_NONNULL,
                                        (unsigned)label_types[label_count - 1]);
                            return WASM_VALIDATION_INVALID;
                        }
                    }
                }
                for (int i = param_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control, params[i - 1]))
                        return WASM_VALIDATION_INVALID;
                if (param_count > WAST_MAX_PARAMS)
                    return WASM_VALIDATION_UNSUPPORTED;
                if (control_top >= EXEC_MAX_CONTROL)
                    return WASM_VALIDATION_UNSUPPORTED;
                wasm_validation_control *next = &controls[++control_top];
                memset(next, 0, sizeof(*next));
                next->engine = eng;
                next->height = top;
                next->kind = (uint8_t)instr->opcode;
                next->param_count = param_count;
                next->result_count = result_count;
                memcpy(next->entry_initialized, initialized,
                       sizeof(next->entry_initialized));
                if (param_count)
                    memcpy(next->params, params,
                           (size_t)param_count * sizeof(params[0]));
                if (result_count)
                    memcpy(next->results, results,
                           (size_t)result_count * sizeof(results[0]));
                for (int i = 0; i < param_count; i++)
                    if (!validation_push(stack, &top, params[i]))
                        return WASM_VALIDATION_UNSUPPORTED;
                break;
            }
            case 0x08: {
                if (instr->u32_imm >= eng->tag_count)
                    return WASM_VALIDATION_INVALID;
                uint32_t type_index = eng->tag_types[instr->u32_imm];
                if (type_index >= eng->type_count)
                    return WASM_VALIDATION_INVALID;
                exec_func_type *tag_type = &eng->types[type_index];
                for (int i = tag_type->param_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control, tag_type->params[i - 1]))
                        return WASM_VALIDATION_INVALID;
                top = control->height;
                control->unreachable = 1;
                break;
            }
            case 0x0a:
                if (!validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_EXNREF))
                    return WASM_VALIDATION_INVALID;
                top = control->height;
                control->unreachable = 1;
                break;
            case 0x05:
                if (control_top == 0 || control->kind != 0x04 ||
                    control->has_else)
                    return WASM_VALIDATION_INVALID;
                control = &controls[control_top];
                for (int i = control->result_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control,
                            control->results[i - 1]))
                        return WASM_VALIDATION_INVALID;
                if (top != control->height)
                    return WASM_VALIDATION_INVALID;
                top = control->height;
                control->unreachable = 0;
                control->has_else = 1;
                memcpy(initialized, control->entry_initialized,
                       sizeof(initialized));
                for (int i = 0; i < control->param_count; i++)
                    if (!validation_push(stack, &top,
                                                control->params[i]))
                        return WASM_VALIDATION_UNSUPPORTED;
                break;
            case 0x0b:
                if (control_top > 0) {
                    if (control->kind == 0x04 && !control->has_else) {
                        if (control->param_count != control->result_count)
                            return WASM_VALIDATION_INVALID;
                        for (int i = 0; i < control->param_count; i++)
                            if (control->params[i] != control->results[i])
                                return WASM_VALIDATION_INVALID;
                    }
                    for (int i = control->result_count; i > 0; i--)
                        if (!validation_pop_type(
                                stack, &top, control,
                                control->results[i - 1]))
                            return WASM_VALIDATION_INVALID;
                    if (top != control->height)
                        return WASM_VALIDATION_INVALID;
                    wasm_valtype end_types[WAST_MAX_RESULTS];
                    int end_count = control->result_count;
                    if (end_count)
                        memcpy(end_types, control->results,
                               (size_t)end_count * sizeof(end_types[0]));
                    top = control->height;
                    memcpy(initialized, control->entry_initialized,
                           sizeof(initialized));
                    control_top--;
                    for (int i = 0; i < end_count; i++)
                        if (!validation_push(stack, &top,
                                                    end_types[i]))
                            return WASM_VALIDATION_UNSUPPORTED;
                    break;
                }
                for (int i = signature->result_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control,
                            signature->results[i - 1]))
                        return WASM_VALIDATION_INVALID;
                /* Stack polymorphism supplies missing operands at the frame
                 * height; it does not discard concrete operands pushed after
                 * the path became unreachable.  After consuming the declared
                 * results the function stack must therefore always be empty. */
                if (top != 0)
                    return WASM_VALIDATION_INVALID;
                return WASM_VALIDATION_VALID;
            case 0x0f:
                for (int i = signature->result_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control,
                            signature->results[i - 1]))
                        return WASM_VALIDATION_INVALID;
                top = control->height;
                control->unreachable = 1;
                break;
            case 0x0c:
            case 0x0d: {
                uint32_t depth = instr->u32_imm;
                if (instr->opcode == 0x0d &&
                    !validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                if (depth > (uint32_t)control_top)
                    return WASM_VALIDATION_INVALID;
                const wasm_valtype *label_types;
                int label_count;
                if (depth == (uint32_t)control_top) {
                    label_types = signature->results;
                    label_count = signature->result_count;
                } else {
                    const wasm_validation_control *target =
                        &controls[control_top - (int)depth];
                    if (target->kind == 0x03) {
                        label_types = target->params;
                        label_count = target->param_count;
                    } else {
                        label_types = target->results;
                        label_count = target->result_count;
                    }
                }
                for (int i = label_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control, label_types[i - 1]))
                        return WASM_VALIDATION_INVALID;
                if (instr->opcode == 0x0d) {
                    for (int i = 0; i < label_count; i++)
                        if (!validation_push(stack, &top,
                                                    label_types[i]))
                            return WASM_VALIDATION_UNSUPPORTED;
                } else {
                    top = control->height;
                    control->unreachable = 1;
                }
                break;
            }
            case 0xd4: {
                wasm_valtype reference = WASM_BOTTOM_TYPE;
                if (!validation_pop(stack, &top, control, &reference) ||
                    (reference != WASM_BOTTOM_TYPE &&
                     !is_reference_type(reference))) {

                    if (getenv("WAST_DEBUG_VALIDATION"))
                        fprintf(stderr, "ref-branch pop/type pc=%u op=%x ref=%u top=%d height=%d unreachable=%d\n",
                                pc, instr->opcode, (unsigned)reference, top,
                                control->height, control->unreachable);
                    return WASM_VALIDATION_INVALID;
                }
                if (!validation_push(
                        stack, &top,
                        reference == WASM_BOTTOM_TYPE ? reference :
                        nonnullable_reference_type(reference)))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            }
            case 0xd5:
            case 0xd6: {
                wasm_valtype reference = WASM_BOTTOM_TYPE;
                if (!validation_pop(stack, &top, control, &reference) ||
                    (reference != WASM_BOTTOM_TYPE &&
                     !is_reference_type(reference))) {

                    if (getenv("WAST_DEBUG_VALIDATION"))
                        fprintf(stderr, "ref-branch pop/type pc=%u op=%x ref=%u top=%d height=%d unreachable=%d\n",
                                pc, instr->opcode, (unsigned)reference, top,
                                control->height, control->unreachable);
                    return WASM_VALIDATION_INVALID;
                }
                wasm_valtype refined = reference == WASM_BOTTOM_TYPE ?
                    reference : nonnullable_reference_type(reference);
                uint32_t depth = instr->u32_imm;
                if (depth > (uint32_t)control_top) {

                    if (getenv("WAST_DEBUG_VALIDATION"))
                        fprintf(stderr, "ref-branch depth pc=%u depth=%u control=%d\n",
                                pc, depth, control_top);
                    return WASM_VALIDATION_INVALID;
                }
                const wasm_valtype *label_types;
                int label_count;
                if (depth == (uint32_t)control_top) {
                    label_types = signature->results;
                    label_count = signature->result_count;
                } else {
                    const wasm_validation_control *target =
                        &controls[control_top - (int)depth];
                    label_types = target->kind == 0x03 ? target->params :
                                                        target->results;
                    label_count = target->kind == 0x03 ? target->param_count :
                                                        target->result_count;
                }
                int carried_count = label_count;
                if (instr->opcode == 0xd6) {
                    if (label_count < 1 ||
                        (refined != WASM_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, refined, eng,
                                                label_types[label_count - 1],
                                                0))) {

                        if (getenv("WAST_DEBUG_VALIDATION"))
                            fprintf(stderr, "ref-branch result pc=%u ref=%u refined=%u labels=%d last=%u\n",
                                    pc, (unsigned)reference, (unsigned)refined,
                                    label_count, label_count ?
                                    (unsigned)label_types[label_count - 1] : 0u);
                        return WASM_VALIDATION_INVALID;
                    }
                    carried_count--;
                }
                wasm_valtype carried[WAST_MAX_RESULTS];
                for (int i = carried_count; i > 0; i--)
                    if (!validation_pop(stack, &top, control,
                                               &carried[i - 1]) ||
                        (carried[i - 1] != WASM_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, carried[i - 1], eng,
                                                label_types[i - 1], 0))) {

                        if (getenv("WAST_DEBUG_VALIDATION"))
                            fprintf(stderr, "ref-branch carried pc=%u i=%d actual=%u expected=%u top=%d height=%d\n",
                                    pc, i, (unsigned)carried[i - 1],
                                    (unsigned)label_types[i - 1], top,
                                    control->height);
                        return WASM_VALIDATION_INVALID;
                    }
                for (int i = 0; i < carried_count; i++)
                    /* Pop then re-push at the label type.  Preserving a
                     * narrower operand type here is unsound: subsequent
                     * fall-through instructions only know the branch label's
                     * declared type (GC issue 516). */
                    if (!validation_push(stack, &top, label_types[i]))
                        return WASM_VALIDATION_UNSUPPORTED;
                if (instr->opcode == 0xd5 &&
                    !validation_push(stack, &top, refined))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            }
            case 0x0e: {
                if (!validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                uint32_t *depths;
                memcpy(&depths, instr->v128_imm.bytes, sizeof(depths));
                int common_count = -1;
                for (uint32_t i = 0; i <= instr->u32_imm; i++) {
                    uint32_t depth = depths[i];
                    int label_count;
                    if (depth > (uint32_t)control_top)
                        return WASM_VALIDATION_INVALID;
                    if (depth == (uint32_t)control_top) {
                        label_count = signature->result_count;
                    } else {
                        const wasm_validation_control *target =
                            &controls[control_top - (int)depth];
                        if (target->kind == 0x03) {
                            label_count = target->param_count;
                        } else {
                            label_count = target->result_count;
                        }
                    }
                    if (common_count < 0) {
                        common_count = label_count;
                    } else if (common_count != label_count)
                        return WASM_VALIDATION_INVALID;
                }
                wasm_valtype operands[WAST_MAX_RESULTS];
                for (int i = common_count; i > 0; i--)
                    if (!validation_pop(
                            stack, &top, control, &operands[i - 1]))
                        return WASM_VALIDATION_INVALID;
                for (uint32_t i = 0; i <= instr->u32_imm; i++) {
                    uint32_t depth = depths[i];
                    const wasm_valtype *label_types;
                    if (depth == (uint32_t)control_top) {
                        label_types = signature->results;
                    } else {
                        const wasm_validation_control *target =
                            &controls[control_top - (int)depth];
                        label_types = target->kind == 0x03 ?
                                      target->params : target->results;
                    }
                    for (int j = 0; j < common_count; j++)
                        if (operands[j] != WASM_BOTTOM_TYPE &&
                            !global_type_is_compat(
                                eng, operands[j], eng, label_types[j], 0))
                            return WASM_VALIDATION_INVALID;
                }
                top = control->height;
                control->unreachable = 1;
                break;
            }
            case 0x1a: {
                wasm_valtype ignored;
                if (!validation_pop(stack, &top, control, &ignored))
                    return WASM_VALIDATION_INVALID;
                break;
            }
            case 0x1b: {
                wasm_valtype condition, second, first;
                if (!validation_pop(stack, &top, control, &condition) ||
                    !validation_pop(stack, &top, control, &second) ||
                    !validation_pop(stack, &top, control, &first) ||
                    (condition != WASM_BOTTOM_TYPE &&
                     condition != WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                wasm_valtype result = first == WASM_BOTTOM_TYPE ? second : first;
                if (!instr->simd_op && second != WASM_BOTTOM_TYPE &&
                    first != WASM_BOTTOM_TYPE &&
                    first != second)
                    return WASM_VALIDATION_INVALID;
                if (!instr->simd_op) {
                    if (result != WASM_BOTTOM_TYPE &&
                        is_reference_type(result))
                        return WASM_VALIDATION_INVALID;
                } else {
                    wasm_valtype selected_type;
                    memcpy(&selected_type, instr->v128_imm.bytes,
                           sizeof(selected_type));
                    if ((first != WASM_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, first, eng,
                                                selected_type, 0)) ||
                        (second != WASM_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, second, eng,
                                                selected_type, 0)))
                        return WASM_VALIDATION_INVALID;
                    result = selected_type;
                }
                if (!validation_push(stack, &top, result))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            }
            case 0x20: {
                uint32_t index = instr->u32_imm;
                wasm_valtype type;
                if (!validation_local_type(signature, func, index, &type) ||
                    !validation_local_initialized(initialized, index))
                    return WASM_VALIDATION_INVALID;
                if (!validation_push(stack, &top, type))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            }
            case 0x21:
            case 0x22: {
                wasm_valtype type;
                if (!validation_local_type(signature, func,
                                                  instr->u32_imm, &type) ||
                    !validation_pop_type(stack, &top, control, type))
                    return WASM_VALIDATION_INVALID;
                validation_initialize_local(initialized, instr->u32_imm);
                if (instr->opcode == 0x22 &&
                    !validation_push(stack, &top, type))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            }
            case 0x23:
                if (instr->u32_imm >= eng->global_count)
                    return WASM_VALIDATION_INVALID;
                if (!validation_push(
                        stack, &top,
                        eng->globals[instr->u32_imm]->value.type))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            case 0x24:
                if (instr->u32_imm >= eng->global_count ||
                    !eng->globals[instr->u32_imm]->mutable_ ||
                    !validation_pop_type(
                        stack, &top, control,
                        eng->globals[instr->u32_imm]->value.type))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x25:
                if (instr->u32_imm >= eng->table_count ||
                    !validation_pop_type(stack, &top, control,
                        eng->tables[instr->u32_imm]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32) ||
                    !validation_push(
                        stack, &top,
                        eng->tables[instr->u32_imm]->element_type))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x26:
                if (instr->u32_imm >= eng->table_count ||
                    !validation_pop_type(
                        stack, &top, control,
                        eng->tables[instr->u32_imm]->element_type) ||
                    !validation_pop_type(stack, &top, control,
                        eng->tables[instr->u32_imm]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x10:
            case 0x12: {
                const exec_func_type *callee;
                if (instr->u32_imm < eng->import_func_count)
                    callee = &eng->types[
                        eng->import_func_types[instr->u32_imm]];
                else {
                    uint32_t index = instr->u32_imm - eng->import_func_count;
                    if (index >= eng->func_count)
                        return WASM_VALIDATION_INVALID;
                    callee = &eng->types[eng->funcs[index].type_index];
                }
                for (int i = callee->param_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control, callee->params[i - 1]))
                        return WASM_VALIDATION_INVALID;
                if (instr->opcode == 0x12) {
                    if (callee->result_count != signature->result_count)
                        return WASM_VALIDATION_INVALID;
                    for (int i = 0; i < callee->result_count; i++)
                        if (!global_type_is_compat(
                                eng, callee->results[i], eng,
                                signature->results[i], 0))
                            return WASM_VALIDATION_INVALID;
                    top = control->height;
                    control->unreachable = 1;
                    control->tail_call_seen = 1;
                } else {
                    for (int i = 0; i < callee->result_count; i++)
                        if (!validation_push(stack, &top,
                                                    callee->results[i]))
                            return WASM_VALIDATION_UNSUPPORTED;
                }
                break;
            }
            case 0x11:
            case 0x13: {
                if (instr->u32_imm >= eng->type_count ||
                    instr->simd_op >= eng->table_count ||
                    !is_function_reference_type(
                        eng->tables[instr->simd_op]->element_type) ||
                    !validation_pop_type(stack, &top, control,
                        eng->tables[instr->simd_op]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                const exec_func_type *callee = &eng->types[instr->u32_imm];
                for (int i = callee->param_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control, callee->params[i - 1]))
                        return WASM_VALIDATION_INVALID;
                if (instr->opcode == 0x13) {
                    if (callee->result_count != signature->result_count)
                        return WASM_VALIDATION_INVALID;
                    for (int i = 0; i < callee->result_count; i++)
                        if (!global_type_is_compat(
                                eng, callee->results[i], eng,
                                signature->results[i], 0))
                            return WASM_VALIDATION_INVALID;
                    top = control->height;
                    control->unreachable = 1;
                    control->tail_call_seen = 1;
                } else {
                    for (int i = 0; i < callee->result_count; i++)
                        if (!validation_push(stack, &top,
                                                    callee->results[i]))
                            return WASM_VALIDATION_UNSUPPORTED;
                }
                break;
            }
            case 0x14:
            case 0x15: {
                if (instr->u32_imm >= eng->type_count)
                    return WASM_VALIDATION_INVALID;
                const exec_func_type *callee = &eng->types[instr->u32_imm];
                wasm_valtype reference;
                if (!validation_pop(stack, &top, control, &reference) ||
                    (reference != WASM_BOTTOM_TYPE &&
                     (!WASM_VALTYPE_IS_TYPE_REF(reference) ||
                      !func_type_is_subtype(
                          eng, WASM_VALTYPE_TYPE_REF_INDEX(reference),
                          eng, instr->u32_imm))))
                    return WASM_VALIDATION_INVALID;
                for (int i = callee->param_count; i > 0; i--)
                    if (!validation_pop_type(
                            stack, &top, control, callee->params[i - 1]))
                        return WASM_VALIDATION_INVALID;
                if (instr->opcode == 0x15) {
                    if (callee->result_count != signature->result_count)
                        return WASM_VALIDATION_INVALID;
                    for (int i = 0; i < callee->result_count; i++)
                        if (!global_type_is_compat(
                                eng, callee->results[i], eng,
                                signature->results[i], 0))
                            return WASM_VALIDATION_INVALID;
                    top = control->height;
                    control->unreachable = 1;
                    control->tail_call_seen = 1;
                } else {
                    for (int i = 0; i < callee->result_count; i++)
                        if (!validation_push(stack, &top,
                                                    callee->results[i]))
                            return WASM_VALIDATION_UNSUPPORTED;
                }
                break;
            }
            case 0x41:
                if (!validation_push(stack, &top, WASM_VALTYPE_I32))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            case 0x42:
                if (!validation_push(stack, &top, WASM_VALTYPE_I64))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            case 0x43:
                if (!validation_push(stack, &top, WASM_VALTYPE_F32))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            case 0x44:
                if (!validation_push(stack, &top, WASM_VALTYPE_F64))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            case 0x28: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            case 0x29: case 0x30: case 0x31: case 0x32: case 0x33:
            case 0x34: case 0x35:
            case 0x2a: case 0x2b: {
                uint32_t natural = instr->opcode == 0x29 ||
                                   instr->opcode == 0x2b ? 3u :
                                   instr->opcode == 0x28 ||
                                   instr->opcode == 0x2a ||
                                   instr->opcode == 0x2e ||
                                   instr->opcode == 0x2f ||
                                   instr->opcode == 0x32 ||
                                   instr->opcode == 0x33 ||
                                   instr->opcode == 0x34 ||
                                   instr->opcode == 0x35 ? 2u :
                                   instr->opcode == 0x30 ||
                                   instr->opcode == 0x31 ? 0u : 0u;
                if (instr->opcode == 0x2e || instr->opcode == 0x2f ||
                    instr->opcode == 0x32 || instr->opcode == 0x33)
                    natural = 1;
                wasm_valtype result = instr->opcode == 0x29 ||
                                      (instr->opcode >= 0x30 &&
                                       instr->opcode <= 0x35) ?
                                      WASM_VALTYPE_I64 :
                                      instr->opcode == 0x2a ?
                                      WASM_VALTYPE_F32 :
                                      instr->opcode == 0x2b ?
                                      WASM_VALTYPE_F64 : WASM_VALTYPE_I32;
                if (instr->memory_index >= eng->memory_count ||
                    (!eng->memories[instr->memory_index]->is_64 &&
                     instr->u64_imm > UINT32_MAX) ||
                    instr->simd_op > natural ||
                    !validation_unary(stack, &top, control,
                        eng->memories[instr->memory_index]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32, result))
                    return WASM_VALIDATION_INVALID;
                break;
            }
            case 0x36: case 0x3a: case 0x3b:
            case 0x37: case 0x3c: case 0x3d: case 0x3e:
            case 0x38: case 0x39: {
                uint32_t natural = instr->opcode == 0x37 ||
                                   instr->opcode == 0x39 ? 3u :
                                   instr->opcode == 0x36 ||
                                   instr->opcode == 0x38 ||
                                   instr->opcode == 0x3e ? 2u :
                                   instr->opcode == 0x3b ||
                                   instr->opcode == 0x3d ? 1u : 0u;
                wasm_valtype value_type = instr->opcode == 0x37 ||
                                          (instr->opcode >= 0x3c &&
                                           instr->opcode <= 0x3e) ?
                                          WASM_VALTYPE_I64 :
                                          instr->opcode == 0x38 ?
                                          WASM_VALTYPE_F32 :
                                          instr->opcode == 0x39 ?
                                          WASM_VALTYPE_F64 : WASM_VALTYPE_I32;
                if (instr->memory_index >= eng->memory_count ||
                    (!eng->memories[instr->memory_index]->is_64 &&
                     instr->u64_imm > UINT32_MAX) ||
                    instr->simd_op > natural ||
                    !validation_pop_type(stack, &top, control,
                                                value_type) ||
                    !validation_pop_type(stack, &top, control,
                        eng->memories[instr->memory_index]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            }
            case 0x3f:
                if (instr->memory_index >= eng->memory_count ||
                    !validation_push(stack, &top,
                        eng->memories[instr->memory_index]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x40:
                if (instr->memory_index >= eng->memory_count ||
                    !validation_unary(stack, &top, control,
                        eng->memories[instr->memory_index]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32,
                        eng->memories[instr->memory_index]->is_64 ?
                            WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x45:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
            case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I32,
                                              WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x50:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
            case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I64,
                                              WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x5b: case 0x5c: case 0x5d:
            case 0x5e: case 0x5f: case 0x60:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F32,
                                              WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x61: case 0x62: case 0x63:
            case 0x64: case 0x65: case 0x66:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F64,
                                              WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x67: case 0x68: case 0x69:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e:
            case 0x6f: case 0x70: case 0x71: case 0x72: case 0x73:
            case 0x74: case 0x75: case 0x76: case 0x77: case 0x78:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I32,
                                              WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x79: case 0x7a: case 0x7b:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x7c: case 0x7d: case 0x7e: case 0x7f: case 0x80:
            case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
            case 0x86: case 0x87: case 0x88: case 0x89: case 0x8a:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I64,
                                              WASM_VALTYPE_I64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x8b: case 0x8c: case 0x8d: case 0x8e:
            case 0x8f: case 0x90: case 0x91:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_F32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x92: case 0x93: case 0x94: case 0x95:
            case 0x96: case 0x97: case 0x98:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F32,
                                              WASM_VALTYPE_F32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0x99: case 0x9a: case 0x9b: case 0x9c:
            case 0x9d: case 0x9e: case 0x9f:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_F64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xa0: case 0xa1: case 0xa2: case 0xa3:
            case 0xa4: case 0xa5: case 0xa6:
                if (!validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F64,
                                              WASM_VALTYPE_F64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xa7:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xa8: case 0xa9:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xaa: case 0xab:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xac: case 0xad:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xae: case 0xaf:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_I64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xb0: case 0xb1:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_I64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xb2: case 0xb3:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_F32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xb4: case 0xb5:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_F32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xb6:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_F32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xb7: case 0xb8:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_F64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xb9: case 0xba:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_F64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xbb:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_F64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xbc:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xbd:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_I64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xbe:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_F32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xbf:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_F64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xc0: case 0xc1:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xc2: case 0xc3: case 0xc4:
                if (!validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I64))
                    return WASM_VALIDATION_INVALID;
                break;
            case 0xd0: {
                int32_t heap_type = (int32_t)instr->u32_imm;
                wasm_valtype type;
                if (!nullable_reference_for_heap(eng, heap_type, &type))
                    return WASM_VALIDATION_INVALID;
                if (!validation_push(stack, &top, type))
                    return WASM_VALIDATION_UNSUPPORTED;
                break;
            }
            case 0xd1: {
                wasm_valtype value;
                if (!validation_pop(stack, &top, control, &value) ||
                    (value != WASM_BOTTOM_TYPE &&
                     !is_reference_type(value)) ||
                    !validation_push(stack, &top,
                                            WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            }
            case 0xd2:
                if (instr->u32_imm >=
                    eng->import_func_count + eng->func_count ||
                    !eng->declared_funcs[instr->u32_imm])
                    return WASM_VALIDATION_INVALID;
                {
                    uint32_t function_type =
                        instr->u32_imm < eng->import_func_count ?
                        eng->import_func_types[instr->u32_imm] :
                        eng->funcs[instr->u32_imm -
                                   eng->import_func_count].type_index;
                    if (function_type >= 0x100u ||
                        !validation_push(
                            stack, &top,
                            (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                           function_type)))
                        return WASM_VALIDATION_UNSUPPORTED;
                }
                break;
            case 0xd3: {
                wasm_valtype right, left;
                if (!validation_pop(stack, &top, control, &right) ||
                    !validation_pop(stack, &top, control, &left) ||
                    (right != WASM_BOTTOM_TYPE && !is_eq_reference_type(eng,right)) ||
                    (left != WASM_BOTTOM_TYPE && !is_eq_reference_type(eng,left)) ||
                    !validation_push(stack, &top, WASM_VALTYPE_I32))
                    return WASM_VALIDATION_INVALID;
                break;
            }
            case 0xFD: {
                uint32_t op = instr->simd_op;
                wast_simd_info info;
                if (!wast_simd_get_info(op, &info))
                    return WASM_VALIDATION_INVALID;
                if ((info.immediate == WAST_SIMD_IMM_MEMARG ||
                     info.immediate == WAST_SIMD_IMM_MEMARG_LANE) &&
                    (instr->memory_index >= eng->memory_count ||
                     (!eng->memories[instr->memory_index]->is_64 &&
                      instr->u64_imm > UINT32_MAX) ||
                     instr->alignment > info.natural_alignment))
                    return WASM_VALIDATION_INVALID;
                if (op == 0x0c) {
                    if (!validation_push(stack,&top,WASM_VALTYPE_V128))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if (op <= 0x0a || op == 0x5c || op == 0x5d) {
                    if (!validation_unary(stack,&top,control,
                            eng->memories[instr->memory_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32,
                            WASM_VALTYPE_V128))
                        return WASM_VALIDATION_INVALID;
                } else if (op == 0x0b) {
                    if (!validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !validation_pop_type(stack,&top,control,
                            eng->memories[instr->memory_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if (op >= 0x54 && op <= 0x57) {
                    if (!validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !validation_pop_type(stack,&top,control,
                            eng->memories[instr->memory_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32) ||
                        !validation_push(stack,&top,WASM_VALTYPE_V128))
                        return WASM_VALIDATION_INVALID;
                } else if (op >= 0x58 && op <= 0x5b) {
                    if (!validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !validation_pop_type(stack,&top,control,
                            eng->memories[instr->memory_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if (op >= 0x0f && op <= 0x14) {
                    wasm_valtype input = op <= 0x11 ? WASM_VALTYPE_I32 :
                        op == 0x12 ? WASM_VALTYPE_I64 :
                        op == 0x13 ? WASM_VALTYPE_F32 : WASM_VALTYPE_F64;
                    if (!validation_unary(stack,&top,control,input,WASM_VALTYPE_V128))
                        return WASM_VALIDATION_INVALID;
                } else if (op >= 0x15 && op <= 0x22) {
                    int replace = op==0x17||op==0x1a||op==0x1c||op==0x1e||op==0x20||op==0x22;
                    wasm_valtype scalar = op <= 0x1c ? WASM_VALTYPE_I32 :
                        op <= 0x1e ? WASM_VALTYPE_I64 :
                        op <= 0x20 ? WASM_VALTYPE_F32 : WASM_VALTYPE_F64;
                    if (replace) {
                        if (!validation_pop_type(stack,&top,control,scalar) ||
                            !validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                            !validation_push(stack,&top,WASM_VALTYPE_V128))
                            return WASM_VALIDATION_INVALID;
                    } else if (!validation_unary(stack,&top,control,
                                   WASM_VALTYPE_V128,scalar))
                        return WASM_VALIDATION_INVALID;
                } else if (op==0x53||op==0x63||op==0x64||op==0x83||op==0x84||
                           op==0xa3||op==0xa4||op==0xc3||op==0xc4) {
                    if (!validation_unary(stack,&top,control,
                            WASM_VALTYPE_V128,WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if (op==0x6b||op==0x6c||op==0x6d||op==0x8b||op==0x8c||
                           op==0x8d||op==0xab||op==0xac||op==0xad||op==0xcb||
                           op==0xcc||op==0xcd) {
                    if (!validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                        !validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !validation_push(stack,&top,WASM_VALTYPE_V128))
                        return WASM_VALIDATION_INVALID;
                } else if (op==0x52||(op>=0x105&&op<=0x10c)||op==0x113) {
                    for (int operand=0;operand<3;operand++)
                        if (!validation_pop_type(stack,&top,control,WASM_VALTYPE_V128))
                            return WASM_VALIDATION_INVALID;
                    if (!validation_push(stack,&top,WASM_VALTYPE_V128))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else {
                    int unary = op==0x4d||op==0x5e||op==0x5f||
                        (op>=0x60&&op<=0x62)||(op>=0x67&&op<=0x6a)||
                        op==0x74||op==0x75||op==0x7a||(op>=0x7c&&op<=0x81)||
                        (op>=0x87&&op<=0x8a)||op==0x94||op==0xa0||op==0xa1||
                        (op>=0xa7&&op<=0xaa)||op==0xc0||op==0xc1||
                        (op>=0xc7&&op<=0xca)||op==0xe0||op==0xe1||op==0xe3||
                        op==0xec||op==0xed||op==0xef||(op>=0xf8&&op<=0xff)||
                        (op>=0x101&&op<=0x104);
                    int valid = unary ?
                        validation_unary(stack,&top,control,
                            WASM_VALTYPE_V128,WASM_VALTYPE_V128) :
                        validation_binary(stack,&top,control,
                            WASM_VALTYPE_V128,WASM_VALTYPE_V128);
                    if (!valid) return WASM_VALIDATION_INVALID;
                }
                break;
            }
            case 0xFB: {
                uint32_t op = instr->simd_op;
                uint32_t ti = instr->u32_imm;
                const exec_func_type *gc_type = NULL;
                if ((op <= 0x0e || (op >= 0x10 && op <= 0x13)) &&
                    (ti >= eng->type_count ||
                     ((gc_type = &eng->types[ti])->kind != WAST_TYPE_STRUCT &&
                      gc_type->kind != WAST_TYPE_ARRAY)))
                    return WASM_VALIDATION_INVALID;
                if (op == 0x00 || op == 0x01) {
                    if (gc_type->kind != WAST_TYPE_STRUCT)
                        return WASM_VALIDATION_INVALID;
                    if (op == 0x00) {
                        for (int i=gc_type->field_count;i-- > 0;)
                            if (!validation_pop_type(stack,&top,control,
                                    gc_type->field_packed[i] ? WASM_VALTYPE_I32 :
                                                               gc_type->fields[i]))
                                return WASM_VALIDATION_INVALID;
                    } else {
                        for (int i=0;i<gc_type->field_count;i++)
                            if (is_reference_type(gc_type->fields[i]) &&
                                !is_nullable_reference_type(gc_type->fields[i]))
                                return WASM_VALIDATION_INVALID;
                    }
                    if (!validation_push(stack,&top,
                            (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE+ti)))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if (op >= 0x02 && op <= 0x05) {
                    uint32_t field=instr->memory_index;
                    if (gc_type->kind!=WAST_TYPE_STRUCT ||
                        field >= (uint32_t)gc_type->field_count)
                        return WASM_VALIDATION_INVALID;
                    if ((op==0x03 || op==0x04) && !gc_type->field_packed[field])
                        return WASM_VALIDATION_INVALID;
                    if (op==0x02 && gc_type->field_packed[field])
                        return WASM_VALIDATION_INVALID;
                    if (op==0x05 &&
                        (!gc_type->field_mutable[field] ||
                         !validation_pop_type(stack,&top,control,
                            gc_type->field_packed[field] ? WASM_VALTYPE_I32 :
                                                          gc_type->fields[field])))
                        return WASM_VALIDATION_INVALID;
                    if (!validation_pop_type(stack,&top,control,
                            (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE+ti)))
                        return WASM_VALIDATION_INVALID;
                    if (op!=0x05 && !validation_push(stack,&top,
                            gc_type->field_packed[field] ? WASM_VALTYPE_I32 :
                                                          gc_type->fields[field]))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if (op == 0x06 || op == 0x07 || op == 0x08) {
                    if (gc_type->kind!=WAST_TYPE_ARRAY)
                        return WASM_VALIDATION_INVALID;
                    if (op==0x08) {
                        for(uint32_t i=0;i<instr->lane_index;i++)
                            if(!validation_pop_type(stack,&top,control,
                                gc_type->field_packed[0]?WASM_VALTYPE_I32:
                                                         gc_type->fields[0]))
                                return WASM_VALIDATION_INVALID;
                    } else {
                        if(!validation_pop_type(stack,&top,control,WASM_VALTYPE_I32))
                            return WASM_VALIDATION_INVALID;
                        if(op==0x06 && !validation_pop_type(stack,&top,control,
                            gc_type->field_packed[0]?WASM_VALTYPE_I32:gc_type->fields[0]))
                            return WASM_VALIDATION_INVALID;
                        if(op==0x07 && is_reference_type(gc_type->fields[0]) &&
                           !is_nullable_reference_type(gc_type->fields[0]))
                            return WASM_VALIDATION_INVALID;
                    }
                    if(!validation_push(stack,&top,
                        (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE+ti)))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if (op == 0x09 || op == 0x0a) {
                    if(gc_type->kind!=WAST_TYPE_ARRAY ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                    if(op==0x09) {
                        if(is_reference_type(gc_type->fields[0]) ||
                           instr->memory_index>=eng->declared_data_count)
                            return WASM_VALIDATION_INVALID;
                    } else if(instr->memory_index>=eng->elem_count ||
                        !global_type_is_compat(eng,eng->elem_types[instr->memory_index],
                                              eng,gc_type->fields[0],0))
                        return WASM_VALIDATION_INVALID;
                    if(!validation_push(stack,&top,
                        (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE+ti)))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if (op>=0x0b && op<=0x0e) {
                    if(gc_type->kind!=WAST_TYPE_ARRAY ||
                       (op==0x0b && gc_type->field_packed[0]) ||
                       ((op==0x0c || op==0x0d) && !gc_type->field_packed[0]))
                        return WASM_VALIDATION_INVALID;
                    if(op==0x0e && (!gc_type->field_mutable[0] ||
                       !validation_pop_type(stack,&top,control,
                          gc_type->field_packed[0]?WASM_VALTYPE_I32:gc_type->fields[0])))
                        return WASM_VALIDATION_INVALID;
                    if(!validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,
                          (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE+ti)))
                        return WASM_VALIDATION_INVALID;
                    if(op!=0x0e && !validation_push(stack,&top,
                        gc_type->field_packed[0]?WASM_VALTYPE_I32:gc_type->fields[0]))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if(op==0x0f) {
                    wasm_valtype ref;
                    if(!validation_pop(stack,&top,control,&ref) ||
                       (ref!=WASM_BOTTOM_TYPE &&
                        !global_type_is_compat(eng,ref,eng,WASM_VALTYPE_ARRAYREF,0)) ||
                       !validation_push(stack,&top,WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if(op==0x10) {
                    if(gc_type->kind!=WAST_TYPE_ARRAY || !gc_type->field_mutable[0] ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,
                          gc_type->field_packed[0]?WASM_VALTYPE_I32:gc_type->fields[0]) ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,
                          (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE+ti)))
                        return WASM_VALIDATION_INVALID;
                } else if(op==0x11 || op==0x12) {
                    if(gc_type->kind!=WAST_TYPE_ARRAY || !gc_type->field_mutable[0] ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,
                          (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE+ti)))
                        return WASM_VALIDATION_INVALID;
                    if(op==0x11 && (is_reference_type(gc_type->fields[0]) ||
                       instr->memory_index>=eng->declared_data_count))
                        return WASM_VALIDATION_INVALID;
                    if(op==0x12 && (instr->memory_index>=eng->elem_count ||
                       !global_type_is_compat(eng,eng->elem_types[instr->memory_index],
                                             eng,gc_type->fields[0],0)))
                        return WASM_VALIDATION_INVALID;
                } else if(op==0x13) {
                    uint32_t sti=instr->memory_index;
                    if(gc_type->kind!=WAST_TYPE_ARRAY || !gc_type->field_mutable[0] ||
                       sti>=eng->type_count || eng->types[sti].kind!=WAST_TYPE_ARRAY ||
                       gc_type->field_packed[0] != eng->types[sti].field_packed[0] ||
                       !global_type_is_compat(eng,eng->types[sti].fields[0],eng,
                                             gc_type->fields[0],0) ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,
                           (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE+sti)) ||
                       !validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                       !validation_pop_type(stack,&top,control,
                           (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE+ti)))
                        return WASM_VALIDATION_INVALID;
                } else if(op>=0x14 && op<=0x17) {
                    wasm_valtype ref,target;
                    if(!validation_pop(stack,&top,control,&ref) ||
                       (ref!=WASM_BOTTOM_TYPE && !is_reference_type(ref)) ||
                       !nullable_reference_for_heap(eng,instr->block_type_index,&target))
                        return WASM_VALIDATION_INVALID;
                    if(op>=0x16) {
                        if(op==0x16) target=nonnullable_reference_type(target);
                        if(!validation_push(stack,&top,target))
                            return WASM_VALIDATION_UNSUPPORTED;
                    } else if(!validation_push(stack,&top,WASM_VALTYPE_I32))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if(op==0x18 || op==0x19) {
                    wasm_valtype source,target,operand;
                    if(!nullable_reference_for_heap(eng,instr->block_type_index,&source) ||
                       !nullable_reference_for_heap(eng,(int32_t)instr->lane_index,&target))
                        return WASM_VALIDATION_INVALID;
                    if(!(instr->alignment&1u)) source=nonnullable_reference_type(source);
                    if(!(instr->alignment&2u)) target=nonnullable_reference_type(target);
                    if(!global_type_is_compat(eng,target,eng,source,0) ||
                       !validation_pop(stack,&top,control,&operand) ||
                       (operand!=WASM_BOTTOM_TYPE &&
                        !global_type_is_compat(eng,operand,eng,source,0)))
                        return WASM_VALIDATION_INVALID;
                    uint32_t depth=instr->u32_imm;
                    if(depth>(uint32_t)control_top) return WASM_VALIDATION_INVALID;
                    const wasm_valtype *label_types;
                    int label_count;
                    if(depth==(uint32_t)control_top){label_types=signature->results;label_count=signature->result_count;}
                    else {
                        const wasm_validation_control *target_control=&controls[control_top-(int)depth];
                        label_types=target_control->kind==0x03?target_control->params:target_control->results;
                        label_count=target_control->kind==0x03?target_control->param_count:target_control->result_count;
                    }
                    wasm_valtype diff_type=source;
                    if((instr->alignment&3u)==3u)
                        diff_type=nonnullable_reference_type(source);
                    wasm_valtype carried=op==0x18?target:diff_type;
                    if(label_count<1 || !global_type_is_compat(eng,carried,eng,
                                                               label_types[label_count-1],0))
                        return WASM_VALIDATION_INVALID;
                    /* The fall-through stack prefix is typed through the
                     * branch label.  Replace the original (possibly more
                     * precise) operands with the label's declared types,
                     * matching the reference validator's pop/push rule. */
                    for(int i=label_count-1;i>0;i--)
                        if(!validation_pop_type(stack,&top,control,
                                                       label_types[i-1]))
                            return WASM_VALIDATION_INVALID;
                    for(int i=0;i<label_count-1;i++)
                        if(!validation_push(stack,&top,label_types[i]))
                            return WASM_VALIDATION_UNSUPPORTED;
                    wasm_valtype fallthrough=op==0x18?diff_type:target;
                    if(!validation_push(stack,&top,fallthrough))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if(op==0x1a) {
                    if(!validation_unary(stack,&top,control,WASM_VALTYPE_EXTERNREF,
                                                WASM_VALTYPE_ANYREF))
                        return WASM_VALIDATION_INVALID;
                } else if(op==0x1b) {
                    if(!validation_unary(stack,&top,control,WASM_VALTYPE_ANYREF,
                                                WASM_VALTYPE_EXTERNREF))
                        return WASM_VALIDATION_INVALID;
                } else if(op==0x1c) {
                    if(!validation_unary(stack,&top,control,WASM_VALTYPE_I32,
                                                WASM_VALTYPE_I31REF_NONNULL))
                        return WASM_VALIDATION_INVALID;
                } else if(op==0x1d || op==0x1e) {
                    if(!validation_unary(stack,&top,control,WASM_VALTYPE_I31REF,
                                                WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else {
                    return WASM_VALIDATION_UNSUPPORTED;
                }
                break;
            }
            case 0xFC: {
                uint32_t sub = instr->simd_op;
                if (sub <= 7) {
                    /* i32/i64.trunc_sat_f32/f64_s/u */
                    wasm_valtype from = (sub < 4) ?
                        ((sub & 2) ? WASM_VALTYPE_F64 : WASM_VALTYPE_F32) :
                        ((sub & 2) ? WASM_VALTYPE_F64 : WASM_VALTYPE_F32);
                    wasm_valtype to = (sub < 4) ? WASM_VALTYPE_I32 : WASM_VALTYPE_I64;
                    if (!validation_unary(stack, &top, control, from, to))
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 10) { /* memory.copy: [i32 i32 i32] -> [] */
                    uint32_t src_mem = instr->source_memory_index;
                    if (instr->memory_index >= eng->memory_count ||
                        src_mem >= eng->memory_count)
                        return WASM_VALIDATION_INVALID;
                    wasm_valtype dst_type = eng->memories[instr->memory_index]->is_64 ?
                        WASM_VALTYPE_I64 : WASM_VALTYPE_I32;
                    wasm_valtype src_type = eng->memories[src_mem]->is_64 ?
                        WASM_VALTYPE_I64 : WASM_VALTYPE_I32;
                    wasm_valtype len_type = dst_type == WASM_VALTYPE_I64 &&
                        src_type == WASM_VALTYPE_I64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32;
                    if (!validation_pop_type(stack, &top, control, len_type) ||
                        !validation_pop_type(stack, &top, control, src_type) ||
                        !validation_pop_type(stack, &top, control, dst_type))
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 11) { /* memory.fill: [i32 i32 i32] -> [] */
                    if (instr->memory_index >= eng->memory_count ||
                        !validation_pop_type(stack, &top, control,
                            eng->memories[instr->memory_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32) ||
                        !validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !validation_pop_type(stack, &top, control,
                            eng->memories[instr->memory_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 8) { /* memory.init: [i32 i32 i32] -> [] */
                    if (instr->memory_index >= eng->memory_count ||
                        !eng->has_data_count ||
                        instr->u32_imm >= eng->declared_data_count ||
                        !validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !validation_pop_type(stack, &top, control,
                            eng->memories[instr->memory_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 9) { /* data.drop: [] -> [] */
                    if (!eng->has_data_count ||
                        instr->u32_imm >= eng->declared_data_count)
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 12) { /* table.init: [i32 i32 i32] -> [] */
                    uint32_t table_index = instr->v128_imm.bytes[0];
                    if (instr->u32_imm >= eng->elem_count ||
                        table_index >= eng->table_count ||
                        !global_type_is_compat(
                            eng, eng->elem_types[instr->u32_imm],
                            eng->tables[table_index]->type_owner,
                            eng->tables[table_index]->element_type, 0) ||
                        !validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !validation_pop_type(stack, &top, control,
                            eng->tables[table_index]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 13) { /* elem.drop: [] -> [] */
                    if (instr->u32_imm >= eng->elem_count)
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 14) { /* table.copy: [i32 i32 i32] -> [] */
                    uint32_t dst_table = instr->u32_imm;
                    uint32_t src_table = instr->v128_imm.bytes[0];
                    if (dst_table >= eng->table_count || src_table >= eng->table_count ||
                        !global_type_is_compat(
                            eng->tables[src_table]->type_owner,
                            eng->tables[src_table]->element_type,
                            eng->tables[dst_table]->type_owner,
                            eng->tables[dst_table]->element_type, 0))
                        return WASM_VALIDATION_INVALID;
                    wasm_valtype dst_type = eng->tables[dst_table]->is_64 ?
                        WASM_VALTYPE_I64 : WASM_VALTYPE_I32;
                    wasm_valtype src_type = eng->tables[src_table]->is_64 ?
                        WASM_VALTYPE_I64 : WASM_VALTYPE_I32;
                    wasm_valtype len_type = dst_type == WASM_VALTYPE_I64 &&
                        src_type == WASM_VALTYPE_I64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32;
                    if (!validation_pop_type(stack, &top, control, len_type) ||
                        !validation_pop_type(stack, &top, control, src_type) ||
                        !validation_pop_type(stack, &top, control, dst_type))
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 15) { /* table.grow: [ref i32] -> [i32] */
                    if (instr->u32_imm >= eng->table_count ||
                        !validation_pop_type(stack, &top, control,
                            eng->tables[instr->u32_imm]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32) ||
                        !validation_pop_type(
                            stack, &top, control,
                            eng->tables[instr->u32_imm]->element_type) ||
                        !validation_push(stack, &top,
                            eng->tables[instr->u32_imm]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                } else if (sub == 16) { /* table.size: [] -> [i32] */
                    if (instr->u32_imm >= eng->table_count ||
                        !validation_push(stack, &top,
                            eng->tables[instr->u32_imm]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_UNSUPPORTED;
                } else if (sub == 17) { /* table.fill: [i32 ref i32] -> [] */
                    wasm_valtype ref;
                    if (instr->u32_imm >= eng->table_count ||
                        !validation_pop_type(stack, &top, control,
                            eng->tables[instr->u32_imm]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32) ||
                        !validation_pop(stack, &top, control, &ref) ||
                        !validation_pop_type(stack, &top, control,
                            eng->tables[instr->u32_imm]->is_64 ?
                                WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return WASM_VALIDATION_INVALID;
                    if (ref != WASM_BOTTOM_TYPE &&
                        !global_type_is_compat(
                            eng, ref,
                            eng->tables[instr->u32_imm]->type_owner,
                            eng->tables[instr->u32_imm]->element_type, 0))
                        return WASM_VALIDATION_INVALID;
                } else {
                    return WASM_VALIDATION_UNSUPPORTED;
                }
                break;
            }
            default:
                return WASM_VALIDATION_UNSUPPORTED;
        }
    }
    return WASM_VALIDATION_INVALID;
}

static uint32_t direct_call_type_index(const waste_exec_engine *engine,
                                       uint32_t function_index) {
    if (function_index < engine->import_func_count)
        return engine->import_func_types[function_index];
    function_index -= engine->import_func_count;
    if (function_index >= engine->func_count) return UINT32_MAX;
    return engine->funcs[function_index].type_index;
}

static void record_instruction_resolution(const waste_exec_engine *engine,
                                          const exec_func *function,
                                          exec_instr *instruction) {
    instruction->validation_flags =
        EXEC_INSTR_VALIDATED | EXEC_INSTR_RESOLVED_IMMEDIATE;
    instruction->resolved_type_index = UINT32_MAX;
    instruction->resolved_target = UINT32_MAX;
    instruction->resolved_value_type = 0;

    switch (instruction->opcode) {
        case 0x02: case 0x03: case 0x04:
        case 0x06: case 0x1f:
            instruction->resolved_target = instruction->simd_op;
            instruction->validation_flags |= EXEC_INSTR_RESOLVED_BRANCH;
            if (instruction->block_type_index >= 0) {
                instruction->resolved_type_index =
                    (uint32_t)instruction->block_type_index;
                instruction->validation_flags |= EXEC_INSTR_RESOLVED_TYPE;
            } else if (instruction->has_block_result_type) {
                instruction->resolved_value_type =
                    instruction->block_result_type;
                instruction->validation_flags |= EXEC_INSTR_RESOLVED_TYPE;
            }
            break;
        case 0x0c: case 0x0d: case 0xd5: case 0xd6:
            instruction->resolved_target = instruction->u32_imm;
            instruction->validation_flags |= EXEC_INSTR_RESOLVED_BRANCH;
            break;
        case 0x0e:
            instruction->validation_flags |= EXEC_INSTR_RESOLVED_BRANCH;
            break;
        case 0xfb:
            if (instruction->simd_op == 0x18 ||
                instruction->simd_op == 0x19) {
                instruction->resolved_target = instruction->u32_imm;
                instruction->validation_flags |= EXEC_INSTR_RESOLVED_BRANCH;
            }
            break;
        case 0x10: case 0x12:
            instruction->resolved_type_index =
                direct_call_type_index(engine, instruction->u32_imm);
            instruction->validation_flags |= EXEC_INSTR_RESOLVED_TYPE;
            break;
        case 0x11: case 0x13: case 0x14: case 0x15:
            instruction->resolved_type_index = instruction->u32_imm;
            instruction->validation_flags |= EXEC_INSTR_RESOLVED_TYPE;
            break;
        case 0x20: case 0x21: case 0x22: {
            const exec_func_type *signature =
                &engine->types[function->type_index];
            if (validation_local_type(
                    signature, function, instruction->u32_imm,
                    &instruction->resolved_value_type))
                instruction->validation_flags |= EXEC_INSTR_RESOLVED_TYPE;
            break;
        }
        case 0x23: case 0x24:
            instruction->resolved_value_type =
                engine->globals[instruction->u32_imm]->value.type;
            instruction->validation_flags |= EXEC_INSTR_RESOLVED_TYPE;
            break;
        default:
            if (instruction->opcode >= 0x28 &&
                instruction->opcode <= 0x40 &&
                instruction->memory_index < engine->memory_count) {
                instruction->resolved_value_type =
                    engine->memories[instruction->memory_index]->is_64
                        ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32;
                instruction->validation_flags |= EXEC_INSTR_RESOLVED_TYPE;
            }
            break;
    }
}

wasm_validation_result wasm_validate_function(
    const waste_exec_engine *engine, exec_func *function,
    exec_instr *code, uint32_t code_size) {
    wasm_validation_result result;
    result.status = WASM_VALIDATION_INVALID;
    result.instruction = UINT32_MAX;
    result.opcode = UINT32_MAX;
    result.subopcode = UINT32_MAX;

    if (function->validated) {
        result.status = WASM_VALIDATION_VALID;
        return result;
    }

    result.status = validate_function_body(
        engine, function, code, code_size, &result.instruction);
    if (result.instruction < code_size) {
        result.opcode = code[result.instruction].opcode;
        result.subopcode = code[result.instruction].simd_op;
    }
    if (result.status == WASM_VALIDATION_VALID) {
        for (uint32_t i = 0; i < code_size; i++)
            record_instruction_resolution(engine, function, &code[i]);
        function->validated = 1;
    }
    return result;
}
