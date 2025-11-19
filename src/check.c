#include "check.h"
#include "env.h"
#include "ast.h"
#include "type_table.h"
#include "builtins.h"

#include <overture/log.h>
#include <overture/mem_pool.h>
#include <overture/mem_stream.h>
#include <overture/vec.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct type_checker {
    const struct builtins* builtins;
    struct type_print_options type_print_options;
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

static inline char* call_signature_to_string(
    const struct type_checker* type_checker,
    const struct type* ret_type,
    struct ast* args)
{
    struct mem_stream mem_stream;
    mem_stream_init(&mem_stream);
    if (ret_type) {
        type_print(mem_stream.file, ret_type, &type_checker->type_print_options);
        fputc(' ', mem_stream.file);
    }
    fputc('(', mem_stream.file);
    for (struct ast* arg = args; arg; arg = arg->next) {
        type_print(mem_stream.file, arg->type, &type_checker->type_print_options);
        if (arg->next)
            fputs(", ", mem_stream.file);
    }
    fputc(')', mem_stream.file);
    mem_stream_destroy(&mem_stream);
    return mem_stream.buf;
}

static inline void report_invalid_type(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* type,
    const struct type* expected_type)
{
    if (expected_type->tag == TYPE_ERROR || type->tag == TYPE_ERROR)
        return;

    char* type_string = type_to_string(type, &type_checker->type_print_options);
    char* expected_type_string = type_to_string(expected_type, &type_checker->type_print_options);
    log_error(type_checker->log, loc, "expected type '%s', but got type '%s'",
        expected_type_string, type_string);
    free(type_string);
    free(expected_type_string);
}

static inline void report_invalid_type_with_msg(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* type,
    const char* expected_type_string)
{
    if (type->tag == TYPE_ERROR)
        return;

    char* type_string = type_to_string(type, &type_checker->type_print_options);
    log_error(type_checker->log, loc, "expected %s type, but got type '%s'",
        expected_type_string, type_string);
    free(type_string);
}

static inline void report_previous_location(struct type_checker* type_checker, struct file_loc* loc) {
    log_note(type_checker->log, loc, "previously declared here");
}

static inline void report_overload_error(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const char* msg,
    const char* func_name,
    struct ast** candidates,
    size_t candidate_count,
    const struct type* ret_type,
    struct ast* args)
{
    char* signature_string = call_signature_to_string(type_checker, ret_type, args);
    log_error(type_checker->log, loc, "%s call to '%s' with signature '%s'",
        msg, func_name, signature_string);
    free(signature_string);
    for (size_t i = 0; i < candidate_count; ++i) {
        char* candidate_type_string = type_to_string(candidates[i]->type, &type_checker->type_print_options);
        log_note(type_checker->log, &candidates[i]->loc, "candidate with type '%s'", candidate_type_string);
        free(candidate_type_string);
    }
}

static inline void report_missing_field(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* type,
    size_t field_index,
    bool is_error)
{
    // Only report one missing field, to avoid bloating the log
    assert(type->tag == TYPE_STRUCT);
    log_msg(is_error ? MSG_ERROR : MSG_WARN, type_checker->log, loc,
        "missing initializer for field '%s' in type '%s'",
        type->struct_type.fields[field_index].name, type->struct_type.name);
}

static inline void report_too_many_fields(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* type,
    size_t field_count)
{
    assert(type->tag == TYPE_STRUCT);
    log_error(type_checker->log, loc, "expected %zu initializer(s) for type '%s', but got %zu",
        type->struct_type.field_count, type->struct_type.name, field_count);
}

static inline void report_lossy_coercion(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* type,
    const struct type* expected_type)
{
    char* type_string = type_to_string(type, &type_checker->type_print_options);
    char* expected_type_string = type_to_string(expected_type, &type_checker->type_print_options);
    log_warn(type_checker->log, loc, "implicit conversion from '%s' to '%s' may lose information",
        type_string, expected_type_string);
    free(type_string);
    free(expected_type_string);
}

static inline void report_incomplete_coercion(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* type,
    const struct type* expected_type)
{
    assert(type->tag == TYPE_COMPOUND && expected_type->tag == TYPE_STRUCT);
    report_missing_field(type_checker, loc, expected_type, type->compound_type.elem_count, false);
}

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
        log_warn(type_checker->log, &ast->loc, "symbol '%s' shadows previous definition", name);
    } else {
        log_error(type_checker->log, &ast->loc, "redefinition for symbol '%s'", name);
    }
    assert(old_ast);
    report_previous_location(type_checker, &old_ast->loc);
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
    ast->cast_expr.value = copy;
    ast->type = type;
}

static inline struct const_eval eval_const_int(struct type_checker*, struct ast* ast) {
    // TODO: Use IR emitter for constants
    ast = ast_skip_parens(ast);
    return (struct const_eval) {
        .is_const = ast->tag == AST_INT_LITERAL,
        .int_val = ast->int_literal
    };
}

