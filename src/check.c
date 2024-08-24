#include "check.h"
#include "env.h"
#include "ast.h"
#include "type_table.h"

#include <overture/log.h>
#include <overture/mem_pool.h>
#include <overture/mem_stream.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct type_checker {
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
static void check_cond(struct type_checker*, struct ast*);
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
        log_warn(type_checker->log, &ast->loc, "symbol '%s' shadows previous definition", name);
    } else {
        log_error(type_checker->log, &ast->loc, "redefinition for symbol '%s'", name);
    }
    log_note(type_checker->log, &old_ast->loc, "previously declared here");
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

static inline const struct type* coerce_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    assert(ast->type);
    if (!expected_type || ast->type == expected_type)
        return ast->type;

    if (type_is_coercible_to(ast->type, expected_type))
        insert_cast(type_checker, ast, expected_type);
    else
        report_invalid_type(type_checker, &ast->loc, ast->type, expected_type);
    return expected_type;
}

static inline void expect_mutable(struct type_checker* type_checker, struct ast* ast) {
    if (!ast_is_mutable(ast))
        log_error(type_checker->log, &ast->loc, "value cannot be written to");
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
            log_error(type_checker->log, &ast->loc,
                "unsized arrays are only allowed as function or shader parameters");
        }
        return type_table_get_unsized_array_type(type_checker->type_table, elem_type);
    } else {
        check_expr(type_checker, ast, type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_INT));
        struct const_eval const_eval = eval_const_int(type_checker, ast);
        if (!const_eval.is_const || const_eval.int_val <= 0) {
            log_error(type_checker->log, &ast->loc,
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
    if (type_is_void(type))
        report_invalid_type_with_msg(type_checker, &ast->loc, type, "variable");
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
    if (type_is_void(ast->type))
        report_invalid_type_with_msg(type_checker, &ast->loc, ast->type, "parameter");
    ast->type = check_array_dim(type_checker, ast->param.dim, ast->type, true);
    insert_symbol(type_checker, ast->param.name, ast, false);
}

static void check_params(struct type_checker* type_checker, struct ast* ast) {
    for (; ast; ast = ast->next)
        check_param(type_checker, ast);
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
        if ((*symbol_ptr)->type->tag == TYPE_FUNC &&
            type_has_same_param_and_ret_types((*symbol_ptr)->type, type))
        {
            conflicting_overload = *symbol_ptr;
            break;
        }
    }
    small_ast_vec_destroy(&symbols);
    return conflicting_overload;
}

static void check_shader_or_func_decl(struct type_checker* type_checker, struct ast* ast) {
    assert(ast->tag == AST_FUNC_DECL || ast->tag == AST_SHADER_DECL);
    bool is_shader_decl = ast->tag == AST_SHADER_DECL;
    const char* decl_name = ast_decl_name(ast);

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

    struct ast* conflicting_overload = find_conflicting_overload(type_checker, decl_name, ast->type);
    if (conflicting_overload) {
        char* type_string = type_to_string(ast->type, &type_checker->type_print_options);
        log_error(type_checker->log, &ast->loc, "redefinition for %s '%s' with type '%s'",
            ast->tag == AST_FUNC_DECL ? "function" : "shader", decl_name, type_string);
        log_note(type_checker->log, &conflicting_overload->loc, "previously declared here");
        free(type_string);
    }

    insert_symbol(type_checker, decl_name, ast, true);
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

static const struct type* check_logic_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    check_cond(type_checker, ast->binary_expr.args);
    check_cond(type_checker, ast->binary_expr.args->next);
    return ast->type = type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_BOOL);
}

