#include "preprocessor.h"
#include "lexer.h"

#include <overture/hash.h>
#include <overture/set.h>
#include <overture/mem.h>
#include <overture/file.h>

#include <stdlib.h>
#include <string.h>
#include <limits.h>
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
    x(LINE, "line") \
    x(WARNING, "warning") \
    x(ERROR, "error")

struct cond {
    struct file_loc loc;
    int value;
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

struct source_file {
    const char* file_name;
    const char* file_data;
    size_t file_size;
    struct token_vec tokens;
    const char* include_guard;
    bool has_include_guard;
};

enum directive {
    DIRECTIVE_NONE,
#define x(name, ...) DIRECTIVE_##name,
    DIRECTIVE_LIST(x)
#undef x
};

static inline char* canonicalize_file_name(const char* file_name) {
#if _XOPEN_SOURCE >= 500 || _DEFAULT_SOURCE || _BSD_SOURCE
    return realpath(file_name, NULL);
#elif WIN32
    return _fullpath(NULL, file_name, 0);
#else
    return NULL;
#endif
}

static inline uint32_t hash_source_file(uint32_t h, struct source_file* const* source_file) {
    return hash_string(h, (*source_file)->file_name);
}

static inline bool is_same_source_file(struct source_file* const* source_file, struct source_file* const* other) {
    return !strcmp((*source_file)->file_name, (*other)->file_name);
}

SET_DEFINE(source_file_set, struct source_file*, hash_source_file, is_same_source_file, PRIVATE)

struct preprocessor {
    struct log* log;
    struct context* context;
    struct token last_token;
    struct source_file_set source_files;
    struct preprocessor_config config;
};

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
        struct cond cond = preprocessor->context->cond_stack.elems[preprocessor->context->cond_stack.elem_count - 1];
        log_error(preprocessor->log, &cond.loc, "unterminated #if directive");
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

static inline struct token peek_token(const struct context* context, size_t offset) {
    size_t index = context->current_token + offset;
    size_t max_index = context_size(context) - 1;
    return context_tokens(context)->elems[index > max_index ? max_index : index];
}

static inline struct token read_token(struct context* context) {
    struct token token = peek_token(context, 0);
    bool end_reached = context->current_token >= context_size(context);
    bool is_new_line = token.tag == TOKEN_NL;
    if (context->on_new_line) {
        context->line_loc = token.loc;
    } else if (!is_new_line) {
        context->line_loc.end = token.loc.end;
    }
    context->current_token += end_reached ? 0 : 1;
    context->on_new_line = is_new_line;
    return token;
}

static inline struct token eat_token(struct context* context, enum token_tag tag) {
    struct token token = read_token(context);
    assert(token.tag == tag);
    return token;
}

static inline bool accept_token(struct context* context, enum token_tag tag) {
    if (peek_token(context, 0).tag == tag) {
        eat_token(context, tag);
        return true;
    }
    return false;
}

static inline bool expect_token(struct preprocessor* preprocessor, enum token_tag tag) {
    if (!accept_token(preprocessor->context, tag)) {
        struct token token = peek_token(preprocessor->context, 0);
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

static inline void eat_extra_tokens(struct preprocessor* preprocessor, const char* directive_name) {
    bool has_extra_tokens = false;
    while (
        !accept_token(preprocessor->context, TOKEN_NL) &&
        !accept_token(preprocessor->context, TOKEN_EOF))
    {
        has_extra_tokens = true;
        read_token(preprocessor->context);
    }
    if (has_extra_tokens) {
        log_error(preprocessor->log, &preprocessor->context->line_loc, "extra tokens at end of #%s directive", directive_name);
    }
}

static inline void parse_error(struct preprocessor* preprocessor, const char* msg) {
    struct token token = read_token(preprocessor->context);
    struct str_view str_view = preprocessor_view(preprocessor, &token);
    log_error(preprocessor->log, &token.loc,
        "expected %s, but got '%.*s'",
        msg, (int)str_view.length, str_view.data);
}

static inline struct source_file* alloc_source_file(const char* file_name) {
    struct source_file* source_file = xcalloc(1, sizeof(struct source_file));
    source_file->file_name = file_name;
    source_file->file_data = file_read(file_name, &source_file->file_size);
    source_file->tokens = token_vec_create();
    struct lexer lexer = lexer_create(
        source_file->file_name,
        source_file->file_data,
        source_file->file_size);
    while (true) {
        struct token token = lexer_advance(&lexer);
        token_vec_push(&source_file->tokens, &token);
        if (token.tag == TOKEN_EOF)
            break;
    }
    return source_file;
}

static inline void free_source_file(struct source_file* source_file) {
    free((char*)source_file->file_name);
    free((char*)source_file->file_data);
    free((char*)source_file->include_guard);
    token_vec_destroy(&source_file->tokens);
    free(source_file);
}

static inline struct source_file* find_source_file(struct preprocessor* preprocessor, const char* file_name) {
    struct source_file* source_file = &(struct source_file) { .file_name = file_name };
    struct source_file* const* existing_file = source_file_set_find(&preprocessor->source_files, &source_file);
    return existing_file ? *existing_file : NULL;
}

struct preprocessor* preprocessor_create(struct log* log, const struct preprocessor_config* config) {
    struct preprocessor* preprocessor = xmalloc(sizeof(struct preprocessor));
    preprocessor->log = log;
    preprocessor->context = NULL;
    preprocessor->source_files = source_file_set_create();
    preprocessor->config = *config;
    return preprocessor;
}

void preprocessor_destroy(struct preprocessor* preprocessor) {
    while (preprocessor->context)
        pop_context(preprocessor);
    SET_FOREACH(struct source_file*, source_file, preprocessor->source_files) {
        free_source_file(*source_file);
    }
    source_file_set_destroy(&preprocessor->source_files);
    free(preprocessor);
}

static inline const char* find_source_file_data(struct preprocessor* preprocessor, const char* file_name) {
    struct source_file* source_file = find_source_file(preprocessor, file_name);
    if (!source_file)
        return NULL;
    return source_file->file_data;
}

static inline struct source_file* get_or_insert_source_file(struct preprocessor* preprocessor, const char* file_name) {
    const char* canonical_name = canonicalize_file_name(file_name);
    struct source_file* existing_file = find_source_file(preprocessor, canonical_name ? canonical_name : file_name);
    if (existing_file) {
        free((char*)canonical_name);
        return existing_file;
    }

    if (!file_exists(file_name))
        return NULL;

    struct source_file* source_file = alloc_source_file(canonical_name ? canonical_name : strdup(file_name));
    [[maybe_unused]] bool was_inserted = source_file_set_insert(&preprocessor->source_files, &source_file);
    assert(was_inserted);
    return source_file;
}

bool preprocessor_open(struct preprocessor* preprocessor, const char* file_name) {
    struct source_file* source_file = get_or_insert_source_file(preprocessor, file_name);
    if (!source_file)
        return false;

    push_context(preprocessor, alloc_context(NULL, source_file));
    return true;
}

static void show_error(struct log* log, const char* file_data, const struct token* token) {
    assert(token->tag == TOKEN_ERROR);
    switch (token->error) {
        case TOKEN_ERROR_INVALID:
            {
                struct str_view view = token_view(file_data, token);
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
    struct token token = peek_token(preprocessor->context, 0);
    return directive_from_string(preprocessor_view(preprocessor, &token));
}

static inline int parse_literal(struct preprocessor* preprocessor) {
    return eat_token(preprocessor->context, TOKEN_INT_LITERAL).int_literal;
}

static inline int parse_condition(struct preprocessor* preprocessor) {
    switch (peek_token(preprocessor->context, 0).tag) {
        case TOKEN_INT_LITERAL: return parse_literal(preprocessor);
        default:
            parse_error(preprocessor, "condition");
            return 0;
    }
}

static void parse_if(struct preprocessor* preprocessor) {
    int value = parse_condition(preprocessor);
    cond_stack_push(&preprocessor->context->cond_stack, &(struct cond) {
        .value = value,
        .loc = preprocessor->context->line_loc
    });
    preprocessor->context->is_active &= value != 0;
    eat_extra_tokens(preprocessor, "if");
}

static void parse_endif(struct preprocessor* preprocessor) {
    if (cond_stack_is_empty(&preprocessor->context->cond_stack)) {
        log_error(preprocessor->log, &preprocessor->context->line_loc, "#endif without #if");
    } else {
        cond_stack_pop(&preprocessor->context->cond_stack);
    }
    bool is_active = true;
    VEC_FOREACH(struct cond, cond, preprocessor->context->cond_stack) {
        if (cond->value == 0) {
            is_active = false;
            break;
        }
    }
    preprocessor->context->is_active = is_active;
    eat_extra_tokens(preprocessor, "endif");
}

static void parse_directive(struct preprocessor* preprocessor, enum directive directive) {
    switch (directive) {
        case DIRECTIVE_IF:    parse_if(preprocessor);    break;
        case DIRECTIVE_ENDIF: parse_endif(preprocessor); break;
        default:
            assert(false && "invalid preprocessor directive");
            break;
    }
}

struct token preprocessor_advance(struct preprocessor* preprocessor) {
    while (true) {
        bool on_new_line = preprocessor->context->on_new_line;
        struct token token = read_token(preprocessor->context);
        if (token.tag == TOKEN_HASH && on_new_line && preprocessor->context->source_file)
        {
            enum directive directive = peek_directive(preprocessor);
            if (directive != DIRECTIVE_NONE) {
                read_token(preprocessor->context);
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
    const char* file_data = find_source_file_data(preprocessor, token->loc.file_name);
    if (!file_data)
        return (struct str_view) {};
    return token_view(file_data, token);
}