static inline bool is_safely_coercible_int_literal(struct type_checker* type_checker, struct ast* ast) {
    struct const_eval const_eval = eval_const_int(type_checker, ast);
    return const_eval.is_const && ((int)((float)const_eval.int_val)) == const_eval.int_val;
}

static inline const struct type* coerce_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    assert(ast->type);
    if (!expected_type || ast->type == expected_type)
        return ast->type;

    enum coercion_rank coercion_rank = type_coercion_rank(ast->type, expected_type);
    if (coercion_rank != COERCION_IMPOSSIBLE) {
        if (type_coercion_is_lossy(ast->type, expected_type) && !is_safely_coercible_int_literal(type_checker, ast)) {
            report_lossy_coercion(type_checker, &ast->loc, ast->type, expected_type);
        } else if (type_coercion_is_incomplete(ast->type, expected_type)) {
            report_incomplete_coercion(type_checker, &ast->loc, ast->type, expected_type);
        }
        insert_cast(type_checker, ast, expected_type);
    } else {
        report_invalid_type(type_checker, &ast->loc, ast->type, expected_type);
    }
    return expected_type;
}

static inline void expect_mutable(struct type_checker* type_checker, struct ast* ast) {
    if (!ast_is_mutable(ast))
        log_error(type_checker->log, &ast->loc, "value cannot be written to");
}

static const struct type* check_prim_type(struct type_checker* type_checker, struct ast* ast) {
    const struct type* prim_type = type_table_make_prim_type(type_checker->type_table, ast->prim_type.tag);
    if (!ast->prim_type.is_closure)
        return prim_type;
    return type_table_make_closure_type(type_checker->type_table, prim_type);
}

static const struct type* check_shader_type(struct type_checker* type_checker, struct ast* ast) {
    return type_table_make_shader_type(type_checker->type_table, ast->shader_type.tag);
}

static const struct type* check_named_type(struct type_checker* type_checker, struct ast* ast) {
    struct ast* symbol = env_find_one_symbol(type_checker->env, ast->named_type.name);
    if (!symbol) {
        log_error(type_checker->log, &ast->loc, "unknown identifier '%s'", ast->named_type.name);
        return ast->type = type_table_make_error_type(type_checker->type_table);
    }
    assert(symbol->type);
    ast->named_type.symbol = symbol;
    return ast->type = symbol->type;
}

static const struct type* check_type(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_PRIM_TYPE:   return check_prim_type(type_checker, ast);
        case AST_SHADER_TYPE: return check_shader_type(type_checker, ast);
        case AST_NAMED_TYPE:  return check_named_type(type_checker, ast);
        default:
            assert(false && "invalid type");
            [[fallthrough]];
        case AST_ERROR:
            return type_table_make_error_type(type_checker->type_table);
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
            log_error(type_checker->log, &ast->loc,
                "unsized arrays are only allowed as function or shader parameters");
        }
        return type_table_make_unsized_array_type(type_checker->type_table, elem_type);
    } else {
        check_expr(type_checker, ast, type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_INT));
        struct const_eval const_eval = eval_const_int(type_checker, ast);
        if (!const_eval.is_const || const_eval.int_val <= 0) {
            log_error(type_checker->log, &ast->loc,
                "array dimension must be constant and strictly positive");
            const_eval.int_val = 1;
        }
        return type_table_make_sized_array_type(type_checker->type_table, elem_type, (size_t)const_eval.int_val);
    }
}

static const struct type* check_var(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* type,
    bool is_global)
{
    insert_symbol(type_checker, ast->var.name, ast, false);
    type = check_array_dim(type_checker, ast->var.dim, type, false);
    if (ast->var.init) {
        if (is_global)
            log_error(type_checker->log, &ast->var.init->loc, "built-in global variables cannot be initialized");
        check_expr(type_checker, ast->var.init, type);
    }
    return ast->type = type;
}

static void check_var_decl(struct type_checker* type_checker, struct ast* ast, bool is_global) {
    if (is_global && !ast_find_attr(ast, "builtin"))
        log_error(type_checker->log, &ast->loc, "only built-in variables can be global");

    const struct type* type = check_type(type_checker, ast->var_decl.type);
    if (type_is_void(type))
        report_invalid_type_with_msg(type_checker, &ast->loc, type, "variable");
    for (struct ast* var = ast->var_decl.vars; var; var = var->next)
        check_var(type_checker, var, type, is_global);
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
    if (ast->param.is_ellipsis)
        return;
    ast->type = check_type(type_checker, ast->param.type);
    if (type_is_void(ast->type))
        report_invalid_type_with_msg(type_checker, &ast->loc, ast->type, "parameter");
    ast->type = check_array_dim(type_checker, ast->param.dim, ast->type, true);
    if (ast->param.name)
        insert_symbol(type_checker, ast->param.name, ast, false);
}

