#include "preprocessor.h"
#include "file_cache.h"
#include "lexer.h"

#include <overture/hash.h>
#include <overture/mem.h>
#include <overture/str_pool.h>
#include <overture/mem_pool.h>
#include <overture/map.h>

#include <string.h>
#include <assert.h>

#define DIRECTIVE_LIST(x) \
    x(DEFINE, "define") \
    x(INCLUDE, "include") \
    x(IF, "if") \
    x(ELSE, "else") \
    x(ELIF, "elif") \
    x(IFDEF, "ifdef") \
    x(IFNDEF, "ifndef") \
    x(ENDIF, "endif") \
    x(ELIFDEF, "elifdef") \
    x(ELIFNDEF, "elifndef") \
    x(UNDEF, "undef") \
    x(PRAGMA, "pragma") \
    x(FILE, "file") \
    x(LINE, "line") \
    x(WARNING, "warning") \
    x(ERROR, "error")

struct cond {
    struct file_loc loc;
    bool is_parent_active;
    bool is_active;
    bool was_active;
};

VEC_DEFINE(cond_stack, struct cond, PRIVATE)

struct context {
    struct context* prev;
    struct source_file* source_file;
    struct token_vec tokens;
    struct cond_stack cond_stack;
    struct file_loc line_loc;
    size_t current_token;
    bool is_active;
    bool on_new_line;
};

enum directive {
    DIRECTIVE_NONE,
#define x(name, ...) DIRECTIVE_##name,
    DIRECTIVE_LIST(x)
#undef x
};

VEC_DEFINE(param_vec, const char*, PRIVATE)

struct macro {
    struct param_vec params;
    struct token_vec tokens;
};

static inline uint32_t hash_macro_name(uint32_t h, const char* const* name) {
    return hash_uint64(h, (uint64_t)*name);
}

static inline bool is_macro_name_equal(const char* const* name, const char* const* other_name) {
    return *name == *other_name;
}

MAP_DEFINE(macro_map, const char*, struct macro, hash_macro_name, is_macro_name_equal, PRIVATE)

struct preprocessor {
    struct log* log;
    struct context* context;
    struct file_cache* file_cache;
    struct macro_map macros;
    struct preprocessor_config config;
    struct str_pool* str_pool;
    struct mem_pool mem_pool;
};

static inline struct macro alloc_macro(void) {
    return (struct macro) {
        .params = param_vec_create(),
        .tokens = token_vec_create()
    };
}

static inline void free_macro(struct macro* macro) {
    param_vec_destroy(&macro->params);
    token_vec_destroy(&macro->tokens);
}

static inline struct context* alloc_context(struct context* prev, struct source_file* source_file) {
    struct context* context = xcalloc(1, sizeof(struct context));
    context->prev = prev;
    context->source_file = source_file;
    context->tokens = token_vec_create();
    context->cond_stack = cond_stack_create();
    context->on_new_line = true;
    context->is_active = true;
    return context;
}

static inline void free_context(struct context* context) {
    token_vec_destroy(&context->tokens);
    cond_stack_destroy(&context->cond_stack);
    free(context);
}

static inline void push_context(struct preprocessor* preprocessor, struct context* context) {
    context->prev = preprocessor->context;
    preprocessor->context = context;
}

static inline struct context* pop_context(struct preprocessor* preprocessor) {
    assert(preprocessor->context);
    if (!cond_stack_is_empty(&preprocessor->context->cond_stack)) {
        struct cond* last_cond = cond_stack_last(&preprocessor->context->cond_stack);
        log_error(preprocessor->log, &last_cond->loc, "unterminated #if directive");
    }
    struct context* prev_context = preprocessor->context->prev;
    free_context(preprocessor->context);
    return preprocessor->context = prev_context;
}

static inline const struct token_vec* context_tokens(const struct context* context) {
    return context->source_file ? &context->source_file->tokens : &context->tokens;
}