static void check_cond(struct type_checker* type_checker, struct ast* ast) {
    if (ast->tag == AST_BINARY_EXPR && binary_expr_tag_is_logic(ast->binary_expr.tag)) {
        check_logic_expr(type_checker, ast, NULL);
    } else {
        check_expr(type_checker, ast, NULL);
        if (type_is_prim_type(ast->type, PRIM_TYPE_STRING) ||
            type_is_prim_type(ast->type, PRIM_TYPE_INT))
        {
            // Strings can be used as conditions or in logic expressions, in which case they are
            // considered to be "true" when they are non empty, and "false" otherwise.
            insert_cast(type_checker, ast,
                type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_BOOL));
        } else {
            coerce_expr(type_checker, ast,
                type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_BOOL));
        }
    }
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
        small_ast_vec_relocate(&all_symbols);
        log_error(type_checker->log, &ast->loc,
            all_symbols.elem_count > 0
                ? "identifier '%s' is overloaded"
                : "unknown identifier '%s'",
            ast->ident_expr.name);
        small_ast_vec_destroy(&all_symbols);
        return type_table_get_error_type(type_checker->type_table);
    }
    ast->ident_expr.symbol = symbol;
    return ast->type = symbol->type;
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

static inline bool is_viable_candidate(const struct type* candidate_type, struct ast* args, size_t arg_count) {
    assert(candidate_type->tag == TYPE_FUNC);
    if (candidate_type->func_type.param_count > arg_count ||
        (candidate_type->func_type.param_count < arg_count && !candidate_type->func_type.has_ellipsis))
        return false;
    struct ast* arg = args;
    for (size_t i = 0; i < candidate_type->func_type.param_count; ++i, arg = arg->next) {
        if (candidate_type->func_type.params[i].is_output && !ast_is_mutable(arg))
            return false;
        if (!type_is_coercible_to(arg->type, candidate_type->func_type.params[i].type))
            return false;
    }
    return true;
}

static inline size_t remove_non_viable_candidates(
    struct ast** candidates,
    size_t candidate_count,
    struct ast* args)
{
    size_t j = 0, arg_count = ast_list_size(args);
    for (size_t i = 0; i < candidate_count; ++i) {
        if (is_viable_candidate(candidates[i]->type, args, arg_count))
            candidates[j++] = candidates[i];
    }
    return j;
}

static inline enum coercion_rank coercion_rank_for_arg(
    size_t arg_index,
    const struct type* arg_type,
    const struct type* candidate_type)
{
    if (arg_index >= candidate_type->func_type.param_count) {
        assert(candidate_type->func_type.has_ellipsis);
        return COERCION_ELLIPSIS;
    }
    return type_coercion_rank(arg_type, candidate_type->func_type.params[arg_index].type);
}

static bool is_better_candidate(
    struct ast* candidate,
    struct ast* other,
    const struct type* ret_type,
    struct ast* args)
{
    [[maybe_unused]] size_t arg_count = ast_list_size(args);
    assert(is_viable_candidate(candidate->type, args, arg_count));
    assert(is_viable_candidate(other->type, args, arg_count));

    bool is_better = false;
    size_t i = 0;
    for (struct ast* arg = args; arg; arg = arg->next, i++) {
        enum coercion_rank candidate_rank = coercion_rank_for_arg(i, arg->type, candidate->type);
        enum coercion_rank other_rank     = coercion_rank_for_arg(i, arg->type, other->type);
        if (candidate_rank < other_rank)
            return false;
        is_better |= candidate_rank > other_rank;
    }
    if (ret_type && !is_better) {
        is_better |=
            type_coercion_rank(candidate->type->func_type.ret_type, ret_type) >
            type_coercion_rank(other->type->func_type.ret_type, ret_type);
    }
    return is_better;
}

static bool is_best_candidate(
    struct ast* candidate,
    struct ast** candidates,
    size_t candidate_count,
    const struct type* ret_type,
    struct ast* args)
{
    for (size_t i = 0; i < candidate_count; ++i) {
        if (candidate == candidates[i])
            continue;
        if (!is_better_candidate(candidate, candidates[i], ret_type, args))
            return false;
    }
    return true;
}