static bool check_params(struct type_checker* type_checker, struct ast* ast) {
    bool has_ellipsis = false;
    for (; ast; ast = ast->next) {
        has_ellipsis |= ast->param.is_ellipsis;
        if (ast->param.is_ellipsis && ast->next)
            log_error(type_checker->log, &ast->loc, "'...' is only valid at the end of a parameter list");
        check_param(type_checker, ast);
    }
    return has_ellipsis;
}

static inline struct ast* find_conflicting_overload(
    struct type_checker* type_checker,
    const char* name,
    const struct type* type)
{
    assert(type->tag == TYPE_FUNC);
    struct ast* conflicting_overload = NULL;
    struct small_ast_vec symbols = env_find_all_symbols(type_checker->env, name);
    small_ast_vec_relocate(&symbols);
    VEC_FOREACH(struct ast*, symbol_ptr, symbols) {
        if ((*symbol_ptr)->type->tag == TYPE_FUNC && (*symbol_ptr)->type == type) {
            conflicting_overload = *symbol_ptr;
            break;
        }
    }
    small_ast_vec_destroy(&symbols);
    return conflicting_overload;
}

static inline void insert_func_or_shader_symbol(struct type_checker* type_checker, struct ast* ast) {
    const char* decl_name = ast_decl_name(ast);
    struct ast* conflicting_overload = ast->tag == AST_SHADER_DECL
        ? env_find_one_symbol(type_checker->env, decl_name)
        : find_conflicting_overload(type_checker, decl_name, ast->type);
    if (conflicting_overload) {
        char* type_string = type_to_string(ast->type, &type_checker->type_print_options);
        log_error(type_checker->log, &ast->loc, "redefinition for %s '%s' with type '%s'",
            ast->tag == AST_FUNC_DECL ? "function" : "shader", decl_name, type_string);
        report_previous_location(type_checker, &conflicting_overload->loc);
        free(type_string);
    } else {
        insert_symbol(type_checker, decl_name, ast, true);
    }
}

static void check_shader_or_func_decl(struct type_checker* type_checker, struct ast* ast) {
    assert(ast->tag == AST_FUNC_DECL || ast->tag == AST_SHADER_DECL);
    env_push_scope(type_checker->env, ast);

    bool is_shader_decl = ast->tag == AST_SHADER_DECL;
    struct ast* params = is_shader_decl ? ast->shader_decl.params : ast->func_decl.params;
    bool has_ellipsis = check_params(type_checker, params);

    const struct type* ret_type = check_type(type_checker,
        is_shader_decl ? ast->shader_decl.type : ast->func_decl.ret_type);

    bool is_constructor = ast_find_attr(ast, "constructor");
    if (is_constructor && (ret_type->tag != TYPE_PRIM || ret_type->prim_type == PRIM_TYPE_VOID)) {
        log_error(type_checker->log, &ast->loc, "constructors must return a constructible primitive type");
    }

    struct small_func_param_vec func_params;
    small_func_param_vec_init(&func_params);
    for (struct ast* param = params; param; param = param->next) {
        if (param->param.is_ellipsis)
            continue;
        small_func_param_vec_push(&func_params, &(struct func_param) {
            .type = param->type,
            .is_output = param->param.is_output
        });
    }
    ast->type = type_table_make_func_type(
        type_checker->type_table, ret_type, func_params.elems, func_params.elem_count, has_ellipsis);
    small_func_param_vec_destroy(&func_params);

    // Built-in functions should not have a function body.
    bool is_builtin = ast_find_attr(ast, "builtin");
    if (has_ellipsis && !is_builtin)
        log_error(type_checker->log, &ast->loc, "'...' is only allowed on built-in functions");
    if (ast->func_decl.body) {
        if (is_builtin)
            log_error(type_checker->log, &ast->loc, "built-in function cannot have a body");
        if (ast->func_decl.body->tag != AST_ERROR)
            check_block_without_scope(type_checker, ast->func_decl.body);
    } else if (!is_builtin) {
        log_error(type_checker->log, &ast->loc, "missing function body");
    }

    env_pop_scope(type_checker->env);

    // Constructors are not placed into the environment, as they are not invoked like "real"
    // functions, but through a constructor expression instead.
    if (!is_constructor)
        insert_func_or_shader_symbol(type_checker, ast);
}

static void check_return_stmt(struct type_checker* type_checker, struct ast* ast) {
    struct ast* shader_or_func = env_find_enclosing_shader_or_func(type_checker->env);
    assert(shader_or_func);
    assert(shader_or_func->type && shader_or_func->type->tag == TYPE_FUNC);

    const struct type* ret_type = shader_or_func->type->func_type.ret_type;
    if (ast->return_stmt.value) {
        // Allow returning values of type 'void' from a function returning 'void',
        // but not from a shader (same behavior as the OSL compiler).
        if (ret_type->tag == TYPE_SHADER)
            log_error(type_checker->log, &ast->return_stmt.value->loc, "shaders cannot return a value");
        else
            check_expr(type_checker, ast->return_stmt.value, ret_type);
    } else if (ret_type->tag != TYPE_SHADER && !type_is_void(ret_type)) {
        log_error(type_checker->log, &ast->loc, "missing return value");
    }

    ast->return_stmt.shader_or_func = shader_or_func;
}

