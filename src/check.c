#include "check.h"
#include "env.h"
#include "ast.h"
#include "type_table.h"

#include <overture/log.h>
#include <overture/mem_pool.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct type_checker {
    struct mem_pool* mem_pool;
    struct type_table* type_table;
    struct env* env;
    struct log* log;
};

struct const_eval {
    bool is_const;
    union {
        int int_val;
        float float_val;
    };
};

static void check_stmt(struct type_checker*, struct ast*);
static const struct type* check_type(struct type_checker*, struct ast*);
static const struct type* check_expr(struct type_checker*, struct ast*, const struct type*);

static inline void insert_symbol(
    struct type_checker* type_checker,
    const char* name,
    struct ast* ast,
    bool allow_overload)
{
    struct ast* old_ast = env_find_one_symbol(type_checker->env, name);
    if (env_insert_symbol(type_checker->env, name, ast, allow_overload)) {
        if (!old_ast || allow_overload)
            return;
        log_warn(type_checker->log, &ast->source_range, "symbol '%s' shadows previous definition", name);
    } else {
        log_error(type_checker->log, &ast->source_range, "redefinition for symbol '%s'", name);
    }
    log_note(type_checker->log, &old_ast->source_range, "previously declared here");
}

static inline void insert_cast(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* type)
{
    struct ast* copy = MEM_POOL_ALLOC(*type_checker->mem_pool, struct ast);
    memcpy(copy, ast, sizeof(struct ast));
    copy->next = NULL;
    ast->tag = AST_CAST_EXPR;
    ast->cast_expr.type = NULL;
    ast->cast_expr.value = ast;
    ast->type = type;
}

static inline const struct type* coerce_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    assert(ast->type);
    if (!expected_type || ast->type == expected_type)
        return ast->type;

    if (type_is_implicitly_convertible_to(ast->type, expected_type)) {
        insert_cast(type_checker, ast, expected_type);
    } else if (expected_type->tag != TYPE_ERROR && ast->type->tag != TYPE_ERROR) {
        struct type_print_options options = { .disable_colors = type_checker->log->disable_colors };
        char* type_string = type_to_string(ast->type, &options);
        char* expected_type_string = type_to_string(expected_type, &options);
        log_error(type_checker->log, &ast->source_range, "expected type '%s', but got type '%s'",
            expected_type_string, type_string);
        free(type_string);
        free(expected_type_string);
    }
    return expected_type;
}

static inline struct const_eval eval_const_int(struct type_checker*, struct ast* ast) {
    // TODO: Use IR emitter for constants
    return (struct const_eval) {
        .is_const = ast->tag == AST_INT_LITERAL,
        .int_val = ast->int_literal
    };
}

static const struct type* check_prim_type(struct type_checker* type_checker, struct ast* ast) {
    const struct type* prim_type = type_table_get_prim_type(type_checker->type_table, ast->prim_type.tag);
    if (!ast->prim_type.is_closure)
        return prim_type;
    return type_table_get_closure_type(type_checker->type_table, prim_type);
}

static const struct type* check_shader_type(struct type_checker* type_checker, struct ast* ast) {
    return type_table_get_shader_type(type_checker->type_table, ast->shader_type.tag);
}

static const struct type* check_named_type(struct type_checker*, struct ast*) {
    assert(false && "not implemented");
    return NULL;
}

static const struct type* check_type(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_PRIM_TYPE:   return check_prim_type(type_checker, ast);
        case AST_SHADER_TYPE: return check_shader_type(type_checker, ast);
        case AST_NAMED_TYPE:  return check_named_type(type_checker, ast);
        default:
            assert(false && "invalid type");
            return type_table_get_error_type(type_checker->type_table);
    }
}

