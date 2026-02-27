#include "type_table.h"
#include "type.h"

#include <overture/set.h>
#include <overture/str_pool.h>
#include <overture/mem_pool.h>
#include <overture/hash.h>
#include <overture/mem.h>

#include <assert.h>
#include <string.h>

static inline uint32_t hash_type(uint32_t h, const struct type* const* type_ptr) {
    const struct type* type = *type_ptr;
    h = hash_uint32(h, type->tag);
    switch (type->tag) {
        case TYPE_ERROR:  break;
        case TYPE_PRIM:    h = hash_uint32(h, type->prim_type);                   break;
        case TYPE_SHADER:  h = hash_uint32(h, type->shader_type);                 break;
        case TYPE_CLOSURE: h = hash_uint64(h, type->closure_type.inner_type->id); break;
        case TYPE_ARRAY:
            h = hash_uint64(h, type->array_type.elem_count);
            h = hash_uint64(h, type->array_type.elem_type->id);
            break;
        case TYPE_FUNC:
            h = hash_uint8(h, type->func_type.has_ellipsis);
            h = hash_uint64(h, type->func_type.ret_type->id);
            h = hash_uint64(h, type->func_type.param_count);
            for (size_t i = 0; i < type->func_type.param_count; ++i) {
                h = hash_uint64(h, type->func_type.params[i].type->id);
                h = hash_uint8(h, type->func_type.params[i].is_output);
            }
            break;
        case TYPE_COMPOUND:
            h = hash_uint64(h, type->compound_type.elem_count);
            for (size_t i = 0; i < type->compound_type.elem_count; ++i)
                h = hash_uint64(h, type->compound_type.elem_types[i]->id);
            break;
        case TYPE_STRUCT:
            return type->id;
        default:
            assert(false && "invalid type");
            break;
    }
    return h;
}

static inline bool is_type_equal(
    const struct type* const* type_ptr,
    const struct type* const* other_ptr)
{
    const struct type* type = *type_ptr;
    const struct type* other = *other_ptr;
    if (type->tag != other->tag)
        return false;
    switch (type->tag) {
        case TYPE_ERROR:   return true;
        case TYPE_PRIM:    return type->prim_type == other->prim_type;
        case TYPE_SHADER:  return type->shader_type == other->shader_type;
        case TYPE_CLOSURE: return type->closure_type.inner_type == other->closure_type.inner_type;
        case TYPE_ARRAY:
            return
                type->array_type.elem_count == other->array_type.elem_count &&
                type->array_type.elem_type == other->array_type.elem_type;
        case TYPE_FUNC:
            if (type->func_type.ret_type != other->func_type.ret_type ||
                type->func_type.has_ellipsis != other->func_type.has_ellipsis ||
                type->func_type.param_count != other->func_type.param_count)
            {
                return false;
            }
            for (size_t i = 0; i < type->func_type.param_count; ++i) {
                if (type->func_type.params[i].type != other->func_type.params[i].type ||
                    type->func_type.params[i].is_output != other->func_type.params[i].is_output)
                {
                    return false;
                }
            }
            return true;
        case TYPE_COMPOUND:
            if (type->compound_type.elem_count != other->compound_type.elem_count)
                return false;
            for (size_t i = 0; i < type->compound_type.elem_count; ++i) {
                if (type->compound_type.elem_types[i] != other->compound_type.elem_types[i])
                    return false;
            }
            return true;
        default:
            return type == other;
    }
}

SET_DEFINE(type_set, const struct type*, hash_type, is_type_equal, PRIVATE)

struct type_table {
    struct type_set types;
    struct mem_pool* mem_pool;
    struct str_pool* str_pool;
};

struct type_table* type_table_create(struct mem_pool* mem_pool) {
    struct type_table* type_table = xmalloc(sizeof(struct type_table));
    type_table->types = type_set_create();
    type_table->mem_pool = mem_pool;
    type_table->str_pool = str_pool_create(mem_pool);
    return type_table;
}

void type_table_destroy(struct type_table* type_table) {
    str_pool_destroy(type_table->str_pool);
    type_set_destroy(&type_table->types);
    free(type_table);
}

static inline struct type* register_type(struct type_table* type_table, struct type* type) {
    [[maybe_unused]] bool was_inserted = type_set_insert(&type_table->types, (const struct type* const*)&type);
    assert(was_inserted);
    return type;
}

static inline struct func_param* copy_func_params(
    struct mem_pool* mem_pool,
    const struct func_param* params,
    size_t param_count)
{
    struct func_param* new_params = MEM_POOL_ALLOC_ARRAY(*mem_pool, param_count, struct func_param);
    xmemcpy(new_params, params, sizeof(struct func_param) * param_count);
    return new_params;
}

static inline const struct type** copy_types(
    struct mem_pool* mem_pool,
    const struct type* const* types,
    size_t type_count)
{
    const struct type** new_types = MEM_POOL_ALLOC_ARRAY(*mem_pool, type_count, const struct type*);
    xmemcpy(new_types, types, sizeof(const struct type*) * type_count);
    return new_types;
}