static void check_break_or_continue_stmt(struct type_checker* type_checker, struct ast* ast) {
    struct ast* loop = env_find_enclosing_loop(type_checker->env);
    if (!loop) {
        log_error(type_checker->log, &ast->loc, "'%s' is not allowed outside of loops",
            ast->tag == AST_BREAK_STMT ? "break" : "continue");
    }
    ast->break_stmt.loop = loop;
}

static void check_cond(struct type_checker* type_checker, struct ast* ast) {
    check_expr(type_checker, ast, type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_BOOL));
}

static const struct type* check_logic_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    check_cond(type_checker, ast->binary_expr.args);
    check_cond(type_checker, ast->binary_expr.args->next);
    ast->type = type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_BOOL);
    return coerce_expr(type_checker, ast, expected_type);
}

static void check_while_loop(struct type_checker* type_checker, struct ast* ast) {
    check_cond(type_checker, ast->while_loop.cond);
    env_push_scope(type_checker->env, ast);
    check_stmt(type_checker, ast->while_loop.body);
    env_pop_scope(type_checker->env);
}

static void check_for_loop(struct type_checker* type_checker, struct ast* ast) {
    env_push_scope(type_checker->env, ast);
    check_stmt(type_checker, ast->for_loop.init);
    check_cond(type_checker, ast->for_loop.cond);
    check_expr(type_checker, ast->for_loop.inc, NULL);
    check_stmt(type_checker, ast->for_loop.body);
    env_pop_scope(type_checker->env);
}

static void check_do_while_loop(struct type_checker* type_checker, struct ast* ast) {
    env_push_scope(type_checker->env, ast);
    check_stmt(type_checker, ast->do_while_loop.body);
    env_pop_scope(type_checker->env);
    check_cond(type_checker, ast->do_while_loop.cond);
}

static void check_if_stmt(struct type_checker* type_checker, struct ast* ast) {
    check_cond(type_checker, ast->if_stmt.cond);
    check_stmt(type_checker, ast->if_stmt.then_stmt);
    if (ast->if_stmt.else_stmt)
        check_stmt(type_checker, ast->if_stmt.else_stmt);
}

static void check_stmt(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_EMPTY_STMT:    break;
        case AST_BLOCK:         check_block(type_checker, ast);               break;
        case AST_VAR_DECL:      check_var_decl(type_checker, ast, false);     break;
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
        case AST_BOOL_LITERAL:
        case AST_INT_LITERAL:
        case AST_FLOAT_LITERAL:
        case AST_STRING_LITERAL:
        {
            const struct type* void_type = type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_VOID);
            check_expr(type_checker, ast, void_type);
            break;
        }
        default:
            assert(false && "invalid statement");
            [[fallthrough]];
        case AST_ERROR:
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
        small_ast_vec_relocate(&all_symbols);
        log_error(type_checker->log, &ast->loc,
            all_symbols.elem_count > 0
                ? "cannot resolve overloaded identifier '%s'"
                : "unknown identifier '%s'",
            ast->ident_expr.name);
        small_ast_vec_destroy(&all_symbols);
        return type_table_make_error_type(type_checker->type_table);
    }
    if (symbol->tag == AST_FUNC_DECL || symbol->tag == AST_STRUCT_DECL) {
        log_error(type_checker->log, &ast->loc, "cannot use %s '%s' as value",
            symbol->tag == AST_FUNC_DECL ? "function" : "structure", ast->ident_expr.name);
    }
    ast->ident_expr.symbol = symbol;
    ast->type = symbol->type;
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_assign_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    const struct type* left_type  = check_expr(type_checker, ast->binary_expr.args, NULL);
    check_expr(type_checker, ast->binary_expr.args->next, left_type);
    expect_mutable(type_checker, ast->binary_expr.args);
    ast->type = left_type;
    return coerce_expr(type_checker, ast, expected_type);
}

static inline bool is_viable_candidate(
    const struct type* candidate_type,
    const struct type* ret_type,
    const struct ast* args,
    size_t arg_count)
{
    assert(candidate_type->tag == TYPE_FUNC);
    if (candidate_type->func_type.param_count > arg_count ||
        (candidate_type->func_type.param_count < arg_count && !candidate_type->func_type.has_ellipsis))
        return false;
    const struct ast* arg = args;
    for (size_t i = 0; i < candidate_type->func_type.param_count; ++i, arg = arg->next) {
        if (candidate_type->func_type.params[i].is_output && !ast_is_mutable(arg))
            return false;
        if (!type_is_coercible_to(arg->type, candidate_type->func_type.params[i].type))
            return false;
    }
    if (ret_type && !type_is_coercible_to(candidate_type->func_type.ret_type, ret_type))
        return false;
    return true;
}