static inline size_t context_size(const struct context* context) {
    return context_tokens(context)->elem_count;
}

static inline struct token peek_token(const struct context* context) {
    return context_tokens(context)->elems[context->current_token];
}

static inline struct token last_token(const struct context* context) {
    return context_tokens(context)->elems[context_size(context) - 1];
}

static inline struct token read_token(struct preprocessor* preprocessor) {
    struct context* context = preprocessor->context;
    assert(context);

    while (context->current_token >= context_size(context)) {
        if (!context->prev)
            return last_token(context);
        context = pop_context(preprocessor);
    }

    struct token token = peek_token(context);
    bool is_new_line = token.tag == TOKEN_NL;
    if (context->on_new_line) {
        context->line_loc = token.loc;
    } else if (!is_new_line) {
        context->line_loc.end = token.loc.end;
    }
    context->on_new_line = is_new_line;
    context->current_token++;
    return token;
}

static inline struct token eat_token(struct preprocessor* preprocessor, [[maybe_unused]] enum token_tag tag) {
    struct token token = read_token(preprocessor);
    assert(token.tag == tag);
    return token;
}

static inline bool accept_token(struct preprocessor* preprocessor, enum token_tag tag) {
    if (peek_token(preprocessor->context).tag == tag) {
        eat_token(preprocessor, tag);
        return true;
    }
    return false;
}

static inline bool expect_token(struct preprocessor* preprocessor, enum token_tag tag) {
    if (!accept_token(preprocessor, tag)) {
        struct token token = peek_token(preprocessor->context);
        struct str_view str_view = preprocessor_view(preprocessor, &token);
        log_error(preprocessor->log, &token.loc,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)str_view.length, str_view.data);
        return false;
    }
    return true;
}

static inline enum directive directive_from_string(struct str_view string) {
#define x(name, str) if (str_view_is_equal(&string, &STR_VIEW(str))) return DIRECTIVE_##name;
    DIRECTIVE_LIST(x)
#undef x
    return DIRECTIVE_NONE;
}

static inline struct file_loc eat_extra_tokens(struct preprocessor* preprocessor, const char* directive_name) {
    struct file_loc loc;
    bool has_extra_tokens = false;
    while (
        !accept_token(preprocessor, TOKEN_NL) &&
        !accept_token(preprocessor, TOKEN_EOF))
    {
        struct token token = read_token(preprocessor);
        if (!has_extra_tokens)
            loc = token.loc;
        loc.end = token.loc.end;
        has_extra_tokens = true;
    }
    if (directive_name && has_extra_tokens)
        log_error(preprocessor->log, &loc, "extra tokens at end of #%s directive", directive_name);
    return loc;
}

static inline void preprocessor_error(struct preprocessor* preprocessor, const char* msg) {
    struct token token = read_token(preprocessor);
    struct str_view str_view = preprocessor_view(preprocessor, &token);
    log_error(preprocessor->log, &token.loc,
        "expected %s, but got '%.*s'",
        msg, (int)str_view.length, str_view.data);
}

struct preprocessor* preprocessor_open(
    struct log* log,
    const char* file_name,
    struct file_cache* file_cache,
    const struct preprocessor_config* config)
{
    struct source_file* source_file = file_cache_insert(file_cache, file_name);
    if (!source_file)
        return NULL;

    struct preprocessor* preprocessor = xmalloc(sizeof(struct preprocessor));
    preprocessor->log = log;
    preprocessor->context = NULL;
    preprocessor->file_cache = file_cache;
    preprocessor->config = *config;
    preprocessor->macros = macro_map_create();
    preprocessor->mem_pool = mem_pool_create();
    preprocessor->str_pool = str_pool_create(&preprocessor->mem_pool);

    push_context(preprocessor, alloc_context(NULL, source_file));
    return preprocessor;
}