static const struct type* insert_type(struct type_table* type_table, const struct type* type) {
    assert(type->tag != TYPE_STRUCT);
    const struct type* const* type_ptr = type_set_find(&type_table->types, &type);
    if (type_ptr)
        return *type_ptr;

    struct type* new_type = MEM_POOL_ALLOC(*type_table->mem_pool, struct type);
    memcpy(new_type, type, sizeof(struct type));
    new_type->id = type_table->types.elem_count;
    if (type->tag == TYPE_FUNC) {
        new_type->func_type.params = copy_func_params(
            type_table->mem_pool,
            type->func_type.params,
            type->func_type.param_count);
    } else if (type->tag == TYPE_COMPOUND) {
        new_type->compound_type.elem_types = copy_types(
            type_table->mem_pool,
            type->compound_type.elem_types,
            type->compound_type.elem_count);
    }
    return register_type(type_table, new_type);
}

struct type* type_table_create_struct_type(struct type_table* type_table, size_t field_count) {
    struct type* type = MEM_POOL_ALLOC(*type_table->mem_pool, struct type);
    memset(type, 0, sizeof(struct type));
    type->id = type_table->types.elem_count;
    type->tag = TYPE_STRUCT;
    type->struct_type.fields = MEM_POOL_ALLOC_ARRAY(*type_table->mem_pool, field_count, struct struct_field);
    memset(type->struct_type.fields, 0, sizeof(struct struct_field) * field_count);
    type->struct_type.field_count = field_count;
    register_type(type_table, type);
    return type;
} 

void type_table_finalize_struct_type(struct type_table* type_table, struct type* type) {
    assert(type->tag == TYPE_STRUCT);
    assert(type_set_find(&type_table->types, (const struct type* const*)&type) != NULL);
    for (size_t i = 0; i < type->struct_type.field_count; ++i)
        type->struct_type.fields[i].name = str_pool_insert(type_table->str_pool, type->struct_type.fields[i].name);
    type->struct_type.name = str_pool_insert(type_table->str_pool, type->struct_type.name);
}

const struct type* type_table_make_error_type(struct type_table* type_table) {
    return insert_type(type_table, &(struct type) { .tag = TYPE_ERROR });
}

const struct type* type_table_make_prim_type(struct type_table* type_table, enum prim_type prim_type) {
    return insert_type(type_table, &(struct type) { .tag = TYPE_PRIM, .prim_type = prim_type });
}

const struct type* type_table_make_shader_type(struct type_table* type_table, enum shader_type shader_type) {
    return insert_type(type_table, &(struct type) { .tag = TYPE_SHADER, .shader_type = shader_type });
}

const struct type* type_table_make_closure_type(
    struct type_table* type_table,
    const struct type* inner_type)
{
    return insert_type(type_table, &(struct type) {
        .tag = TYPE_CLOSURE,
        .closure_type.inner_type = inner_type
    });
}

const struct type* type_table_make_sized_array_type(
    struct type_table* type_table,
    const struct type* elem_type,
    size_t elem_count)
{
    assert(elem_count > 0);
    return insert_type(type_table, &(struct type) {
        .tag = TYPE_ARRAY,
        .array_type = {
            .elem_type = elem_type,
            .elem_count = elem_count
        }
    });
}

const struct type* type_table_make_unsized_array_type(
    struct type_table* type_table,
    const struct type* elem_type)
{
    return insert_type(type_table, &(struct type) {
        .tag = TYPE_ARRAY,
        .array_type.elem_type = elem_type
    });
}

const struct type* type_table_make_func_type(
    struct type_table* type_table,
    const struct type* ret_type,
    const struct func_param* params,
    size_t param_count,
    bool has_ellipsis)
{
    return insert_type(type_table, &(struct type) {
        .tag = TYPE_FUNC,
        .func_type = {
            .ret_type = ret_type,
            .params = params,
            .param_count = param_count,
            .has_ellipsis = has_ellipsis
        }
    });
}

const struct type* type_table_make_compound_type(
    struct type_table* type_table,
    const struct type* const* elem_types,
    size_t elem_count)
{
    return insert_type(type_table, &(struct type) {
        .tag = TYPE_COMPOUND,
        .compound_type = {
            .elem_types = elem_types,
            .elem_count = elem_count
        }
    });
}

const struct type* type_table_make_constructor_type(
    struct type_table* type_table,
    const struct type* struct_type)
{
    assert(struct_type->tag == TYPE_STRUCT);
    struct small_func_param_vec func_params;
    small_func_param_vec_init(&func_params);
    for (size_t i = 0; i < struct_type->struct_type.field_count; ++i) {
        small_func_param_vec_push(&func_params, &(struct func_param) {
            .type = struct_type->struct_type.fields[i].type,
            .is_output = false
        });
    }
    const struct type* constructor_type = type_table_make_func_type(type_table,
        struct_type, func_params.elems, func_params.elem_count, false);
    small_func_param_vec_destroy(&func_params);
    return constructor_type;
}
