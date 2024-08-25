#include "builtins.h"
#include "type_table.h"
#include "ast.h"

#include <overture/mem_pool.h>
#include <overture/mem.h>

#include <assert.h>

struct builtins {
    struct ast* constructors[PRIM_TYPE_COUNT];
    struct mem_pool mem_pool;
    struct type_table* type_table;
};

static inline struct ast* alloc_builtin(struct builtins* builtins, const struct type* type) {
    struct ast* ast = MEM_POOL_ALLOC(builtins->mem_pool, struct ast);
    memset(ast, 0, sizeof(struct ast));
    ast->tag = AST_BUILTIN;
    ast->type = type;
    return ast;
}

static struct ast* make_triple_constructor_from_float(struct builtins* builtins, enum prim_type_tag tag) {
    const struct type* float_type  = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_FLOAT);
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    struct func_param func_params[] = { { float_type, false } };
    return alloc_builtin(builtins,
        type_table_make_func_type(builtins->type_table, triple_type, func_params, 1, false));
}

static struct ast* make_triple_constructor_from_components(struct builtins* builtins, enum prim_type_tag tag) {
    const struct type* float_type  = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_FLOAT);
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    struct func_param func_params[] = {
        { float_type, false },
        { float_type, false },
        { float_type, false },
    };
    return alloc_builtin(builtins,
        type_table_make_func_type(builtins->type_table, triple_type, func_params, 3, false));
}

static struct ast* make_triple_constructor_from_triple(struct builtins* builtins, enum prim_type_tag tag, enum prim_type_tag other_tag) {
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    const struct type* other_type = type_table_make_prim_type(builtins->type_table, other_tag);
    struct func_param func_params[] = { { other_type, false } };
    return alloc_builtin(builtins,
        type_table_make_func_type(builtins->type_table, triple_type, func_params, 1, false));
}

static struct ast* make_triple_constructor_with_space(struct builtins* builtins, enum prim_type_tag tag) {
    const struct type* float_type  = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_FLOAT);
    const struct type* string_type = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_STRING);
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    struct func_param func_params[] = {
        { string_type, false },
        { float_type, false },
        { float_type, false },
        { float_type, false },
    };
    return alloc_builtin(builtins,
        type_table_make_func_type(builtins->type_table, triple_type, func_params, 4, false));
}

static struct ast* make_scalar_constructor(struct builtins* builtins, enum prim_type_tag tag, enum prim_type_tag other_tag) {
    const struct type* type = type_table_make_prim_type(builtins->type_table, tag);
    const struct type* other_type  = type_table_make_prim_type(builtins->type_table, other_tag);
    struct func_param func_params[1] = { { other_type, false } };
    return alloc_builtin(builtins,
        type_table_make_func_type(builtins->type_table, type, func_params, 1, false));
}

static void make_triple_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    struct ast* constructors[] = {
        make_triple_constructor_from_float(builtins, tag),
        make_triple_constructor_from_components(builtins, tag),
        make_triple_constructor_with_space(builtins, tag),
        make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_COLOR),
        make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_VECTOR),
        make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_POINT),
        make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_NORMAL)
    };
    builtins->constructors[tag] = ast_link(constructors, sizeof(constructors) / sizeof(constructors[0]));
}

static void make_scalar_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    struct ast* constructors[] = {
        make_scalar_constructor(builtins, tag, PRIM_TYPE_FLOAT),
        make_scalar_constructor(builtins, tag, PRIM_TYPE_INT),
        make_scalar_constructor(builtins, tag, PRIM_TYPE_BOOL)
    };
    builtins->constructors[tag] = ast_link(constructors, sizeof(constructors) / sizeof(constructors[0]));
}

struct builtins* builtins_create(struct type_table* type_table) {
    struct builtins* builtins = xcalloc(1, sizeof(struct builtins));

    builtins->mem_pool = mem_pool_create();
    builtins->type_table = type_table;

    make_scalar_constructors(builtins, PRIM_TYPE_BOOL);
    make_scalar_constructors(builtins, PRIM_TYPE_INT);
    make_scalar_constructors(builtins, PRIM_TYPE_FLOAT);

    make_triple_constructors(builtins, PRIM_TYPE_COLOR);
    make_triple_constructors(builtins, PRIM_TYPE_VECTOR);
    make_triple_constructors(builtins, PRIM_TYPE_POINT);
    make_triple_constructors(builtins, PRIM_TYPE_NORMAL);

    return builtins;
}

void builtins_destroy(struct builtins* builtins) {
    mem_pool_destroy(&builtins->mem_pool);
    free(builtins);
}

void builtins_populate_env(const struct builtins* builtins, struct env* env) {
    // TODO
}

struct small_ast_vec builtins_list_constructors(const struct builtins* builtins, enum prim_type_tag tag) {
    struct small_ast_vec constructors;
    small_ast_vec_init(&constructors);
    for (struct ast* constructor = builtins->constructors[tag]; constructor; constructor = constructor->next)
        small_ast_vec_push(&constructors, &constructor);
    return constructors;
}
