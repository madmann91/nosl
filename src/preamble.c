#include "preamble.h"
#include "type_table.h"
#include "ast.h"

#include <overture/mem_pool.h>

#include <assert.h>

struct preamble_builder {
    struct preamble* preamble;
    struct mem_pool* mem_pool;
    struct type_table* type_table;
};

static inline struct ast* alloc_builtin(struct preamble_builder* builder, const struct type* type) {
    struct ast* ast = MEM_POOL_ALLOC(*builder->mem_pool, struct ast);
    memset(ast, 0, sizeof(struct ast));
    ast->tag = AST_BUILTIN;
    ast->type = type;
    return ast;
}

static inline struct ast* append_decl(struct preamble_builder* builder, struct ast* decl) {
    if (!builder->preamble->first_decl)
        builder->preamble->first_decl = decl;
    if (builder->preamble->last_decl)
        builder->preamble->last_decl->next = decl;
    builder->preamble->last_decl = decl;
    return decl;
}

static struct ast* make_triple_constructor_from_float(struct preamble_builder* builder, enum prim_type_tag tag) {
    const struct type* float_type  = type_table_get_prim_type(builder->type_table, PRIM_TYPE_FLOAT);
    const struct type* triple_type = type_table_get_prim_type(builder->type_table, tag);
    struct func_param func_params[] = { { float_type, false } };
    return alloc_builtin(builder,
        type_table_get_func_type(builder->type_table, triple_type, func_params, 1, false));
}

static struct ast* make_triple_constructor_from_components(struct preamble_builder* builder, enum prim_type_tag tag) {
    const struct type* float_type  = type_table_get_prim_type(builder->type_table, PRIM_TYPE_FLOAT);
    const struct type* triple_type = type_table_get_prim_type(builder->type_table, tag);
    struct func_param func_params[] = {
        { float_type, false },
        { float_type, false },
        { float_type, false },
    };
    return alloc_builtin(builder,
        type_table_get_func_type(builder->type_table, triple_type, func_params, 3, false));
}

static struct ast* make_triple_constructor_from_triple(struct preamble_builder* builder, enum prim_type_tag tag, enum prim_type_tag other_tag) {
    const struct type* triple_type = type_table_get_prim_type(builder->type_table, tag);
    const struct type* other_type = type_table_get_prim_type(builder->type_table, other_tag);
    struct func_param func_params[] = { { other_type, false } };
    return alloc_builtin(builder,
        type_table_get_func_type(builder->type_table, triple_type, func_params, 1, false));
}

static struct ast* make_triple_constructor_with_space(struct preamble_builder* builder, enum prim_type_tag tag) {
    const struct type* float_type  = type_table_get_prim_type(builder->type_table, PRIM_TYPE_FLOAT);
    const struct type* string_type = type_table_get_prim_type(builder->type_table, PRIM_TYPE_STRING);
    const struct type* triple_type = type_table_get_prim_type(builder->type_table, tag);
    struct func_param func_params[] = {
        { string_type, false },
        { float_type, false },
        { float_type, false },
        { float_type, false },
    };
    return alloc_builtin(builder,
        type_table_get_func_type(builder->type_table, triple_type, func_params, 4, false));
}

static void make_triple_constructors(struct preamble_builder* builder, enum prim_type_tag tag) {
    const size_t constructor_count = 7;
    struct ast** constructors = mem_pool_alloc(
        builder->mem_pool, sizeof(struct ast*) * constructor_count, alignof(struct ast*));
    size_t i = 0;
    constructors[i++] = make_triple_constructor_from_float(builder, tag);
    constructors[i++] = make_triple_constructor_from_components(builder, tag);
    constructors[i++] = make_triple_constructor_with_space(builder, tag);
    constructors[i++] = make_triple_constructor_from_triple(builder, tag, PRIM_TYPE_COLOR);
    constructors[i++] = make_triple_constructor_from_triple(builder, tag, PRIM_TYPE_VECTOR);
    constructors[i++] = make_triple_constructor_from_triple(builder, tag, PRIM_TYPE_POINT);
    constructors[i++] = make_triple_constructor_from_triple(builder, tag, PRIM_TYPE_NORMAL);
    assert(i == constructor_count);
    builder->preamble->constructors[tag] = constructors;
    builder->preamble->constructor_count[tag] = constructor_count;
}

static struct ast* make_scalar_constructor(struct preamble_builder* builder, enum prim_type_tag tag, enum prim_type_tag other_tag) {
    const struct type* type = type_table_get_prim_type(builder->type_table, tag);
    const struct type* other_type  = type_table_get_prim_type(builder->type_table, other_tag);
    struct func_param func_params[1] = {
        { other_type, false }
    };
    return alloc_builtin(builder,
        type_table_get_func_type(builder->type_table, type, func_params, 1, false));
}

static void make_scalar_constructors(struct preamble_builder* builder, enum prim_type_tag tag) {
    const size_t constructor_count = 3;
    struct ast** constructors = mem_pool_alloc(
        builder->mem_pool, sizeof(struct ast*) * constructor_count, alignof(struct ast*));
    size_t i = 0;
    constructors[i++] = make_scalar_constructor(builder, tag, PRIM_TYPE_FLOAT);
    constructors[i++] = make_scalar_constructor(builder, tag, PRIM_TYPE_INT);
    constructors[i++] = make_scalar_constructor(builder, tag, PRIM_TYPE_BOOL);
    assert(i == constructor_count);
    builder->preamble->constructors[tag] = constructors;
    builder->preamble->constructor_count[tag] = constructor_count;
}

struct preamble preamble_build(struct mem_pool* mem_pool, struct type_table* type_table) {
    struct preamble preamble = {};
    struct preamble_builder builder = {
        .preamble = &preamble,
        .mem_pool = mem_pool,
        .type_table = type_table
    };

    make_scalar_constructors(&builder, PRIM_TYPE_BOOL);
    make_scalar_constructors(&builder, PRIM_TYPE_INT);
    make_scalar_constructors(&builder, PRIM_TYPE_FLOAT);

    make_triple_constructors(&builder, PRIM_TYPE_COLOR);
    make_triple_constructors(&builder, PRIM_TYPE_VECTOR);
    make_triple_constructors(&builder, PRIM_TYPE_POINT);
    make_triple_constructors(&builder, PRIM_TYPE_NORMAL);

    return preamble;
}
