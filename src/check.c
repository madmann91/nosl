#include "check.h"
#include "env.h"
#include "ast.h"

#include <overture/log.h>

#include <assert.h>

struct type_checker {
    struct env* env;
    struct log* log;
};

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

static void check_func_decl(struct type_checker* type_checker, struct ast* ast) {
    insert_symbol(type_checker, ast->func_decl.name, ast, true);
}

static void check_struct_decl(struct type_checker* type_checker, struct ast* ast) {
}

static void check_shader_decl(struct type_checker* type_checker, struct ast* ast) {
}

static void check_decl(struct type_checker* type_checker, struct ast* ast) {
    switch (ast->tag) {
        case AST_FUNC_DECL:   check_func_decl(type_checker, ast);   break;
        case AST_STRUCT_DECL: check_struct_decl(type_checker, ast); break;
        case AST_SHADER_DECL: check_shader_decl(type_checker, ast); break;
        default:
            assert(false && "invalid declaration");
            break;
    }
}

void check(struct ast* ast, struct log* log) {
    struct type_checker type_checker = {
        .env = env_create(),
        .log = log
    };
    for (; ast; ast = ast->next)
        check_decl(&type_checker, ast);
    env_destroy(type_checker.env);
}