static inline size_t remove_non_viable_candidates(
    struct ast** candidates,
    size_t candidate_count,
    const struct type* ret_type,
    const struct ast* args)
{
    size_t j = 0, arg_count = ast_list_size(args);
    for (size_t i = 0; i < candidate_count; ++i) {
        if (is_viable_candidate(candidates[i]->type, ret_type, args, arg_count))
            candidates[j++] = candidates[i];
    }
    return j;
}

static bool is_better_candidate(
    const struct ast* candidate,
    const struct ast* other,
    const struct type* ret_type,
    const struct ast* args)
{
    assert(is_viable_candidate(candidate->type, ret_type, args, ast_list_size(args)));
    assert(is_viable_candidate(other->type, ret_type, args, ast_list_size(args)));
    bool is_better = false;
    size_t arg_index = 0;
    for (const struct ast* arg = args; arg; arg = arg->next, arg_index++) {
        enum coercion_rank candidate_rank = COERCION_ELLIPSIS;
        enum coercion_rank other_rank     = COERCION_ELLIPSIS;
        if (arg_index < candidate->type->func_type.param_count)
            candidate_rank = type_coercion_rank(arg->type, candidate->type->func_type.params[arg_index].type);
        if (arg_index < other->type->func_type.param_count)
            other_rank = type_coercion_rank(arg->type, other->type->func_type.params[arg_index].type);
        if (candidate_rank < other_rank)
            return false;
        is_better |= candidate_rank > other_rank;
    }
    if (is_better)
        return true;
    if (!ret_type)
        return false;
    return
        type_coercion_rank(candidate->type->func_type.ret_type, ret_type) >
        type_coercion_rank(other->type->func_type.ret_type, ret_type);
}

static struct ast* find_best_candidate(
    struct ast** candidates,
    size_t candidate_count,
    const struct type* ret_type,
    struct ast* args)
{
    struct ast* best_candidate = candidates[0];
    for (size_t i = 1; i < candidate_count; ++i) {
        if (is_better_candidate(candidates[i], best_candidate, ret_type, args))
            best_candidate = candidates[i];
    }
    for (size_t i = 0; i < candidate_count; ++i) {
        if (candidates[i] == best_candidate)
            continue;
        // Exit if an ambiguity is detected (when no candidate is the best).
        if (!is_better_candidate(best_candidate, candidates[i], ret_type, args))
            return NULL;
    }
    return best_candidate;
}

static struct ast* find_func_from_candidates(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const char* func_name,
    struct ast** candidates,
    size_t candidate_count,
    const struct type* ret_type,
    struct ast* args)
{
    if (candidate_count > 0 && candidates[0]->type->tag != TYPE_FUNC) {
        report_invalid_type_with_msg(type_checker, loc, candidates[0]->type, "function");
        return NULL;
    }

    size_t viable_count = remove_non_viable_candidates(candidates, candidate_count, ret_type, args);
    if (viable_count == 0) {
        report_overload_error(
            type_checker, loc, "no viable candidate for", func_name, candidates, candidate_count, ret_type, args);
        return NULL;
    } else if (viable_count == 1) {
        return candidates[0];
    }

    struct ast* symbol = find_best_candidate(candidates, viable_count, ret_type, args);
    if (!symbol) {
        report_overload_error(
            type_checker, loc, "ambiguous", func_name, candidates, viable_count, ret_type, args);
    }
    return symbol;
}

static struct ast* find_func_or_struct_with_name(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const char* func_name,
    const struct type* ret_type,
    struct ast* args)
{
    struct small_ast_vec symbols = env_find_all_symbols(type_checker->env, func_name);
    small_ast_vec_relocate(&symbols);

    struct ast* symbol = NULL;
    if (symbols.elem_count == 0) {
        log_error(type_checker->log, loc, "unknown identifier '%s'", func_name);
    } else if (symbols.elem_count == 1 && symbols.elems[0]->tag == AST_STRUCT_DECL) {
        symbol = symbols.elems[0];
    } else {
        symbol = find_func_from_candidates(
            type_checker, loc, func_name, symbols.elems, symbols.elem_count, ret_type, args);
    }

    small_ast_vec_destroy(&symbols);
    return symbol;
}