static struct ast* find_best_candidate(
    struct ast** candidates,
    size_t candidate_count,
    const struct type* ret_type,
    struct ast* args)
{
    for (size_t i = 0; i < candidate_count; ++i) {
        if (is_best_candidate(candidates[i], candidates, candidate_count, ret_type, args))
            return candidates[i];
    }
    return NULL;
}

static void list_candidates(struct type_checker* type_checker, struct ast* const* candidates, size_t candidate_count) {
    for (size_t i = 0; i < candidate_count; ++i) {
        char* candidate_type_string = type_to_string(candidates[i]->type, &type_checker->type_print_options);
        log_note(type_checker->log, &candidates[i]->loc, "candidate with type '%s'", candidate_type_string);
        free(candidate_type_string);
    }
}

static inline char* arg_types_to_string(struct ast* args, const struct type_print_options* type_print_options) {
    struct mem_stream mem_stream;
    mem_stream_init(&mem_stream);
    for (struct ast* arg = args; arg; arg = arg->next) {
        type_print(mem_stream.file, arg->type, type_print_options);
        if (arg->next)
            fputs(", ", mem_stream.file);
    }
    mem_stream_destroy(&mem_stream);
    return mem_stream.buf;
}

static struct ast* find_func_with_name(
    struct type_checker* type_checker,
    struct file_loc* loc,
    const char* func_name,
    const struct type* ret_type,
    struct ast* args)
{
    struct small_ast_vec symbols = env_find_all_symbols(type_checker->env, func_name);
    small_ast_vec_relocate(&symbols);

    struct ast* symbol = NULL;
    if (symbols.elem_count == 0) {
        log_error(type_checker->log, loc, "unknown identifier '%s'", func_name);
    } else if (symbols.elems[0]->type->tag != TYPE_FUNC) {
        report_invalid_type_with_msg(type_checker, loc, symbols.elems[0]->type, "function");
    } else {
        size_t viable_count = remove_non_viable_candidates(symbols.elems, symbols.elem_count, args);
        if (viable_count == 0) {
            char* arg_types_string = arg_types_to_string(args, &type_checker->type_print_options);
            log_error(type_checker->log, loc, "no viable candidate for call to '%s' with signature '(%s)'", func_name, arg_types_string);
            list_candidates(type_checker, symbols.elems, symbols.elem_count);
            free(arg_types_string);
        } else if (viable_count == 1) {
            symbol = symbols.elems[0];
        } else {
            symbol = find_best_candidate(symbols.elems, viable_count, ret_type, args);
            if (!symbol) {
                log_error(type_checker->log, loc, "ambiguous call to '%s'", func_name);
                list_candidates(type_checker, symbols.elems, viable_count);
            }
        }
    }

    small_ast_vec_destroy(&symbols);
    return symbol;
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

    struct ast* symbol = find_func_with_name(
        type_checker, &ast->loc, callee->ident_expr.name, ret_type, args);

    callee->type = symbol ? symbol->type : type_table_get_error_type(type_checker->type_table);
    callee->ident_expr.symbol = symbol;
    for (; ast != callee; ast = ast->paren_expr.inner_expr)
        ast->type = callee->type;
    return callee->type;
}

static inline bool check_call_args(struct type_checker* type_checker, struct ast* args) {
    for (struct ast* arg = args; arg; arg = arg->next) {
        if (check_expr(type_checker, arg, NULL)->tag == TYPE_ERROR)
            return false;
    }
    return true;
}