static const struct type* check_array_dim(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* elem_type,
    bool allow_unsized)
{
    if (!ast)
        return elem_type;

    if (ast->tag == AST_UNSIZED_DIM) {
        if (!allow_unsized) {
            log_error(type_checker->log, &ast->source_range,
                "unsized arrays are only allowed as function or shader parameters");
        }
        return type_table_get_unsized_array_type(type_checker->type_table, elem_type);
    } else {
        check_expr(type_checker, ast, type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_INT));
        struct const_eval const_eval = eval_const_int(type_checker, ast);
        if (!const_eval.is_const || const_eval.int_val <= 0) {
            log_error(type_checker->log, &ast->source_range,
                "array dimension must be constant and strictly positive");
            const_eval.int_val = 1;
        }
        return type_table_get_sized_array_type(type_checker->type_table, elem_type, (size_t)const_eval.int_val);
    }
}

static const struct type* check_var(struct type_checker* type_checker, struct ast* ast, const struct type* type) {
    insert_symbol(type_checker, ast->var.name, ast, false);
    type = check_array_dim(type_checker, ast->var.dim, type, false);
    if (ast->var.init)
        check_expr(type_checker, ast->var.init, type);
    return ast->type = type;
}

static void check_var_decl(struct type_checker* type_checker, struct ast* ast) {
    const struct type* type = check_type(type_checker, ast->var_decl.type);
    for (struct ast* var = ast->var_decl.vars; var; var = var->next)
        check_var(type_checker, var, type);
}

static void check_block_without_scope(struct type_checker* type_checker, struct ast* ast) {
    assert(ast->tag == AST_BLOCK);
    for (struct ast* stmt = ast->block.stmts; stmt; stmt = stmt->next)
        check_stmt(type_checker, stmt);
}

static void check_block(struct type_checker* type_checker, struct ast* ast) {
    env_push_scope(type_checker->env, ast);
    check_block_without_scope(type_checker, ast);
    env_pop_scope(type_checker->env);
}

static void check_param(struct type_checker* type_checker, struct ast* ast) {
    ast->type = check_type(type_checker, ast->param.type);
    ast->type = check_array_dim(type_checker, ast->param.dim, ast->type, true);
    insert_symbol(type_checker, ast->param.name, ast, false);
}

static void check_params(struct type_checker* type_checker, struct ast* ast) {
    for (; ast; ast = ast->next)
        check_param(type_checker, ast);
}

static void check_shader_or_func_decl(struct type_checker* type_checker, struct ast* ast) {
    assert(ast->tag == AST_FUNC_DECL || ast->tag == AST_SHADER_DECL);
    bool is_shader_decl = ast->tag == AST_SHADER_DECL;

    env_push_scope(type_checker->env, ast);

    struct ast* params = is_shader_decl ? ast->shader_decl.params : ast->func_decl.params;
    check_params(type_checker, params);

    size_t param_count = ast_list_size(params);
    struct type* func_type = type_table_create_func_type(type_checker->type_table, param_count);
    func_type->func_type.ret_type = check_type(type_checker,
        is_shader_decl ? ast->shader_decl.type : ast->func_decl.ret_type);

    size_t param_index = 0;
    for (struct ast* param = params; param; param = param->next) {
        struct func_param* func_param = &func_type->func_type.params[param_index++];
        func_param->is_output = param->param.is_output;
        func_param->name = param->param.name;
        func_param->type = param->type;
    }
    type_table_finalize_type(type_checker->type_table, func_type);
    ast->type = func_type;

    check_block_without_scope(type_checker, ast->func_decl.body);
    env_pop_scope(type_checker->env);

    insert_symbol(type_checker, ast->func_decl.name, ast, true);
}

static void check_return_stmt(struct type_checker* type_checker, struct ast* ast) {
    struct ast* shader_or_func = env_find_enclosing_shader_or_func(type_checker->env);
    assert(shader_or_func);
    assert(shader_or_func->type && shader_or_func->type->tag == TYPE_FUNC);

    const struct type* ret_type = shader_or_func->type->func_type.ret_type;
    bool takes_val = ret_type->tag != TYPE_SHADER && !type_is_void(ret_type);
    if (ast->return_stmt.value) {
        if (!takes_val) {
            log_error(type_checker->log, &ast->source_range, "%s '%s' cannot return a value",
                ret_type->tag == TYPE_SHADER ? "shader" : "function",
                ast_decl_name(shader_or_func));
        } else {
            check_expr(type_checker, ast->return_stmt.value, ret_type);
        }
    } else if (takes_val) {
        log_error(type_checker->log, &ast->source_range, "missing return value");
    }

    ast->return_stmt.shader_or_func = shader_or_func;
}