static const struct type* check_struct_constructor(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* constructor_type,
    struct ast* args)
{
    assert(constructor_type->tag == TYPE_FUNC);
    assert(constructor_type->func_type.ret_type->tag == TYPE_STRUCT);
    size_t arg_count = ast_list_size(args);
    if (arg_count < constructor_type->func_type.param_count) {
        report_missing_field(type_checker, loc, constructor_type->func_type.ret_type, arg_count, true);
        return type_table_make_error_type(type_checker->type_table);
    } else if (arg_count > constructor_type->func_type.param_count) {
        report_too_many_fields(type_checker, loc, constructor_type->func_type.ret_type, arg_count);
        return type_table_make_error_type(type_checker->type_table);
    }
    return constructor_type;
}

static const struct type* check_callee(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* ret_type,
    struct ast* args)
{
    struct ast* callee = ast_skip_parens(ast);
    if (callee->tag != AST_IDENT_EXPR)
        return check_expr(type_checker, ast, NULL);

    struct ast* symbol = find_func_or_struct_with_name(
        type_checker, &ast->loc, callee->ident_expr.name, ret_type, args);

    callee->ident_expr.symbol = symbol;
    if (symbol) {
        callee->type = symbol->tag == AST_STRUCT_DECL
            ? check_struct_constructor(type_checker, &ast->loc, symbol->struct_decl.constructor_type, args)
            : symbol->type;
    } else {
        callee->type = type_table_make_error_type(type_checker->type_table);
    }

    return ast->type = callee->type;
}

static inline bool check_call_args(struct type_checker* type_checker, struct ast* args) {
    bool success = true;
    for (struct ast* arg = args; arg; arg = arg->next)
        success &= check_expr(type_checker, arg, NULL)->tag != TYPE_ERROR;
    return success;
}

static const struct type* check_call_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    if (!check_call_args(type_checker, ast->call_expr.args))
        return ast->type = type_table_make_error_type(type_checker->type_table);

    const struct type* callee_type = check_callee(
        type_checker, ast->call_expr.callee, expected_type, ast->call_expr.args);
    if (callee_type->tag != TYPE_FUNC) {
        // Error was already reported in find_func_by_name
        return ast->type = type_table_make_error_type(type_checker->type_table);
    }

    assert(
        ast_list_size(ast->call_expr.args) == callee_type->func_type.param_count ||
        (callee_type->func_type.has_ellipsis && ast_list_size(ast->call_expr.args) > callee_type->func_type.param_count));

    size_t arg_index = 0;
    for (struct ast* arg = ast->call_expr.args; arg_index < callee_type->func_type.param_count; arg = arg->next)
        coerce_expr(type_checker, arg, callee_type->func_type.params[arg_index++].type);
    ast->type = callee_type->func_type.ret_type;
    return coerce_expr(type_checker, ast, expected_type);
}

static inline const struct type* find_func_ret_type(struct ast* symbol) {
    assert(symbol->type);
    assert(symbol->type->tag == TYPE_FUNC);
    return symbol->type->func_type.ret_type;
}

static const struct type* check_binary_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    if (ast->binary_expr.tag == BINARY_EXPR_ASSIGN)
        return check_assign_expr(type_checker, ast, expected_type);
    if (binary_expr_tag_is_logic(ast->binary_expr.tag))
        return check_logic_expr(type_checker, ast, expected_type);

    if (!check_call_args(type_checker, ast->binary_expr.args))
        return ast->type = type_table_make_error_type(type_checker->type_table);

    const char* func_name = binary_expr_tag_to_func_name(ast->binary_expr.tag);
    struct ast* symbol = find_func_or_struct_with_name(type_checker, &ast->loc, func_name, expected_type, ast->binary_expr.args);
    if (!symbol)
        return ast->type = type_table_make_error_type(type_checker->type_table);

    ast->unary_expr.symbol = symbol;
    ast->type = find_func_ret_type(symbol);

    if (binary_expr_tag_is_assign(ast->binary_expr.tag))
        expect_mutable(type_checker, ast->binary_expr.args);
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_unary_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    if (!check_call_args(type_checker, ast->unary_expr.arg))
        return ast->type = type_table_make_error_type(type_checker->type_table);

    const char* func_name = unary_expr_tag_to_func_name(ast->unary_expr.tag);
    struct ast* symbol = find_func_or_struct_with_name(type_checker, &ast->loc, func_name, expected_type, ast->unary_expr.arg);
    if (!symbol || symbol->tag == AST_STRUCT_DECL)
        return ast->type = type_table_make_error_type(type_checker->type_table);

    ast->unary_expr.symbol = symbol;
    ast->type = find_func_ret_type(symbol);

    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_compound_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    struct ast* last = NULL;
    for (struct ast* elem = ast->compound_expr.elems; elem; last = elem, elem = elem->next)
        check_expr(type_checker, elem, ast->next ? NULL : expected_type);
    assert(last);
    return ast->type = last->type;
}