static const struct type* check_call_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    if (!check_call_args(type_checker, ast->call_expr.args))
        return ast->type = type_table_get_error_type(type_checker->type_table);

    const struct type* callee_type = check_callee(
        type_checker, ast->call_expr.callee, expected_type, ast->call_expr.args);
    if (callee_type->tag != TYPE_FUNC) {
        // Error was already reported in find_func_by_name
        return ast->type = type_table_get_error_type(type_checker->type_table);
    }

    assert(ast_list_size(ast->call_expr.args) == callee_type->func_type.param_count);
    size_t arg_index = 0;
    for (struct ast* arg = ast->call_expr.args; arg; arg = arg->next)
        coerce_expr(type_checker, arg, callee_type->func_type.params[arg_index++].type);
    ast->type = callee_type->func_type.ret_type;
    return coerce_expr(type_checker, ast, expected_type);
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

    check_expr(type_checker, ast->binary_expr.args, NULL);
    check_expr(type_checker, ast->binary_expr.args->next, NULL);

    const char* func_name = binary_expr_tag_to_func_name(ast->binary_expr.tag);
    struct ast* symbol = find_func_with_name(type_checker, &ast->loc, func_name, expected_type, ast->binary_expr.args);
    if (!symbol)
        return ast->type = type_table_get_error_type(type_checker->type_table);

    if (binary_expr_tag_is_assign(ast->binary_expr.tag))
        expect_mutable(type_checker, ast->binary_expr.args);
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_unary_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    check_expr(type_checker, ast->unary_expr.arg, NULL);

    const char* func_name = unary_expr_tag_to_func_name(ast->unary_expr.tag);
    struct ast* symbol = find_func_with_name(type_checker, &ast->loc, func_name, expected_type, ast->unary_expr.arg);
    if (!symbol)
        return ast->type = type_table_get_error_type(type_checker->type_table);

    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_construct_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    ast->type = check_type(type_checker, ast->construct_expr.type);
    return coerce_expr(type_checker, ast, expected_type);
}

static const struct type* check_compound_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    // TODO
    return ast->type = type_table_get_error_type(type_checker->type_table);
}

static const struct type* check_compound_init(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    if (expected_type) {
        if (expected_type->tag == TYPE_ARRAY)  {
            assert(expected_type->tag == TYPE_ARRAY);
            size_t arg_count = 0;
            for (struct ast* elem = ast->compound_init.elems; elem; elem = elem->next, arg_count++)
                check_expr(type_checker, elem, expected_type->array_type.elem_type);
            return ast->type = type_table_get_sized_array_type(type_checker->type_table,
                expected_type->array_type.elem_type, arg_count);
        } else if (expected_type->tag == TYPE_PRIM) {
            // TODO
            return ast->type = type_table_get_error_type(type_checker->type_table);
        } else if (expected_type->tag == TYPE_STRUCT) {
            // TODO
            return ast->type = type_table_get_error_type(type_checker->type_table);
        }
    }

    log_error(type_checker->log, &ast->loc,
        "compound initializers are only allowed where a structure, vector, point, normal, color, matrix, or array type is required");
    return ast->type = type_table_get_error_type(type_checker->type_table);
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
        return type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_FLOAT);
    } else {
        report_invalid_type_with_msg(type_checker, loc, value_type, "vector, point, normal, color, or array");
        return type_table_get_error_type(type_checker->type_table);
    }
}

static const struct type* check_index_expr(
    struct type_checker* type_checker,
    struct ast* ast,
    const struct type* expected_type)
{
    const struct type* int_type = type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_INT);
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
        ast->type = type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_FLOAT);
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
            for (size_t index = 0; index < 3; ++index) {
                if (ast->proj_expr.elem[0] == component_names[index]) {
                    ast->type = type_table_get_prim_type(type_checker->type_table, PRIM_TYPE_FLOAT);
                    ast->proj_expr.index = index;
                    break;
                }
            }
        }
    }

    // TODO: Structures

    if (!ast->type) {
        ast->type = type_table_get_error_type(type_checker->type_table);
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
        case AST_PAREN_EXPR:     return ast->type = check_expr(type_checker, ast->paren_expr.inner_expr, expected_type);
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
        .type_print_options.disable_colors = log->disable_colors,
        .mem_pool = mem_pool,
        .type_table = type_table_create(mem_pool),
        .env = env_create(),
        .log = log
    };
    for (; ast; ast = ast->next)
        check_top_level_decl(&type_checker, ast);
    env_destroy(type_checker.env);
    type_table_destroy(type_checker.type_table);
}