static void check_break_or_continue_stmt(struct type_checker* type_checker, struct ast* ast) {
    struct ast* loop = env_find_enclosing_loop(type_checker->env);
    if (!loop) {
        log_error(type_checker->log, &ast->source_range, "'%s' is not allowed outside of loops",
            ast->tag == AST_BREAK_STMT ? "break" : "continue");
    }
    ast->break_stmt.loop = loop;
}

static void check_while_loop(struct type_checker* type_checker, struct ast* ast) {
    check_expr(type_checker, ast->while_loop.cond,
        type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_BOOL));
    env_push_scope(type_checker->env, ast);
    check_stmt(type_checker, ast->while_loop.body);
    env_pop_scope(type_checker->env);
}

static void check_for_loop(struct type_checker* type_checker, struct ast* ast) {
    env_push_scope(type_checker->env, ast);
    check_stmt(type_checker, ast->for_loop.init);
    check_expr(type_checker, ast->for_loop.cond,
        type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_BOOL));
    check_expr(type_checker, ast->for_loop.inc, NULL);
    check_stmt(type_checker, ast->for_loop.body);
    env_pop_scope(type_checker->env);
}

static void check_do_while_loop(struct type_checker* type_checker, struct ast* ast) {
    env_push_scope(type_checker->env, ast);
    check_stmt(type_checker, ast->do_while_loop.body);
    env_pop_scope(type_checker->env);
    check_expr(type_checker, ast->do_while_loop.cond,
        type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_BOOL));
}

static void check_if_stmt(struct type_checker* type_checker, struct ast* ast) {
    check_expr(type_checker, ast->if_stmt.cond,
        type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_BOOL));
    check_stmt(type_checker, ast->if_stmt.then_stmt);
    if (ast->if_stmt.else_stmt)
        check_stmt(type_checker, ast->if_stmt.else_stmt);
}

static void check_stmt(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_EMPTY_STMT:    break;
        case AST_BLOCK:         check_block(type_checker, ast);               break;
        case AST_VAR_DECL:      check_var_decl(type_checker, ast);            break;
        case AST_FUNC_DECL:     check_shader_or_func_decl(type_checker, ast); break;
        case AST_RETURN_STMT:   check_return_stmt(type_checker, ast);         break;
        case AST_WHILE_LOOP:    check_while_loop(type_checker, ast);          break;
        case AST_FOR_LOOP:      check_for_loop(type_checker, ast);            break;
        case AST_DO_WHILE_LOOP: check_do_while_loop(type_checker, ast);       break;
        case AST_IF_STMT:       check_if_stmt(type_checker, ast);             break;
        case AST_BREAK_STMT:
        case AST_CONTINUE_STMT:
            check_break_or_continue_stmt(type_checker, ast);
            break;
        case AST_IDENT_EXPR:
        case AST_PAREN_EXPR:
        case AST_BINARY_EXPR:
        case AST_UNARY_EXPR:
        case AST_CALL_EXPR:
        case AST_CONSTRUCT_EXPR:
        case AST_COMPOUND_EXPR:
        case AST_COMPOUND_INIT:
        case AST_TERNARY_EXPR:
        case AST_INDEX_EXPR:
        case AST_PROJ_EXPR:
        case AST_CAST_EXPR:
            check_expr(type_checker, ast, NULL);
            break;
        default:
            assert(false && "invalid statement");
            break;
    }
}