static const struct type* check_compound_init(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    struct small_type_vec elem_types;
    small_type_vec_init(&elem_types);
    for (struct ast* elem = ast->compound_init.elems; elem; elem = elem->next) {
        const struct type* elem_type = check_expr(type_checker, elem, NULL);
        small_type_vec_push(&elem_types, &elem_type);
    }
    ast->type = type_table_make_compound_type(type_checker->type_table, elem_types.elems, elem_types.elem_count);
    small_type_vec_destroy(&elem_types);
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_construct_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    const struct type* type = check_type(type_checker, ast->construct_expr.type);
    assert(type->tag == TYPE_PRIM);

    if (!check_call_args(type_checker, ast->construct_expr.args))
        return type_table_make_error_type(type_checker->type_table);

    struct small_ast_vec candidates;
    small_ast_vec_init(&candidates);
    struct ast* constructors = builtins_constructors(type_checker->builtins, type->prim_type);
    for (struct ast* constructor = constructors; constructor; constructor = constructor->next)
        small_ast_vec_push(&candidates, &constructor);
    struct ast* symbol = find_func_from_candidates(type_checker, &ast->loc,
        prim_type_tag_to_string(type->prim_type), candidates.elems, candidates.elem_count, type, ast->construct_expr.args);
    small_ast_vec_destroy(&candidates);
    if (!symbol)
        return type_table_make_error_type(type_checker->type_table);

    assert(find_func_ret_type(symbol) == type);
    ast->type = type;
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_ternary_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    check_cond(type_checker, ast->ternary_expr.cond);
    ast->type = check_expr(type_checker, ast->ternary_expr.then_expr, NULL);
    check_expr(type_checker, ast->ternary_expr.else_expr, ast->type);
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_single_index_expr(
    struct type_checker* type_checker,
    const struct file_loc* loc,
    const struct type* value_type)
{
    if (value_type->tag == TYPE_ARRAY) {
        return value_type->array_type.elem_type;
    } else if (type_is_triple(value_type)) {
        return type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_FLOAT);
    } else {
        report_invalid_type_with_msg(type_checker, loc, value_type, "vector, point, normal, color, or array");
        return type_table_make_error_type(type_checker->type_table);
    }
}

static const struct type* check_index_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    const struct type* int_type = type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_INT);
    if (ast->index_expr.value->tag != AST_INDEX_EXPR) {
        const struct type* value_type = check_expr(type_checker, ast->index_expr.value, NULL);
        check_expr(type_checker, ast->index_expr.index, int_type);
        ast->type = check_single_index_expr(type_checker, &ast->index_expr.value->loc, value_type);
        return coerce_expr(type_checker, ast, expected_type);
    }

    const struct type* value_type = check_expr(type_checker, ast->index_expr.value->index_expr.value, NULL);
    check_expr(type_checker, ast->index_expr.index, int_type);
    check_expr(type_checker, ast->index_expr.value->index_expr.index, int_type);
    if (type_is_prim_type(value_type, PRIM_TYPE_MATRIX)) {
        ast->type = type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_FLOAT);
    } else {
        value_type = check_single_index_expr(type_checker, &ast->index_expr.value->index_expr.value->loc, value_type);
        ast->type  = check_single_index_expr(type_checker, &ast->index_expr.value->loc, value_type);
    }
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_proj_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    const struct type* value_type = check_expr(type_checker, ast->proj_expr.value, NULL);

    if (type_is_triple(value_type)) {
        const char* component_names = value_type->prim_type == PRIM_TYPE_COLOR ? "rgb" : "xyz";
        if (ast->proj_expr.elem[0] != 0 && ast->proj_expr.elem[1] == 0) {
            for (size_t i = 0; i < 3; ++i) {
                if (ast->proj_expr.elem[0] == component_names[i]) {
                    ast->type = type_table_make_prim_type(type_checker->type_table, PRIM_TYPE_FLOAT);
                    ast->proj_expr.index = i;
                    break;
                }
            }
        }
    } else if (value_type->tag == TYPE_STRUCT) {
        for (size_t i = 0; i < value_type->struct_type.field_count; ++i) {
            if (!strcmp(value_type->struct_type.fields[i].name, ast->proj_expr.elem)) {
                ast->type = value_type->struct_type.fields[i].type;
                ast->proj_expr.index = i;
                break;
            }
        }
    }

    if (!ast->type) {
        ast->type = type_table_make_error_type(type_checker->type_table);
        if (value_type->tag != TYPE_ERROR) {
            char* type_string = type_to_string(value_type, &type_checker->type_print_options);
            log_error(type_checker->log, &ast->loc, "unknown field or component '%s' for type '%s'",
                ast->proj_expr.elem, type_string);
            free(type_string);
        }
    }

    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_cast_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    ast->type = check_type(type_checker, ast->cast_expr.type);
    const struct type* value_type = check_expr(type_checker, ast->cast_expr.value, NULL);
    if (!type_is_castable_to(value_type, ast->type)) {
        char* value_type_string = type_to_string(value_type, &type_checker->type_print_options);
        char* type_string = type_to_string(ast->type, &type_checker->type_print_options);
        log_error(type_checker->log, &ast->loc, "invalid cast from type '%s' to type '%s'",
            value_type_string, type_string);
        free(value_type_string);
        free(type_string);
    }
    return coerce_expr(type_checker, ast, expected_type);
}