void preprocessor_close(struct preprocessor* preprocessor) {
    while (preprocessor->context)
        pop_context(preprocessor);
    MAP_FOREACH_VAL(struct macro, macro, preprocessor->macros) {
        free_macro(macro);
    }
    macro_map_destroy(&preprocessor->macros);
    str_pool_destroy(preprocessor->str_pool);
    mem_pool_destroy(&preprocessor->mem_pool);
    free(preprocessor);
}

static void show_error(struct log* log, const char* file_data, const struct token* token) {
    assert(token->tag == TOKEN_ERROR);
    switch (token->error) {
        case TOKEN_ERROR_INVALID:
            {
                struct str_view view = token_view(token, file_data);
                log_error(log, &token->loc, "invalid token '%.*s'", (int)view.length, view.data);
            }
            break;
        case TOKEN_ERROR_UNTERMINATED_COMMENT:
            log_error(log, &token->loc, "unterminated multi-line comment");
            break;
        case TOKEN_ERROR_UNTERMINATED_STRING:
            log_error(log, &token->loc, "unterminated string");
            break;
        default:
            assert(false && "invalid token error");
            break;
    }
}

static inline enum directive peek_directive(struct preprocessor* preprocessor) {
    struct token token = peek_token(preprocessor->context);
    return directive_from_string(preprocessor_view(preprocessor, &token));
}

static inline int parse_literal(struct preprocessor* preprocessor) {
    return eat_token(preprocessor, TOKEN_INT_LITERAL).int_literal;
}

static inline int parse_condition(struct preprocessor* preprocessor) {
    switch (peek_token(preprocessor->context).tag) {
        case TOKEN_INT_LITERAL: return parse_literal(preprocessor);
        default:
            preprocessor_error(preprocessor, "condition");
            return 0;
    }
}

static const char* parse_ident(struct preprocessor* preprocessor) {
    struct token token = peek_token(preprocessor->context);
    const char* ident = str_pool_insert_view(preprocessor->str_pool, preprocessor_view(preprocessor, &token));
    expect_token(preprocessor, TOKEN_IDENT);
    return ident;
}

static void enter_if(struct preprocessor* preprocessor, bool is_active) {
    bool is_parent_active = true;
    if (!cond_stack_is_empty(&preprocessor->context->cond_stack)) {
        struct cond* last_cond = cond_stack_last(&preprocessor->context->cond_stack);
        is_parent_active = last_cond->is_parent_active & last_cond->is_active;
    }
    cond_stack_push(&preprocessor->context->cond_stack, &(struct cond) {
        .is_parent_active = is_parent_active,
        .is_active = is_active,
        .was_active = is_active,
        .loc = preprocessor->context->line_loc
    });
    preprocessor->context->is_active &= is_active;
}

static inline void enter_elif(struct preprocessor* preprocessor, bool is_active) {
    assert(!cond_stack_is_empty(&preprocessor->context->cond_stack));
    struct cond* last_cond = cond_stack_last(&preprocessor->context->cond_stack);
    is_active &= !last_cond->was_active;
    last_cond->was_active |= is_active;
    last_cond->is_active = is_active;
    preprocessor->context->is_active = last_cond->is_parent_active & is_active;
}

static inline bool error_on_empty_cond_stack(struct preprocessor* preprocessor, const char* directive_name) {
    if (cond_stack_is_empty(&preprocessor->context->cond_stack)) {
        log_error(preprocessor->log, &preprocessor->context->line_loc, "#%s without #if", directive_name);
        return true;
    }
    return false;
}

static void parse_if(struct preprocessor* preprocessor) {
    enter_if(preprocessor, parse_condition(preprocessor) != 0);
    eat_extra_tokens(preprocessor, "if");
}

static void parse_else(struct preprocessor* preprocessor) {
    if (!error_on_empty_cond_stack(preprocessor, "else"))
        enter_elif(preprocessor, true);
    eat_extra_tokens(preprocessor, "else");
}

static void parse_elif(struct preprocessor* preprocessor) {
    bool is_active = parse_condition(preprocessor) != 0;
    if (!error_on_empty_cond_stack(preprocessor, "elif"))
        enter_elif(preprocessor, is_active);
    eat_extra_tokens(preprocessor, "elif");
}