static const struct type* check_ident_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    struct ast* symbol = env_find_one_symbol(type_checker->env, ast->ident_expr.name);
    if (!symbol) {
        struct small_ast_vec all_symbols = env_find_all_symbols(type_checker->env, ast->ident_expr.name);
        log_error(type_checker->log, &ast->source_range,
            all_symbols.elem_count > 0
                ? "identifier '%s' is overloaded"
                : "unknown identifier '%s'",
            ast->ident_expr.name);
        small_ast_vec_destroy(&all_symbols);
        return type_table_get_error_type(type_checker->type_table);
    }
    return symbol->type;
}

static const struct type* check_binary_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_unary_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_call_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_construct_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_compound_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_compound_init(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_ternary_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_index_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_proj_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_cast_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    switch (ast->tag) {
        case AST_BOOL_LITERAL:
        case AST_INT_LITERAL:
        case AST_FLOAT_LITERAL:
        case AST_STRING_LITERAL: {
            ast->type = type_table_get_prim_type(type_checker->type_table,
                ast_literal_tag_to_prim_type_tag(ast->tag));
            return coerce_expr(type_checker, ast, expected_type);
        }
        case AST_IDENT_EXPR:     return check_ident_expr(type_checker, ast, expected_type);
        case AST_BINARY_EXPR:    return check_binary_expr(type_checker, ast, expected_type);
        case AST_UNARY_EXPR:     return check_unary_expr(type_checker, ast, expected_type);
        case AST_CALL_EXPR:      return check_call_expr(type_checker, ast, expected_type);
        case AST_CONSTRUCT_EXPR: return check_construct_expr(type_checker, ast, expected_type);
        case AST_PAREN_EXPR:     return check_expr(type_checker, ast->paren_expr.inner_expr, expected_type);
        case AST_COMPOUND_EXPR:  return check_compound_expr(type_checker, ast, expected_type);
        case AST_COMPOUND_INIT:  return check_compound_init(type_checker, ast, expected_type);
        case AST_TERNARY_EXPR:   return check_ternary_expr(type_checker, ast, expected_type);
        case AST_INDEX_EXPR:     return check_index_expr(type_checker, ast, expected_type);
        case AST_PROJ_EXPR:      return check_proj_expr(type_checker, ast, expected_type);
        case AST_CAST_EXPR:      return check_cast_expr(type_checker, ast, expected_type);
        default:
            assert(false && "invalid expression");
            return type_table_get_error_type(type_checker->type_table);
    }
}

static void check_struct_decl(struct type_checker* type_checker, struct ast* ast) {
    insert_symbol(type_checker, ast->struct_decl.name, ast, false);
    env_push_scope(type_checker->env, ast);
    struct type* struct_type = type_table_create_struct_type(type_checker->type_table, ast_field_count(ast));
    size_t field_index = 0;
    for (struct ast* field = ast->struct_decl.fields; field; field = field->next) {
        assert(field->tag == AST_VAR_DECL);
        const struct type* field_type = check_type(type_checker, field->var_decl.type);
        for (struct ast* var = field->var_decl.vars; var; var = var->next) {
            struct struct_field* struct_field = &struct_type->struct_type.fields[field_index++];
            check_var(type_checker, var, field_type);
            struct_field->name = var->var.name;
        }
    }
    env_pop_scope(type_checker->env);
}

static void check_top_level_decl(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_STRUCT_DECL:
            check_struct_decl(type_checker, ast);
            break;
        case AST_SHADER_DECL:
        case AST_FUNC_DECL:
            check_shader_or_func_decl(type_checker, ast);
            break;
        default:
            assert(false && "invalid declaration");
            break;
    }
}

void check(struct mem_pool* mem_pool, struct ast* ast, struct log* log) {
    struct type_checker type_checker = {
        .mem_pool = mem_pool,
        .type_table = type_table_create(),
        .env = env_create(),
        .log = log
    };
    for (; ast; ast = ast->next)
        check_top_level_decl(&type_checker, ast);
    env_destroy(type_checker.env);
    type_table_destroy(type_checker.type_table);
}