static inline const struct type* check_literal(struct type_checker* type_checker, struct ast* ast) {
    enum prim_type_tag tag = PRIM_TYPE_VOID;
    switch (ast->tag) {
        case AST_INT_LITERAL:    tag = PRIM_TYPE_INT;    break;
        case AST_FLOAT_LITERAL:  tag = PRIM_TYPE_FLOAT;  break;
        case AST_BOOL_LITERAL:   tag = PRIM_TYPE_BOOL;   break;
        case AST_STRING_LITERAL: tag = PRIM_TYPE_STRING; break;
        default:
            assert(false && "invalid AST literal");
            break;
    }
    return ast->type = type_table_make_prim_type(type_checker->type_table, tag);
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
        case AST_STRING_LITERAL:
            check_literal(type_checker, ast);
            return coerce_expr(type_checker, ast, expected_type);

        case AST_IDENT_EXPR:     return check_ident_expr(type_checker, ast, expected_type);
        case AST_BINARY_EXPR:    return check_binary_expr(type_checker, ast, expected_type);
        case AST_UNARY_EXPR:     return check_unary_expr(type_checker, ast, expected_type);
        case AST_CALL_EXPR:      return check_call_expr(type_checker, ast, expected_type);
        case AST_CONSTRUCT_EXPR: return check_construct_expr(type_checker, ast, expected_type);
        case AST_PAREN_EXPR:     return ast->type = check_expr(type_checker, ast->paren_expr.inner_expr, expected_type);
        case AST_COMPOUND_EXPR:  return check_compound_expr(type_checker, ast, expected_type);
        case AST_COMPOUND_INIT:  return check_compound_init(type_checker, ast, expected_type);
        case AST_TERNARY_EXPR:   return check_ternary_expr(type_checker, ast, expected_type);
        case AST_INDEX_EXPR:     return check_index_expr(type_checker, ast, expected_type);
        case AST_PROJ_EXPR:      return check_proj_expr(type_checker, ast, expected_type);
        case AST_CAST_EXPR:      return check_cast_expr(type_checker, ast, expected_type);
        default:
            assert(false && "invalid expression");
            [[fallthrough]];
        case AST_ERROR:
            return type_table_make_error_type(type_checker->type_table);
    }
}

static void check_struct_decl(struct type_checker* type_checker, struct ast* ast) {
    static const char* operator_prefix = "__operator__";
    if (!strncmp(ast->struct_decl.name, operator_prefix, strlen(operator_prefix))) {
        log_error(type_checker->log, &ast->loc, "structure name '%s' is not allowed", ast->struct_decl.name);
        log_note(type_checker->log, NULL, "names beginning with '%s' are reserved for functions", operator_prefix);
        return;
    }

    insert_symbol(type_checker, ast->struct_decl.name, ast, false);
    env_push_scope(type_checker->env, ast);
    struct type* struct_type = type_table_create_struct_type(type_checker->type_table, ast_field_count(ast));
    struct_type->struct_type.name = ast->struct_decl.name;
    size_t field_index = 0;
    for (struct ast* field = ast->struct_decl.fields; field; field = field->next) {
        assert(field->tag == AST_VAR_DECL);
        const struct type* field_type = check_type(type_checker, field->var_decl.type);
        for (struct ast* var = field->var_decl.vars; var; var = var->next) {
            struct struct_field* struct_field = &struct_type->struct_type.fields[field_index++];
            check_var(type_checker, var, field_type, false);
            struct_field->name = var->var.name;
            struct_field->type = var->type;
        }
    }
    assert(field_index == ast_field_count(ast));
    type_table_finalize_struct_type(type_checker->type_table, struct_type);
    env_pop_scope(type_checker->env);
    ast->type = struct_type;
    ast->struct_decl.constructor_type = type_table_make_constructor_type(type_checker->type_table, struct_type);
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
        case AST_VAR_DECL:
            check_var_decl(type_checker, ast, true);
            break;
        default:
            assert(false && "invalid declaration");
            [[fallthrough]];
        case AST_ERROR:
            break;
    }
}

void check(
    struct mem_pool* mem_pool,
    struct type_table* type_table,
    const struct builtins* builtins,
    struct ast* ast,
    struct log* log)
{
    struct type_checker type_checker = {
        .type_print_options.disable_colors = log->disable_colors,
        .mem_pool = mem_pool,
        .type_table = type_table,
        .builtins = builtins,
        .env = env_create(),
        .log = log
    };
    builtins_populate_env(builtins, type_checker.env);
    for (; ast; ast = ast->next)
        check_top_level_decl(&type_checker, ast);
    env_destroy(type_checker.env);
}
