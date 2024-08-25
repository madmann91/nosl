#include "builtins.h"
#include "type_table.h"
#include "ast.h"
#include "env.h"

#include <overture/mem_pool.h>
#include <overture/mem.h>

#include <assert.h>

struct builtins {
    struct ast* constructors[PRIM_TYPE_COUNT];
    struct ast* operators;
    struct mem_pool mem_pool;
    struct type_table* type_table;
};

static inline struct ast* alloc_builtin(struct builtins* builtins, const char* name, const struct type* type) {
    struct ast* ast = MEM_POOL_ALLOC(builtins->mem_pool, struct ast);
    memset(ast, 0, sizeof(struct ast));
    ast->tag = AST_BUILTIN;
    ast->type = type;
    ast->builtin.name = name;
    return ast;
}

static inline void append_builtin(struct ast** builtin_list, struct ast* builtin) {
    builtin->next = *builtin_list;
    *builtin_list = builtin;
}

static struct ast* make_triple_constructor_from_float(struct builtins* builtins, enum prim_type_tag tag) {
    const struct type* float_type  = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_FLOAT);
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    struct func_param func_params[] = { { float_type, false } };
    return alloc_builtin(builtins, prim_type_tag_to_string(tag),
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
    return alloc_builtin(builtins, prim_type_tag_to_string(tag),
        type_table_make_func_type(builtins->type_table, triple_type, func_params, 3, false));
}

static struct ast* make_triple_constructor_from_triple(struct builtins* builtins, enum prim_type_tag tag, enum prim_type_tag other_tag) {
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    const struct type* other_type = type_table_make_prim_type(builtins->type_table, other_tag);
    struct func_param func_params[] = { { other_type, false } };
    return alloc_builtin(builtins, prim_type_tag_to_string(tag),
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
    return alloc_builtin(builtins, prim_type_tag_to_string(tag),
        type_table_make_func_type(builtins->type_table, triple_type, func_params, 4, false));
}

static struct ast* make_scalar_constructor(struct builtins* builtins, enum prim_type_tag tag, enum prim_type_tag other_tag) {
    const struct type* type = type_table_make_prim_type(builtins->type_table, tag);
    const struct type* other_type  = type_table_make_prim_type(builtins->type_table, other_tag);
    struct func_param func_params[1] = { { other_type, false } };
    return alloc_builtin(builtins, prim_type_tag_to_string(tag),
        type_table_make_func_type(builtins->type_table, type, func_params, 1, false));
}

static struct ast* make_binary_operator(
    struct builtins* builtins,
    const char* name,
    enum prim_type_tag left_tag,
    enum prim_type_tag right_tag,
    enum prim_type_tag ret_tag)
{
    const struct type* left_type  = type_table_make_prim_type(builtins->type_table, left_tag);
    const struct type* right_type = type_table_make_prim_type(builtins->type_table, right_tag);
    const struct type* ret_type   = type_table_make_prim_type(builtins->type_table, ret_tag);
    struct func_param func_params[] = {
        { left_type,  false },
        { right_type, false }
    };
    return alloc_builtin(builtins, name,
        type_table_make_func_type(builtins->type_table, ret_type, func_params, 2, false));
}

static struct ast* make_unary_operator(
    struct builtins* builtins,
    const char* name,
    enum prim_type_tag arg_tag,
    enum prim_type_tag ret_tag,
    bool is_output)
{
    const struct type* arg_type = type_table_make_prim_type(builtins->type_table, arg_tag);
    const struct type* ret_type = type_table_make_prim_type(builtins->type_table, ret_tag);
    struct func_param func_params[] = { { arg_type, is_output } };
    return alloc_builtin(builtins, name,
        type_table_make_func_type(builtins->type_table, ret_type, func_params, 1, false));
}

static void register_triple_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_float(builtins, tag));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_components(builtins, tag));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_with_space(builtins, tag));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_COLOR));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_POINT));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_NORMAL));
}

