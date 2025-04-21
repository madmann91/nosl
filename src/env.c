#include "env.h"

#include <overture/map.h>
#include <overture/hash.h>
#include <overture/mem.h>

struct symbol {
    struct ast* ast;
    struct symbol* next;
    bool allow_overload;
};

static inline uint32_t hash_symbol_name(uint32_t h, const char* const* string_ptr) {
    return hash_string(h, *string_ptr);
}

static inline bool are_symbol_names_equal(const char* const* string_ptr, const char* const* other_ptr) {
    return !strcmp(*string_ptr, *other_ptr);
}

MAP_DEFINE(symbol_table, const char*, struct symbol*, hash_symbol_name, are_symbol_names_equal, PRIVATE)

struct scope {
    struct ast* ast;
    struct symbol_table symbol_table;
    struct scope* prev;
    struct scope* next;
};

struct env {
    struct scope* scope;
    struct symbol* free_symbols;
};

[[nodiscard]] static inline struct symbol* alloc_symbol(struct env* env) {
    if (env->free_symbols) {
        struct symbol* symbol = env->free_symbols;
        env->free_symbols = symbol->next;
        symbol->next = NULL;
        return symbol;
    }
    return xcalloc(1, sizeof(struct symbol));
}

static inline void free_symbol(struct env* env, struct symbol* symbol) {
    symbol->next = env->free_symbols;
    env->free_symbols = symbol;
}

[[nodiscard]] static inline struct scope* alloc_scope(struct scope* prev) {
    struct scope* scope = xcalloc(1, sizeof(struct scope));
    scope->symbol_table = symbol_table_create();
    scope->prev = prev;
    return scope;
}

static inline void clear_scope(struct env* env, struct scope* scope) {
    MAP_FOREACH_VAL(struct symbol*, symbol_ptr, scope->symbol_table) {
        struct symbol* symbol = *symbol_ptr;
        while (symbol) {
            struct symbol* next = symbol->next;
            free_symbol(env, symbol);
            symbol = next;
        }
    }
    symbol_table_clear(&scope->symbol_table);
}

static inline void free_scope(struct env* env, struct scope* scope) {
    clear_scope(env, scope);
    symbol_table_destroy(&scope->symbol_table);
    free(scope);
}

struct env* env_create(void) {
    struct env* env = xmalloc(sizeof(struct env));
    env->scope = alloc_scope(NULL);
    env->free_symbols = NULL;
    return env;
}

void env_destroy(struct env* env) {
    struct scope* scope = env->scope;
    while (scope->prev)
        scope = scope->prev;
    while (scope) {
        struct scope* next = scope->next;
        free_scope(env, scope);
        scope = next;
    }
    struct symbol* symbol = env->free_symbols;
    while (symbol) {
        struct symbol* next = symbol->next;
        free(symbol);
        symbol = next;
    }
    free(env);
}

struct ast* env_find_enclosing_shader_or_func(struct env* env) {
    for (struct scope* scope = env->scope; scope; scope = scope->prev) {
        if (!scope->ast)
            continue;
        if (scope->ast->tag == AST_SHADER_DECL || scope->ast->tag == AST_FUNC_DECL)
            return scope->ast;
    }
    return NULL;
}

struct ast* env_find_enclosing_loop(struct env* env) {
    for (struct scope* scope = env->scope; scope; scope = scope->prev) {
        if (!scope->ast)
            continue;
        if (scope->ast->tag == AST_WHILE_LOOP ||
            scope->ast->tag == AST_FOR_LOOP ||
            scope->ast->tag == AST_DO_WHILE_LOOP)
        {
            return scope->ast;
        }
    }
    return NULL;
}

static inline struct symbol* find_first_symbol(struct scope* scope, const char* name) {
    struct symbol* const* symbol_ptr = symbol_table_find(&scope->symbol_table, &name);
    return symbol_ptr ? *symbol_ptr : NULL;
}

struct ast* env_find_one_symbol(struct env* env, const char* name) {
    struct scope* scope = env->scope;
    while (scope) {
        struct symbol* symbol = find_first_symbol(scope, name);
        if (symbol && !symbol->next)
            return symbol->ast;
        scope = scope->prev;
    }
    return NULL;
}

struct small_ast_vec env_find_all_symbols(struct env* env, const char* name) {
    struct small_ast_vec symbols;
    small_ast_vec_init(&symbols);

    struct scope* scope = env->scope;
    while (scope) {
        struct symbol* symbol = find_first_symbol(scope, name);
        while (symbol) {
            small_ast_vec_push(&symbols, &symbol->ast);
            symbol = symbol->next;
        }
        scope = scope->prev;
    }
    return symbols;
}

bool env_insert_symbol(struct env* env, const char* name, struct ast* ast, bool allow_overload) {
    struct symbol* first_symbol = find_first_symbol(env->scope, name);
    if (first_symbol && (!allow_overload || !first_symbol->allow_overload))
        return false;
    struct symbol* symbol = alloc_symbol(env);
    symbol->allow_overload = allow_overload;
    if (first_symbol) {
        symbol->next = first_symbol->next;
        symbol->ast  = first_symbol->ast;
        first_symbol->ast = ast;
        first_symbol->next = symbol;
    } else {
        symbol->ast = ast;
        [[maybe_unused]] bool was_inserted = symbol_table_insert(&env->scope->symbol_table, &name, &symbol);
        assert(was_inserted);
    }
    return true;
}

void env_push_scope(struct env* env, struct ast* ast) {
    struct scope* scope = env->scope;
    if (!scope->next)
        scope->next = alloc_scope(scope);
    env->scope = scope->next;
    env->scope->ast = ast;
}

void env_pop_scope(struct env* env) {
    assert(env->scope->prev);
    clear_scope(env, env->scope);
    env->scope = env->scope->prev;
}