static void parse_endif(struct preprocessor* preprocessor) {
    bool is_active = true;
    if (!error_on_empty_cond_stack(preprocessor, "endif")) {
        is_active = cond_stack_last(&preprocessor->context->cond_stack)->is_parent_active;
        cond_stack_pop(&preprocessor->context->cond_stack);
    }
    preprocessor->context->is_active = is_active;
    eat_extra_tokens(preprocessor, "endif");
}

static inline bool is_defined(struct preprocessor* preprocessor, const char* ident) {
    return macro_map_find(&preprocessor->macros, &ident) != NULL;
}

static inline void parse_ifdef_or_ifndef(struct preprocessor* preprocessor, bool is_ifndef) {
    bool is_active = is_defined(preprocessor, parse_ident(preprocessor)) ^ is_ifndef;
    enter_if(preprocessor, is_active);
    eat_extra_tokens(preprocessor, is_ifndef ? "ifndef" : "ifdef");
}

static void parse_ifdef(struct preprocessor* preprocessor) {
    parse_ifdef_or_ifndef(preprocessor, false);
}

static void parse_ifndef(struct preprocessor* preprocessor) {
    parse_ifdef_or_ifndef(preprocessor, true);
}

static void parse_elifdef_or_elifndef(struct preprocessor* preprocessor, bool is_elifndef) {
    const char* directive_name = is_elifndef ? "elifndef" : "elifdef";
    bool is_active = is_defined(preprocessor, parse_ident(preprocessor)) ^ is_elifndef;
    if (!error_on_empty_cond_stack(preprocessor, directive_name))
        enter_elif(preprocessor, is_active);
    eat_extra_tokens(preprocessor, directive_name);
}

static void parse_elifdef(struct preprocessor* preprocessor) {
    parse_elifdef_or_elifndef(preprocessor, false);
}

static void parse_elifndef(struct preprocessor* preprocessor) {
    parse_elifdef_or_elifndef(preprocessor, true);
}

static void parse_define(struct preprocessor* preprocessor) {
    const struct file_loc loc = peek_token(preprocessor->context).loc;
    const char* name = parse_ident(preprocessor);
    struct macro macro = alloc_macro();
    if (accept_token(preprocessor, TOKEN_LPAREN)) {
        while (peek_token(preprocessor->context).tag == TOKEN_IDENT) {
            param_vec_push(&macro.params, (const char*[]) { parse_ident(preprocessor) });
            if (!accept_token(preprocessor, TOKEN_COMMA))
                break;
        }
        expect_token(preprocessor, TOKEN_RPAREN);
    }
    struct token token;
    do {
        token = read_token(preprocessor);
        token_vec_push(&macro.tokens, &token);
    } while (token.tag != TOKEN_NL);
    struct macro* existing_macro = (struct macro*)macro_map_find(&preprocessor->macros, &name);
    if (existing_macro) {
        log_warn(preprocessor->log, &loc, "redefinition for macro '%s'", name);
        free_macro(existing_macro);
        *existing_macro = macro;
    } else {
        [[maybe_unused]] bool was_inserted = macro_map_insert(&preprocessor->macros, &name, &macro);
        assert(was_inserted);
    }
}

static void parse_undef(struct preprocessor* preprocessor) {
    const struct file_loc loc = peek_token(preprocessor->context).loc;
    const char* name = parse_ident(preprocessor);
    struct macro* macro = (struct macro*)macro_map_find(&preprocessor->macros, &name);
    if (!macro) {
        log_error(preprocessor->log, &loc, "unknown macro '%s'", name);
    } else {
        free_macro(macro);
        macro_map_remove(&preprocessor->macros, &name);
    }
    eat_extra_tokens(preprocessor, "undef");
}