static void register_scalar_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    append_builtin(&builtins->constructors[tag], make_scalar_constructor(builtins, tag, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->constructors[tag], make_scalar_constructor(builtins, tag, PRIM_TYPE_INT));
    append_builtin(&builtins->constructors[tag], make_scalar_constructor(builtins, tag, PRIM_TYPE_BOOL));
}

static void register_scalar_operators(struct builtins* builtins, enum prim_type_tag tag) {
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_FLOAT) {
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__add__", tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__sub__", tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__mul__", tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__div__", tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__mod__", tag, tag, tag));
        append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__pre_inc__", tag, tag, true));
        append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__pre_dec__", tag, tag, true));
        append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__post_inc__", tag, tag, true));
        append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__post_dec__", tag, tag, true));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__lt__", tag, tag, PRIM_TYPE_BOOL));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__le__", tag, tag, PRIM_TYPE_BOOL));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__gt__", tag, tag, PRIM_TYPE_BOOL));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__ge__", tag, tag, PRIM_TYPE_BOOL));
        append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__neg__", tag, tag, false));
    }
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_BOOL) {
        append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__not__", tag, tag, false));
        append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__compl__", tag, tag, false));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__bitand__", tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__xor__", tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__bitor__", tag, tag, tag));
    }
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__eq__", tag, tag, PRIM_TYPE_BOOL));
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__ne__", tag, tag, PRIM_TYPE_BOOL));
}

static void register_matrix_or_triple_operators(struct builtins* builtins, enum prim_type_tag tag) {
    enum prim_type_tag neg_or_sub_tag = tag != PRIM_TYPE_COLOR && tag != PRIM_TYPE_MATRIX ? PRIM_TYPE_VECTOR : tag;
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__add__", tag, tag, tag));
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__sub__", tag, tag, neg_or_sub_tag));
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__mul__", tag, tag, tag));
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__div__", tag, tag, tag));
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__eq__", tag, tag, PRIM_TYPE_BOOL));
    append_builtin(&builtins->operators, make_binary_operator(builtins, "__operator__ne__", tag, tag, PRIM_TYPE_BOOL));
    append_builtin(&builtins->operators, make_unary_operator(builtins, "__operator__neg__", tag, neg_or_sub_tag, false));
}

struct builtins* builtins_create(struct type_table* type_table) {
    struct builtins* builtins = xcalloc(1, sizeof(struct builtins));

    builtins->mem_pool = mem_pool_create();
    builtins->type_table = type_table;

    register_scalar_constructors(builtins, PRIM_TYPE_BOOL);
    register_scalar_constructors(builtins, PRIM_TYPE_INT);
    register_scalar_constructors(builtins, PRIM_TYPE_FLOAT);

    register_triple_constructors(builtins, PRIM_TYPE_COLOR);
    register_triple_constructors(builtins, PRIM_TYPE_VECTOR);
    register_triple_constructors(builtins, PRIM_TYPE_POINT);
    register_triple_constructors(builtins, PRIM_TYPE_NORMAL);

    register_scalar_operators(builtins, PRIM_TYPE_BOOL);
    register_scalar_operators(builtins, PRIM_TYPE_INT);
    register_scalar_operators(builtins, PRIM_TYPE_FLOAT);

    register_matrix_or_triple_operators(builtins, PRIM_TYPE_COLOR);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_VECTOR);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_POINT);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_COLOR);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_MATRIX);

    return builtins;
}

void builtins_destroy(struct builtins* builtins) {
    mem_pool_destroy(&builtins->mem_pool);
    free(builtins);
}

void builtins_populate_env(const struct builtins* builtins, struct env* env) {
    for (struct ast* operator = builtins->operators; operator; operator = operator->next)
        env_insert_symbol(env, operator->builtin.name, operator, true);
}

struct small_ast_vec builtins_list_constructors(const struct builtins* builtins, enum prim_type_tag tag) {
    struct small_ast_vec constructors;
    small_ast_vec_init(&constructors);
    for (struct ast* constructor = builtins->constructors[tag]; constructor; constructor = constructor->next)
        small_ast_vec_push(&constructors, &constructor);
    return constructors;
}
