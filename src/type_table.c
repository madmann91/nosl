#include "type_table.h"
#include "type.h"

#include <overture/set.h>
#include <overture/str_pool.h>
#include <overture/mem_pool.h>
#include <overture/hash.h>
#include <overture/mem.h>

#include <assert.h>

static inline uint32_t hash_type(uint32_t h, const struct type* const* type_ptr) {
    const struct type* type = *type_ptr;
    h = hash_uint32(h, type->tag);
    switch (type->tag) {
        case TYPE_PRIM:   h = hash_uint32(h, type->prim_type); break;
        case TYPE_SHADER: h = hash_uint32(h, type->shader_type); break;
        case TYPE_ARRAY:
            h = hash_uint64(h, type->array_type.elem_count);
            h = hash_uint64(h, type->array_type.elem_type->id);
            break;
        case TYPE_FUNC:
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
        case TYPE_PRIM:   return type->prim_type == other->prim_type;
        case TYPE_SHADER: return type->shader_type == other->shader_type;
        case TYPE_ARRAY:
            return
                type->array_type.elem_count == other->array_type.elem_count &&
                type->array_type.elem_type == other->array_type.elem_type;
        default:
            return type == other;
    }
}

SET_DEFINE(type_set, const struct type*, hash_type, is_type_equal, PRIVATE)

struct type_table {
    struct type_set types;
    struct str_pool* str_pool;
    struct mem_pool mem_pool;
};

struct type_table* type_table_create(void) {
    struct type_table* type_table = xmalloc(sizeof(struct type_table));
    type_table->types = type_set_create();
    type_table->str_pool = str_pool_create();
    type_table->mem_pool = mem_pool_create();
    return type_table;
}

void type_table_destroy(struct type_table* type_table) {
    type_set_destroy(&type_table->types);
    str_pool_destroy(type_table->str_pool);
    mem_pool_destroy(&type_table->mem_pool);
    free(type_table);
}

static inline struct type* insert_type(struct type_table* type_table, struct type* type) {
    [[maybe_unused]] bool was_inserted = type_set_insert(&type_table->types, (const struct type* const*)&type);
    assert(was_inserted);
    return type;
}

const struct type* type_table_insert(struct type_table* type_table, const struct type* type) {
    assert(!type_is_nominal(type));
    const struct type* const* type_ptr = type_set_find(&type_table->types, &type);
    if (type_ptr)
        return *type_ptr;

    struct type* new_type = MEM_POOL_ALLOC(type_table->mem_pool, struct type);
    memcpy(new_type, type, sizeof(struct type));
    new_type->id = type_table->types.elem_count;
    return insert_type(type_table, new_type);
}

struct type* type_table_create_nominal_type(struct type_table* type_table, enum type_tag tag) {
    assert(type_tag_is_nominal(tag));
    struct type* type = mem_pool_alloc(&type_table->mem_pool, sizeof(struct type), alignof(struct type));
    memset(type, 0, sizeof(struct type));
    type->id = type_table->types.elem_count;
    type->tag = tag;
    return insert_type(type_table, type);
}

static inline void finalize_struct_type(struct type_table* type_table, struct type* type) {
    struct struct_field* fields = mem_pool_alloc(&type_table->mem_pool,
        sizeof(struct struct_field) * type->struct_type.field_count,
        alignof(struct struct_field));
    for (size_t i = 0; i < type->struct_type.field_count; ++i) {
        fields[i].type = type->struct_type.fields[i].type;
        fields[i].name = str_pool_insert(type_table->str_pool, type->struct_type.fields[i].name);
    }
    type->struct_type.fields = fields;
}

static inline void finalize_func_type(struct type_table* type_table, struct type* type) {
    struct func_param* params = mem_pool_alloc(&type_table->mem_pool,
        sizeof(struct func_param) * type->func_type.param_count,
        alignof(struct func_param));
    for (size_t i = 0; i < type->func_type.param_count; ++i) {
        params[i].type = type->func_type.params[i].type;
        params[i].name = str_pool_insert(type_table->str_pool, type->func_type.params[i].name);
        params[i].is_output = type->func_type.params[i].is_output;
    }
    type->func_type.params = params;
}

void type_table_finalize(struct type_table* type_table, struct type* type) {
    assert(type_is_nominal(type));
    assert(type_set_find(&type_table->types, (const struct type* const*)&type));
    if (type->tag == TYPE_STRUCT)
        finalize_struct_type(type_table, type);
    else if (type->tag == TYPE_FUNC)
        finalize_func_type(type_table, type);
    else
        assert(false && "invalid type");
}