static void parse_warning_or_error(struct preprocessor* preprocessor, bool is_error) {
    struct file_loc loc = eat_extra_tokens(preprocessor, NULL);
    struct source_file* source_file = file_cache_find(preprocessor->file_cache, loc.file_name);
    assert(source_file);
    struct str_view msg = file_loc_view(&loc, source_file->file_data);
    log_msg(is_error ? MSG_ERROR : MSG_WARN, preprocessor->log, &loc, "%.*s", (int)msg.length, msg.data);
}

static void parse_warning(struct preprocessor* preprocessor) {
    parse_warning_or_error(preprocessor, false);
}

static void parse_error(struct preprocessor* preprocessor) {
    parse_warning_or_error(preprocessor, true);
}

static inline void ignore_directive(struct preprocessor* preprocessor, const char* directive_name) {
    eat_extra_tokens(preprocessor, NULL);
    log_warn(preprocessor->log, &preprocessor->context->line_loc, "ignoring preprocessor directive '#%s'", directive_name);
}

static void parse_line(struct preprocessor* preprocessor) {
    ignore_directive(preprocessor, "line");
}

static void parse_file(struct preprocessor* preprocessor) {
    ignore_directive(preprocessor, "file");
}

static void parse_pragma(struct preprocessor* preprocessor) {
    ignore_directive(preprocessor, "pragma");
}

static void parse_directive(struct preprocessor* preprocessor, enum directive directive) {
    switch (directive) {
        case DIRECTIVE_IF:       parse_if(preprocessor);       break;
        case DIRECTIVE_ELSE:     parse_else(preprocessor);     break;
        case DIRECTIVE_ELIF:     parse_elif(preprocessor);     break;
        case DIRECTIVE_ENDIF:    parse_endif(preprocessor);    break;
        case DIRECTIVE_IFDEF:    parse_ifdef(preprocessor);    break;
        case DIRECTIVE_IFNDEF:   parse_ifndef(preprocessor);   break;
        case DIRECTIVE_ELIFDEF:  parse_elifdef(preprocessor);  break;
        case DIRECTIVE_ELIFNDEF: parse_elifndef(preprocessor); break;
        case DIRECTIVE_DEFINE:   parse_define(preprocessor);   break;
        case DIRECTIVE_UNDEF:    parse_undef(preprocessor);    break;
        case DIRECTIVE_WARNING:  parse_warning(preprocessor);  break;
        case DIRECTIVE_ERROR:    parse_error(preprocessor);    break;
        case DIRECTIVE_LINE:     parse_line(preprocessor);     break;
        case DIRECTIVE_FILE:     parse_file(preprocessor);     break;
        case DIRECTIVE_PRAGMA:   parse_pragma(preprocessor);   break;
        default:
            assert(false && "invalid preprocessor directive");
            break;
    }
}

struct token preprocessor_advance(struct preprocessor* preprocessor) {
    while (true) {
        bool on_new_line = preprocessor->context->on_new_line;
        struct token token = read_token(preprocessor);
        if (token.tag == TOKEN_HASH && on_new_line && preprocessor->context->source_file) {
            enum directive directive = peek_directive(preprocessor);
            if (directive != DIRECTIVE_NONE) {
                read_token(preprocessor);
                parse_directive(preprocessor, directive);
            }
            continue;
        } else if (token.tag == TOKEN_EOF) {
            if (preprocessor->context->prev) {
                pop_context(preprocessor);
                continue;
            }
        } else if (token.tag == TOKEN_NL || !preprocessor->context->is_active) {
            continue;
        } else if (token.tag == TOKEN_ERROR) {
            const char* file_data = preprocessor_view(preprocessor, &token).data;
            show_error(preprocessor->log, file_data, &token);
            continue;
        }

        return token;
    }
}

struct str_view preprocessor_view(struct preprocessor* preprocessor, const struct token* token) {
    struct source_file* source_file = file_cache_find(preprocessor->file_cache, token->loc.file_name);
    assert(source_file);
    return token_view(token, source_file->file_data);
}
