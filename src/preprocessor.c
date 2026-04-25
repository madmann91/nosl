#include "preprocessor.h"
#include "file_cache.h"
#include "lexer.h"
#include "ast.h"

#include <overture/file.h>
#include <overture/hash.h>
#include <overture/mem.h>
#include <overture/str_pool.h>
#include <overture/mem_pool.h>
#include <overture/set.h>

#include <string.h>
#include <assert.h>
#include <inttypes.h>

#define TOKENS_AHEAD 2

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

enum cond_value {
    COND_FALSE,
    COND_TRUE,
    COND_PARSE,
    COND_IS_DEFINED,
    COND_IS_NOT_DEFINED
};

struct cond {
    struct file_loc loc;
    bool was_active;
    bool was_last_else;
};

VEC_DEFINE(cond_vec, struct cond, PRIVATE)

struct cond_stack {
    struct cond_vec conds;
    size_t inactive_cond_depth;
};

typedef void (*custom_macro_callback)(struct preprocessor*, struct file_loc*);

struct macro {
    bool has_params;
    bool is_variadic;
    bool is_disabled;
    size_t param_count;
    const char* name;
    struct token_vec tokens;
    struct file_loc loc;
    custom_macro_callback callback;
};

enum directive {
    DIRECTIVE_NONE,
#define x(name, ...) DIRECTIVE_##name,
    DIRECTIVE_LIST(x)
#undef x
};

enum context_tag {
    CONTEXT_SOURCE_FILE,
    CONTEXT_TOKEN_BUFFER
};

struct source_file {
    struct cached_file* cached_file;
    struct lexer lexer;
    struct cond_stack cond_stack;
    const char* displayed_file_name;
    uint32_t displayed_line;
};

struct token_buffer {
    size_t token_index;
    struct token_vec tokens;
};

struct context {
    enum context_tag tag;
    bool is_finalized;
    bool is_active;
    struct token ahead[TOKENS_AHEAD];
    struct macro* macro;
    struct context* prev;
    union {
        struct source_file  source_file;
        struct token_buffer token_buffer;
    };
};

struct macro_arg {
    struct file_loc loc;
    struct token_vec unexpanded_tokens;
    struct token_vec expanded_tokens;
    bool is_expanded;
};

VEC_DEFINE(macro_arg_vec, struct macro_arg, PRIVATE)

static inline uint32_t hash_macro_name(uint32_t h, struct macro* const* macro) {
    return hash_uint64(h, (uint64_t)(*macro)->name);
}

static inline bool is_macro_name_equal(struct macro* const* macro, struct macro* const* other_macro) {
    return (*macro)->name == (*other_macro)->name;
}

SET_DEFINE(macro_set, struct macro*, hash_macro_name, is_macro_name_equal, PRIVATE)

struct preprocessor {
    struct log* log;
    const char* const* include_paths;
    struct context* context;
    struct macro_set macros;
    struct str_pool* str_pool;
    struct mem_pool mem_pool;
    struct file_cache* file_cache;
    struct cond_stack cond_stack;
    size_t inactive_cond_depth;
};

SMALL_VEC_DEFINE(small_str_view_vec, struct str_view, PRIVATE)
SMALL_VEC_DEFINE(small_index_vec, size_t, PRIVATE)

static struct token expand_token(struct preprocessor*);
static int parse_cond(struct preprocessor*, bool);

static inline void cleanup_macro(struct macro* macro) {
    token_vec_destroy(&macro->tokens);
    memset(macro, 0, sizeof(struct macro));
}

static inline void free_macro(struct macro* macro) {
    cleanup_macro(macro);
    free(macro);
}

static inline struct macro* find_macro(struct preprocessor* preprocessor, const char* name) {
    struct macro* macro_ptr = &(struct macro) { .name = name };
    struct macro* const* macro = macro_set_find(&preprocessor->macros, &macro_ptr);
    return macro ? *macro : NULL;
}

static inline void insert_macro(struct preprocessor* preprocessor, const struct macro* macro) {
    struct macro* existing_macro = find_macro(preprocessor, macro->name);
    if (existing_macro) {
        log_warn(preprocessor->log, &macro->loc, "redefinition for macro '%s'", macro->name);
        log_note(preprocessor->log, &existing_macro->loc, "previously declared here");
        cleanup_macro(existing_macro);
        *existing_macro = *macro;
    } else {
        struct macro* new_macro = xcalloc(1, sizeof(struct macro));
        *new_macro = *macro;
        [[maybe_unused]] bool was_inserted = macro_set_insert(&preprocessor->macros, &new_macro);
        assert(was_inserted);
    }
}

static inline struct context* alloc_context(struct context* prev, enum context_tag tag) {
    struct context* context = xcalloc(1, sizeof(struct context));
    context->tag = tag;
    context->prev = prev;
    context->is_active = true;
    for (int i = 0; i < TOKENS_AHEAD; ++i)
        context->ahead[i] = (struct token) { .tag = TOKEN_ERROR, .error = TOKEN_ERROR_INVALID };
    return context;
}

static inline void advance_context(struct context* context) {
    assert(context->is_finalized);
    struct token token = { .tag = TOKEN_EOF };
    if (context->tag == CONTEXT_SOURCE_FILE) {
        token = lexer_advance(&context->source_file.lexer);
        token.loc.displayed_file_name = context->source_file.displayed_file_name;
        token.loc.displayed_line = context->source_file.displayed_line;
    } else if (context->tag == CONTEXT_TOKEN_BUFFER) {
        if (context->token_buffer.token_index < context->token_buffer.tokens.elem_count)
            token = context->token_buffer.tokens.elems[context->token_buffer.token_index++];
    } else {
        assert(false && "invalid context");
    }
    for (int i = 0; i < TOKENS_AHEAD - 1; ++i)
        context->ahead[i] = context->ahead[i + 1];
    context->ahead[TOKENS_AHEAD - 1] = token;
    if (context->tag == CONTEXT_SOURCE_FILE && context->ahead[0].tag == TOKEN_NL)
        context->source_file.displayed_line++;
}

static inline void finalize_context(struct context* context) {
    context->is_finalized = true;
    for (int i = 0; i < TOKENS_AHEAD; ++i)
        advance_context(context);
}

static inline struct context* alloc_source_file_context(struct cached_file* cached_file, struct context* prev) {
    struct context* context = alloc_context(prev, CONTEXT_SOURCE_FILE);
    context->source_file.cond_stack.conds = cond_vec_create();
    context->source_file.lexer = lexer_create(cached_file->file_name, cached_file->file_data);
    context->source_file.cached_file = cached_file;
    context->source_file.displayed_file_name = cached_file->file_name;
    context->source_file.displayed_line = 1;
    finalize_context(context);
    return context;
}

static inline struct context* alloc_token_buffer_context(struct context* prev) {
    struct context* context = alloc_context(prev, CONTEXT_TOKEN_BUFFER);
    context->tag = CONTEXT_TOKEN_BUFFER;
    context->token_buffer.tokens = token_vec_create();
    return context;
}

static inline struct context* alloc_expanded_macro_context(struct context* prev, struct macro* macro) {
    struct context* context = alloc_token_buffer_context(prev);
    context->macro = macro;
    return context;
}

static inline void free_context(struct context* context) {
    if (context->tag == CONTEXT_SOURCE_FILE) {
        cond_vec_destroy(&context->source_file.cond_stack.conds);
    } else if (context->tag == CONTEXT_TOKEN_BUFFER) {
        token_vec_destroy(&context->token_buffer.tokens);
    } else {
        assert(false && "invalid context");
    }
    free(context);
}

static inline void push_context(struct preprocessor* preprocessor, struct context* context) {
    if (context->macro)
        context->macro->is_disabled = true;
    context->prev = preprocessor->context;
    preprocessor->context = context;
}

static inline struct cond* find_last_cond(const struct context* context) {
    if (context->tag != CONTEXT_SOURCE_FILE || cond_vec_is_empty(&context->source_file.cond_stack.conds))
        return NULL;
    return cond_vec_last((struct cond_vec*)&context->source_file.cond_stack.conds);
}

static inline struct context* pop_context(struct preprocessor* preprocessor) {
    assert(preprocessor->context);
    struct cond* last_cond = find_last_cond(preprocessor->context);
    if (last_cond)
        log_error(preprocessor->log, &last_cond->loc, "unterminated '#if'");

    if (preprocessor->context->macro)
        preprocessor->context->macro->is_disabled = false;

    struct context* prev_context = preprocessor->context->prev;
    free_context(preprocessor->context);
    return preprocessor->context = prev_context;
}

static inline struct context* update_or_peek_context(struct preprocessor* preprocessor, bool update_context) {
    struct context* context = preprocessor->context;
    assert(context);
    while (context->ahead->tag == TOKEN_EOF) {
        if (!context->prev)
            break;
        context = update_context ? pop_context(preprocessor) : context->prev;
    }
    return context;
}

static inline struct token read_or_peek_token(struct preprocessor* preprocessor, bool update_context) {
    struct context* context = update_or_peek_context(preprocessor, update_context);
    struct token token = context->ahead[0];
    if (update_context && token.tag != TOKEN_EOF)
        advance_context(context);
    return token;
}

static inline struct token read_token(struct preprocessor* preprocessor) {
    return read_or_peek_token(preprocessor, true);
}

static inline struct token peek_token(struct preprocessor* preprocessor) {
    return read_or_peek_token(preprocessor, false);
}

static inline void eat_token(struct preprocessor* preprocessor, [[maybe_unused]] enum token_tag tag) {
    [[maybe_unused]] struct token token = read_token(preprocessor);
    assert(token.tag == tag);
}

static inline bool accept_token(struct preprocessor* preprocessor, enum token_tag tag) {
    if (peek_token(preprocessor).tag == tag) {
        eat_token(preprocessor, tag);
        return true;
    }
    return false;
}

static inline bool expect_token(struct preprocessor* preprocessor, enum token_tag tag) {
    if (!accept_token(preprocessor, tag)) {
        struct token token = peek_token(preprocessor);
        struct str_view contents = token_printable_contents(&token);
        log_error(preprocessor->log, &token.loc,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)contents.length, contents.data);
        return false;
    }
    return true;
}

static inline struct macro_arg macro_arg_create(void) {
    return (struct macro_arg) {
        .expanded_tokens = token_vec_create(),
        .unexpanded_tokens = token_vec_create()
    };
}

static inline void macro_arg_destroy(struct macro_arg* macro_arg) {
    token_vec_destroy(&macro_arg->expanded_tokens);
    token_vec_destroy(&macro_arg->unexpanded_tokens);
    memset(macro_arg, 0, sizeof(struct macro_arg));
}

static const struct token_vec* expand_macro_arg(struct preprocessor* preprocessor, struct macro_arg* macro_arg) {
    if (macro_arg->is_expanded)
        return &macro_arg->expanded_tokens;

    // Push a context with all the tokens of the macro argument on it, and then expand every token
    // from that context, to get the fully expanded tokens of the macro argument.
    struct context* context = alloc_token_buffer_context(preprocessor->context);
    VEC_FOREACH(struct token, token, macro_arg->unexpanded_tokens)
        token_vec_push(&context->token_buffer.tokens, token);
    token_vec_push(&context->token_buffer.tokens, &(struct token) { .tag = TOKEN_STOP_EXPAND });
    finalize_context(context);
    push_context(preprocessor, context);

    while (true) {
        struct token token = expand_token(preprocessor);
        if (token.tag == TOKEN_STOP_EXPAND)
            break;
        token_vec_push(&macro_arg->expanded_tokens, &token);
    }

    macro_arg->is_expanded = true;
    return &macro_arg->expanded_tokens;
}

static inline struct token make_string_literal_token(struct str_view contents, const struct file_loc* loc) {
    return (struct token) {
        .tag = TOKEN_STRING_LITERAL,
        .loc = *loc,
        .contents = contents,
        .string_literal = str_view_shrink(contents, 1, 1)
    };
}

static struct token stringify_macro_arg(struct preprocessor* preprocessor, struct macro_arg* macro_arg) {
    struct str str = str_create();
    str_push(&str, '\"');
    for (size_t i = 0; i < macro_arg->unexpanded_tokens.elem_count; ++i) {
        struct token token = macro_arg->unexpanded_tokens.elems[i];
        if (token.has_space_before && i > 0)
            str_push(&str, ' ');
        if (token.tag == TOKEN_STRING_LITERAL) {
            str_append(&str, STR_VIEW("\\\""));
            str_append(&str, str_view_shrink(token.contents, 1, 1));
            str_append(&str, STR_VIEW("\\\""));
        } else {
            str_append(&str, token.contents);
        }
    }
    str_push(&str, '\"');
    const char* contents = str_pool_insert_view(preprocessor->str_pool, str_to_view(&str));
    str_destroy(&str);

    return make_string_literal_token(STR_VIEW(contents), &macro_arg->loc);
}

static inline struct token concatenate_tokens(
    struct preprocessor* preprocessor,
    const struct token* left_token,
    const struct token* right_token,
    const struct file_loc* concat_loc)
{
    struct str concat_str = str_create();
    str_append(&concat_str, left_token->contents);
    str_append(&concat_str, right_token->contents);
    const char* lexer_data = str_pool_insert_view(preprocessor->str_pool, str_to_view(&concat_str));
    str_destroy(&concat_str);

    struct lexer lexer = lexer_create(concat_loc->file_name, STR_VIEW(lexer_data));
    struct token first_token = lexer_advance(&lexer);
    struct token next_token = lexer_advance(&lexer);
    if (first_token.tag == TOKEN_ERROR || next_token.tag != TOKEN_EOF) {
        log_error(preprocessor->log, &left_token->loc, "cannot concatenate '%.*s' and '%.*s'",
            (int)left_token ->contents.length, left_token ->contents.data,
            (int)right_token->contents.length, right_token->contents.data);
    }
    first_token.loc = *concat_loc;
    return first_token;
}

static inline bool parse_macro_args(
    struct preprocessor* preprocessor,
    const struct macro* macro,
    struct macro_arg_vec* macro_args,
    const struct file_loc* loc)
{
    if (!macro->has_params)
        return true;

    eat_token(preprocessor, TOKEN_LPAREN);
    struct macro_arg* last_arg = NULL;
    size_t paren_depth = 0;

    while (true) {
        struct token token = read_token(preprocessor);

        // Allocate a macro argument, if there is not one already.
        if (!last_arg) {
            struct macro_arg macro_arg = macro_arg_create();
            macro_arg_vec_push(macro_args, &macro_arg);
            last_arg = macro_arg_vec_last(macro_args);
            last_arg->loc = token.loc;
        }

        if (token.tag == TOKEN_EOF) {
            log_error(preprocessor->log, &token.loc, "unterminated argument list for macro '%s'", macro->name);
            return false;
        } else if (token.tag == TOKEN_RPAREN) {
            if (paren_depth == 0)
                break;
            paren_depth--;
        } else if (token.tag == TOKEN_LPAREN) {
            paren_depth++;
        }

        // Everything after the regular macro arguments belongs to __VA_ARG__, including commas.
        if (token.tag == TOKEN_COMMA && paren_depth == 0 && macro_args->elem_count <= macro->param_count) {
            last_arg = NULL;
            continue;
        }

        token_vec_push(&last_arg->unexpanded_tokens, &token);
    }

    if (macro->param_count > macro_args->elem_count || (!macro->is_variadic && macro->param_count != macro_args->elem_count)) {
        log_error(preprocessor->log, loc, "expected %zu argument(s) to macro '%s', but got %zu",
            macro->param_count, macro->name, macro_args->elem_count);
        return false;
    }
    return true;
}

static inline struct context* expand_macro_with_args(
    struct preprocessor* preprocessor,
    struct macro* macro,
    struct macro_arg* args,
    size_t arg_count,
    [[maybe_unused]] const struct file_loc* loc)
{
    assert(macro->param_count == arg_count || (arg_count >= macro->param_count && macro->is_variadic));
    bool should_concat_left = false;
    struct file_loc concat_loc = {};

    struct context* context = alloc_expanded_macro_context(preprocessor->context, macro);
    for (size_t i = 0; i < macro->tokens.elem_count; ++i) {
        struct token macro_token = macro->tokens.elems[i];
        const struct token* expanded_tokens = &macro_token;
        size_t num_expanded_tokens = 1;

        if (macro_token.tag == TOKEN_MACRO_PARAM) {
            // May happen if there is no argument for '__VA_ARGS__', in which case we just skip this parameter.
            if (macro_token.macro_param_index >= arg_count)
                continue;

            const struct token_vec* arg_tokens = &args[macro_token.macro_param_index].unexpanded_tokens;
            const bool should_concat_right = i + 1 < macro->tokens.elem_count && macro->tokens.elems[i + 1].tag == TOKEN_CONCAT;
            if (!should_concat_left && !should_concat_right)
                arg_tokens = expand_macro_arg(preprocessor, &args[macro_token.macro_param_index]);

            expanded_tokens     = arg_tokens->elems;
            num_expanded_tokens = arg_tokens->elem_count;
        } else if (macro_token.tag == TOKEN_CONCAT) {
            should_concat_left = true;
            concat_loc = macro_token.loc;
            continue;
        } else if (macro_token.tag == TOKEN_HASH) {
            assert(i + 1 < macro->tokens.elem_count);
            assert(macro->tokens.elems[i + 1].tag == TOKEN_MACRO_PARAM);
            macro_token = stringify_macro_arg(preprocessor, &args[macro->tokens.elems[++i].macro_param_index]);
        }

        if (should_concat_left) {
            // There may not be a right-hand side or left-hand side to the concatenation operator,
            // if argument expansion produced no token on either side.
            if (num_expanded_tokens > 0 && !token_vec_is_empty(&context->token_buffer.tokens)) {
                struct token* last_token = token_vec_last(&context->token_buffer.tokens);
                *last_token = concatenate_tokens(preprocessor, last_token, &expanded_tokens[0], &concat_loc);
                expanded_tokens++;
                num_expanded_tokens--;
            }
            should_concat_left = false;
        }

        for (size_t i = 0; i < num_expanded_tokens; ++i)
            token_vec_push(&context->token_buffer.tokens, &expanded_tokens[i]);
    }

    finalize_context(context);
    return context;
}

static inline struct context* expand_macro(
    struct preprocessor* preprocessor,
    struct macro* macro,
    const struct file_loc* loc)
{
    struct context* context = NULL;
    struct macro_arg_vec macro_args = macro_arg_vec_create();
    if (parse_macro_args(preprocessor, macro, &macro_args, loc))
        context = expand_macro_with_args(preprocessor, macro, macro_args.elems, macro_args.elem_count, loc);

    VEC_FOREACH(struct macro_arg, macro_arg, macro_args)
        macro_arg_destroy(macro_arg);
    macro_arg_vec_destroy(&macro_args);
    return context;
}

static struct token expand_token(struct preprocessor* preprocessor) {
    while (true) {
        struct token token = read_token(preprocessor);
        if (token.tag != TOKEN_IDENT || !preprocessor->context->is_active)
            return token;

        const char* ident = str_pool_find_view(preprocessor->str_pool, token.contents);
        if (!ident)
            return token;

        struct macro* macro = find_macro(preprocessor, ident);
        if (!macro || macro->is_disabled)
            return token;

        const bool has_args = peek_token(preprocessor).tag == TOKEN_LPAREN;
        if (macro->has_params && !has_args)
            return token;

        if (macro->callback) {
            macro->callback(preprocessor, &token.loc);
        } else {
            struct context* context = expand_macro(preprocessor, macro, &token.loc);
            if (context)
                push_context(preprocessor, context);
        }
    }
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
    while (true) {
        struct token token = read_token(preprocessor);
        if (token.tag == TOKEN_NL || token.tag == TOKEN_EOF)
            break;
        if (!has_extra_tokens)
            loc = token.loc;
        loc.end = token.loc.end;
        has_extra_tokens = true;
    }
    if (directive_name && has_extra_tokens)
        log_error(preprocessor->log, &loc, "extra tokens after '#%s'", directive_name);
    return loc;
}

static inline struct source_file* find_containing_source_file(struct context* context) {
    while (context && context->tag != CONTEXT_SOURCE_FILE)
        context = context->prev;
    assert(context);
    assert(context->tag == CONTEXT_SOURCE_FILE);
    return &context->source_file;
}

static void expand_file_macro(struct preprocessor* preprocessor, struct file_loc* loc) {
    struct source_file* source_file = find_containing_source_file(preprocessor->context);
    struct context* context = alloc_token_buffer_context(preprocessor->context);

    struct str file_str = str_create();
    str_printf(&file_str, "\"%s\"", source_file->displayed_file_name);
    const char* file_string = str_pool_insert(preprocessor->str_pool, file_str.data);
    str_destroy(&file_str);

    struct token file_token = make_string_literal_token(STR_VIEW(file_string), loc);
    token_vec_push(&context->token_buffer.tokens, &file_token);
    finalize_context(context);
    push_context(preprocessor, context);
}

static void expand_line_macro(struct preprocessor* preprocessor, struct file_loc* loc) {
    struct source_file* source_file = find_containing_source_file(preprocessor->context);
    struct context* context = alloc_token_buffer_context(preprocessor->context);

    struct str line_str = str_create();
    str_printf(&line_str, "\"%"PRIu32"\"", source_file->displayed_line);
    const char* line_string = str_pool_insert(preprocessor->str_pool, line_str.data);
    str_destroy(&line_str);

    struct token line_token = {
        .tag = TOKEN_INT_LITERAL,
        .loc = *loc,
        .contents = STR_VIEW(line_string),
        .int_literal = source_file->displayed_line
    };
    token_vec_push(&context->token_buffer.tokens, &line_token);
    finalize_context(context);
    push_context(preprocessor, context);
}

static void register_custom_macro(
    struct preprocessor* preprocessor,
    const char* name,
    custom_macro_callback callback)
{
    insert_macro(preprocessor, &(struct macro) {
        .name = str_pool_insert(preprocessor->str_pool, name),
        .callback = callback
    });
}

static void register_standard_macros(struct preprocessor* preprocessor) {
    register_custom_macro(preprocessor, "__FILE__", expand_file_macro);
    register_custom_macro(preprocessor, "__LINE__", expand_line_macro);
}

struct preprocessor* preprocessor_open(
    struct log* log,
    struct file_cache* file_cache,
    const char* file_name,
    const char* const* include_paths)
{
    struct cached_file* cached_file = file_cache_read(file_cache, file_name);
    if (!cached_file)
        return NULL;

    file_cache_reset(file_cache);

    struct preprocessor* preprocessor = xmalloc(sizeof(struct preprocessor));
    preprocessor->log = log;
    preprocessor->context = NULL;
    preprocessor->file_cache = file_cache;
    preprocessor->include_paths = include_paths;
    preprocessor->macros = macro_set_create();
    preprocessor->mem_pool = mem_pool_create();
    preprocessor->str_pool = str_pool_create(&preprocessor->mem_pool);

    register_standard_macros(preprocessor);

    push_context(preprocessor, alloc_source_file_context(cached_file, NULL));
    return preprocessor;
}

void preprocessor_close(struct preprocessor* preprocessor) {
    while (preprocessor->context)
        pop_context(preprocessor);
    SET_FOREACH(struct macro*, macro, preprocessor->macros) {
        free_macro(*macro);
    }
    macro_set_destroy(&preprocessor->macros);
    str_pool_destroy(preprocessor->str_pool);
    mem_pool_destroy(&preprocessor->mem_pool);
    free(preprocessor);
}

static const char* parse_ident(struct preprocessor* preprocessor) {
    const char* ident = str_pool_insert_view(preprocessor->str_pool, peek_token(preprocessor).contents);
    expect_token(preprocessor, TOKEN_IDENT);
    return ident;
}

static int parse_primary_cond(struct preprocessor* preprocessor, bool should_eval) {
    struct token token = expand_token(preprocessor);
    switch (token.tag) {
        case TOKEN_INT_LITERAL:
            return token.int_literal;
        case TOKEN_TRUE:
            return 1;
        case TOKEN_FALSE:
            return 0;
        case TOKEN_LPAREN: {
            int cond = parse_cond(preprocessor, should_eval);
            expect_token(preprocessor, TOKEN_RPAREN);
            return cond;
        }
        case TOKEN_IDENT:
            if (str_view_is_equal(&token.contents, &STR_VIEW("defined"))) {
                bool has_paren = accept_token(preprocessor, TOKEN_LPAREN);
                const char* ident = parse_ident(preprocessor);
                if (has_paren)
                    expect_token(preprocessor, TOKEN_RPAREN);
                return should_eval && find_macro(preprocessor, ident) ? 1 : 0;
            }
            return 0;
        default:
            log_error(preprocessor->log, &token.loc,
                "expected condition, but got '%.*s'",
                (int)token.contents.length, token.contents.data);
            return 0;
    }
}

static int parse_prefix_cond(struct preprocessor* preprocessor, bool should_eval) {
    enum unary_expr_tag tag = token_tag_to_unary_expr_tag(peek_token(preprocessor).tag, true);
    if (unary_expr_tag_is_inc_or_dec(tag))
        tag = UNARY_EXPR_INVALID;
    if (tag == UNARY_EXPR_INVALID)
        return parse_primary_cond(preprocessor, should_eval);

    read_token(preprocessor);
    int cond = parse_prefix_cond(preprocessor, should_eval);
    if (should_eval) {
        switch (tag) {
            case UNARY_EXPR_INVALID:
                [[fallthrough]];
            case UNARY_EXPR_PLUS:     return cond;
            case UNARY_EXPR_NOT:      return !cond;
            case UNARY_EXPR_BIT_NOT:  return ~cond;
            case UNARY_EXPR_NEG:      return -cond;
            default:
                assert(false && "invalid unary expression");
                break;
        }
    }
    return cond;
}

static inline int parse_binary_cond(struct preprocessor* preprocessor, int left, int prec, bool should_eval) {
    while (true) {
        struct token token = peek_token(preprocessor);
        enum binary_expr_tag tag = token_tag_to_binary_expr_tag(token.tag);
        if (tag == BINARY_EXPR_INVALID || binary_expr_tag_is_assign(tag))
            return left;

        int next_prec = binary_expr_tag_precedence(tag);
        if (next_prec < prec) {
            left = parse_binary_cond(preprocessor, left, next_prec, should_eval);
        } else if (next_prec > prec) {
            return left;
        } else {
            read_token(preprocessor);

            bool should_eval_right = should_eval;
            if (left == 0 && tag == BINARY_EXPR_LOGIC_AND)
                should_eval_right = false;
            else if (left != 0 && tag == BINARY_EXPR_LOGIC_OR)
                should_eval_right = false;

            int right = parse_binary_cond(preprocessor, parse_prefix_cond(preprocessor, should_eval_right), prec - 1, should_eval_right);
            if (should_eval) {
                switch (tag) {
                    case BINARY_EXPR_MUL:       left = left *  right; break;
                    case BINARY_EXPR_ADD:       left = left +  right; break;
                    case BINARY_EXPR_SUB:       left = left -  right; break;
                    case BINARY_EXPR_LSHIFT:    left = left << right; break;
                    case BINARY_EXPR_RSHIFT:    left = left >> right; break;
                    case BINARY_EXPR_CMP_LT:    left = left <  right; break;
                    case BINARY_EXPR_CMP_LE:    left = left <= right; break;
                    case BINARY_EXPR_CMP_GT:    left = left >  right; break;
                    case BINARY_EXPR_CMP_GE:    left = left >= right; break;
                    case BINARY_EXPR_CMP_NE:    left = left != right; break;
                    case BINARY_EXPR_CMP_EQ:    left = left == right; break;
                    case BINARY_EXPR_BIT_AND:   left = left &  right; break;
                    case BINARY_EXPR_BIT_XOR:   left = left ^  right; break;
                    case BINARY_EXPR_BIT_OR:    left = left |  right; break;
                    case BINARY_EXPR_LOGIC_AND: left = left && right; break;
                    case BINARY_EXPR_LOGIC_OR:  left = left || right; break;

                    case BINARY_EXPR_DIV:
                    case BINARY_EXPR_REM:
                        if (right == 0) {
                            log_error(preprocessor->log, &token.loc, "division by zero while evaluating condition");
                            right = 1;
                        }
                        left = tag == BINARY_EXPR_DIV ? left / right : left % right;
                        break;

                    default:
                        assert(false && "invalid binary expression");
                        break;
                }
            }
        }
    }
    return left;
}

static int parse_ternary_cond(struct preprocessor* preprocessor, bool should_eval) {
    int cond = parse_binary_cond(preprocessor, parse_prefix_cond(preprocessor, should_eval), binary_expr_max_precedence(false), should_eval);
    if (!accept_token(preprocessor, TOKEN_QUESTION))
        return cond;

    int then_val = parse_cond(preprocessor, should_eval && cond != 0);
    expect_token(preprocessor, TOKEN_COLON);
    int else_val = parse_cond(preprocessor, should_eval && cond == 0);
    return cond ? then_val : else_val;
}

static int parse_cond(struct preprocessor* preprocessor, bool should_eval) {
    return parse_ternary_cond(preprocessor, should_eval);
}

static inline bool eval_cond(struct preprocessor* preprocessor, enum cond_value cond_value) {
    switch (cond_value) {
        case COND_PARSE:
            return parse_cond(preprocessor, true) != 0;
        case COND_IS_DEFINED:
        case COND_IS_NOT_DEFINED:
        {
            const char* ident = parse_ident(preprocessor);
            const bool is_defined = find_macro(preprocessor, ident) != NULL;
            return is_defined ^ (cond_value == COND_IS_NOT_DEFINED);
        }
        case COND_TRUE:
            return true;
        default:
            assert(false && "invalid condition value");
            [[fallthrough]];
        case COND_FALSE:
            return false;
    }
}

static void enter_if(
    struct preprocessor* preprocessor,
    const char* directive_name,
    enum cond_value cond_value,
    struct file_loc* loc)
{
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
    if (!preprocessor->context->is_active) {
        preprocessor->context->source_file.cond_stack.inactive_cond_depth++;
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

    const bool is_active = eval_cond(preprocessor, cond_value);
    cond_vec_push(
        &preprocessor->context->source_file.cond_stack.conds,
        &(struct cond) { .was_active = is_active, .loc = *loc });

    preprocessor->context->is_active &= is_active;
    eat_extra_tokens(preprocessor, directive_name);
}

static inline void enter_elif(
    struct preprocessor* preprocessor,
    const char* directive_name,
    enum cond_value cond_value,
    struct file_loc* loc)
{
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
    if (preprocessor->context->source_file.cond_stack.inactive_cond_depth > 0) {
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

    struct cond* last_cond = find_last_cond(preprocessor->context);
    assert(last_cond);

    if (last_cond->was_last_else)
        log_error(preprocessor->log, loc, "'#%s' after '#else'", directive_name);

    const bool is_active = eval_cond(preprocessor, cond_value) & !last_cond->was_active;
    last_cond->was_active |= is_active;
    last_cond->was_last_else = cond_value == COND_TRUE;
    preprocessor->context->is_active = is_active;
    eat_extra_tokens(preprocessor, directive_name);
}

static inline bool error_on_empty_cond_stack(
    struct preprocessor* preprocessor,
    const char* directive_name,
    struct file_loc* loc)
{
    if (!find_last_cond(preprocessor->context)) {
        log_error(preprocessor->log, loc, "'#%s' without '#if'", directive_name);
        return true;
    }
    return false;
}

static void parse_if(struct preprocessor* preprocessor, struct file_loc* loc) {
    enter_if(preprocessor, "if", COND_PARSE, loc);
}

static void parse_else(struct preprocessor* preprocessor, struct file_loc* loc) {
    if (!error_on_empty_cond_stack(preprocessor, "else", loc))
        enter_elif(preprocessor, "else", COND_TRUE, loc);
}

static void parse_elif(struct preprocessor* preprocessor, struct file_loc* loc) {
    if (!error_on_empty_cond_stack(preprocessor, "elif", loc))
        enter_elif(preprocessor, "elif", COND_PARSE, loc);
}

static void parse_endif(struct preprocessor* preprocessor, struct file_loc* loc) {
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
    if (preprocessor->context->source_file.cond_stack.inactive_cond_depth > 0) {
        preprocessor->context->source_file.cond_stack.inactive_cond_depth--;
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

    if (!error_on_empty_cond_stack(preprocessor, "endif", loc))
        cond_vec_pop(&preprocessor->context->source_file.cond_stack.conds);

    preprocessor->context->is_active = true;
    eat_extra_tokens(preprocessor, "endif");
}

static inline void parse_ifdef_or_ifndef(struct preprocessor* preprocessor, bool is_ifndef, struct file_loc* loc) {
    const char* directive_name = is_ifndef ? "ifndef" : "ifdef";
    enter_if(preprocessor, directive_name, is_ifndef ? COND_IS_NOT_DEFINED : COND_IS_DEFINED, loc);
}

static void parse_ifdef(struct preprocessor* preprocessor, struct file_loc* loc) {
    parse_ifdef_or_ifndef(preprocessor, false, loc);
}

static void parse_ifndef(struct preprocessor* preprocessor, struct file_loc* loc) {
    parse_ifdef_or_ifndef(preprocessor, true, loc);
}

static void parse_elifdef_or_elifndef(struct preprocessor* preprocessor, bool is_elifndef, struct file_loc* loc) {
    const char* directive_name = is_elifndef ? "elifndef" : "elifdef";
    if (!error_on_empty_cond_stack(preprocessor, directive_name, loc))
        enter_elif(preprocessor, directive_name, is_elifndef ? COND_IS_NOT_DEFINED : COND_IS_DEFINED, loc);
}

static void parse_elifdef(struct preprocessor* preprocessor, struct file_loc* loc) {
    parse_elifdef_or_elifndef(preprocessor, false, loc);
}

static void parse_elifndef(struct preprocessor* preprocessor, struct file_loc* loc) {
    parse_elifdef_or_elifndef(preprocessor, true, loc);
}

static inline size_t find_macro_param_index(
    struct preprocessor* preprocessor,
    const struct small_str_view_vec* params,
    const struct token* token,
    bool is_variadic)
{
    if (str_view_is_equal(&token->contents, &STR_VIEW("__VA_ARGS__"))) {
        if (!is_variadic) {
            log_warn(preprocessor->log, &token->loc, "'__VA_ARGS__' is only allowed inside variadic macros");
            return SIZE_MAX;
        }
        return params->elem_count;
    }

    for (size_t i = 0; i < params->elem_count; ++i) {
        if (str_view_is_equal(&params->elems[i], &token->contents))
            return i;
    }
    return SIZE_MAX;
}

static bool verify_macro(struct preprocessor* preprocessor, const struct macro* macro) {
    if (!strcmp(macro->name, "defined")) {
        log_error(preprocessor->log, &macro->loc, "'defined' cannot be used as a macro name");
        return false;
    }
    for (size_t i = 0; i < macro->tokens.elem_count; ++i) {
        switch (macro->tokens.elems[i].tag) {
            case TOKEN_HASH:
                if (i + 1 == macro->tokens.elem_count || macro->tokens.elems[i + 1].tag != TOKEN_MACRO_PARAM) {
                    log_error(preprocessor->log, &macro->loc, "stringification operator '#' must be followed by a macro parameter");
                    return false;
                }
                break;
            case TOKEN_CONCAT:
                if (i == 0 || i + 1 == macro->tokens.elem_count || macro->tokens.elems[i + 1].tag == TOKEN_CONCAT) {
                    log_error(preprocessor->log, &macro->loc, "invalid use of concatenation operator '##'");
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}

static void parse_define(struct preprocessor* preprocessor, struct file_loc* loc) {
    const char* name = parse_ident(preprocessor);

    bool has_params = false;
    bool is_variadic = false;
    struct small_str_view_vec params;
    small_str_view_vec_init(&params);
    if (accept_token(preprocessor, TOKEN_LPAREN)) {
        has_params = true;
        while (peek_token(preprocessor).tag == TOKEN_IDENT) {
            struct token token = read_token(preprocessor);
            small_str_view_vec_push(&params, &token.contents);
            assert(token.tag == TOKEN_IDENT);
            if (!accept_token(preprocessor, TOKEN_COMMA))
                break;
        }
        is_variadic |= accept_token(preprocessor, TOKEN_ELLIPSIS);
        expect_token(preprocessor, TOKEN_RPAREN);
    }

    struct macro macro = {
        .tokens = token_vec_create(),
        .name = name,
        .has_params = has_params,
        .is_variadic = is_variadic,
        .param_count = params.elem_count,
        .loc = *loc
    };

    while (true) {
        struct token token = read_token(preprocessor);
        if (token.tag == TOKEN_NL || token.tag == TOKEN_EOF)
            break;

        macro.loc.end = token.loc.end;
        if (token.tag == TOKEN_IDENT) {
            const size_t macro_param_index = find_macro_param_index(preprocessor, &params, &token, is_variadic);
            if (macro_param_index != SIZE_MAX) {
                token.tag = TOKEN_MACRO_PARAM;
                token.macro_param_index = macro_param_index;
            }
        }
        token_vec_push(&macro.tokens, &token);
    }
    small_str_view_vec_destroy(&params);

    if (!verify_macro(preprocessor, &macro)) {
        cleanup_macro(&macro);
        return;
    }

    insert_macro(preprocessor, &macro);
}

static void parse_undef(struct preprocessor* preprocessor) {
    const struct file_loc loc = peek_token(preprocessor).loc;
    const char* name = parse_ident(preprocessor);
    struct macro* macro = find_macro(preprocessor, name);
    if (!macro) {
        log_error(preprocessor->log, &loc, "unknown macro '%s'", name);
    } else {
        macro_set_remove(&preprocessor->macros, &macro);
        free_macro(macro);
    }
    eat_extra_tokens(preprocessor, "undef");
}

static struct str_view extract_line(const char* str) {
    struct str_view str_view = { .data = str };
    while (*str && *str != '\n')
        str++, str_view.length++;
    return str_view;
}

static void parse_warning_or_error(struct preprocessor* preprocessor, bool is_error) {
    // Extract the warning/error message, by reading the source file data that has not been lexed yet.
    struct str_view msg = extract_line(peek_token(preprocessor).contents.data);
    struct file_loc loc = eat_extra_tokens(preprocessor, NULL);
    log_msg(is_error ? MSG_ERROR : MSG_WARN, preprocessor->log, &loc, "%.*s", (int)msg.length, msg.data);
}

static void parse_warning(struct preprocessor* preprocessor) {
    parse_warning_or_error(preprocessor, false);
}

static void parse_error(struct preprocessor* preprocessor) {
    parse_warning_or_error(preprocessor, true);
}

static inline void ignore_directive(struct preprocessor* preprocessor, const char* directive_name, struct file_loc* loc) {
    eat_extra_tokens(preprocessor, NULL);
    log_warn(preprocessor->log, loc, "ignoring '#%s'", directive_name);
}

static void parse_line(struct preprocessor* preprocessor, struct file_loc* loc) {
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
    struct source_file* source_file = &preprocessor->context->source_file;

    struct token line_token = expand_token(preprocessor);
    if (line_token.tag != TOKEN_INT_LITERAL) {
        log_error(preprocessor->log, loc, "missing or invalid line number in '#line' directive");
        if (line_token.tag != TOKEN_NL)
            eat_extra_tokens(preprocessor, "line");
        return;
    }

    struct token file_name_token = expand_token(preprocessor);
    if (file_name_token.tag == TOKEN_STRING_LITERAL) {
        source_file->displayed_file_name = str_pool_insert_view(preprocessor->str_pool, file_name_token.string_literal);
        eat_extra_tokens(preprocessor, "line");
    } else if (file_name_token.tag != TOKEN_NL) {
        log_error(preprocessor->log, &file_name_token.loc, "invalid file name '%.*s' in '#line' directive",
            (int)file_name_token.contents.length, file_name_token.contents.data);
        eat_extra_tokens(preprocessor, "line");
        return;
    }

    uint32_t displayed_line = line_token.int_literal;
    source_file->displayed_line = ++displayed_line;

    // Update the already extracted tokens so that they also have the updated line info.
    for (int i = 0; i < TOKENS_AHEAD; ++i) {
        struct token* token = &preprocessor->context->ahead[i];
        token->loc.displayed_file_name = source_file->displayed_file_name;
        token->loc.displayed_line = displayed_line;
        if (token->tag == TOKEN_NL)
            displayed_line++;
    }
}

static void parse_pragma(struct preprocessor* preprocessor, struct file_loc* loc) {
    struct token token = peek_token(preprocessor);
    if (token.tag == TOKEN_IDENT && str_view_is_equal(&token.contents, &STR_VIEW("once"))) {
        assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
        preprocessor->context->source_file.cached_file->has_pragma_once = true;
        eat_token(preprocessor, TOKEN_IDENT);
        eat_extra_tokens(preprocessor, "pragma");
    } else {
        ignore_directive(preprocessor, "pragma", loc);
    }
}

static struct str_view parse_include_file_name(struct preprocessor* preprocessor) {
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);

    struct token token = read_token(preprocessor);
    if (token.tag == TOKEN_STRING_LITERAL) {
        return token.contents;
    } else if (token.tag == TOKEN_CMP_LT) {
        struct token first_token = token;
        struct token last_token  = token;
        do {
            token = read_token(preprocessor);
            if (token.tag == TOKEN_EOF || token.tag == TOKEN_NL) {
                log_error(preprocessor->log, &token.loc, "unterminated include file name");
                return (struct str_view) {};
            }
            last_token = token;
        } while (token.tag != TOKEN_CMP_GT);

        // We know the tokens all come from the same, continuous file data, and not from macro
        // expansion, which makes it possible to just use a string view that spans all tokens.
        return (struct str_view) {
            .data = first_token.contents.data,
            .length = (last_token.contents.data + last_token.contents.length) - first_token.contents.data
        };
    } else {
        log_error(preprocessor->log, &token.loc, "expected include file name, but got '%.*s'",
            (int)token.contents.length, token.contents.data);
        return (struct str_view) {};
    }
}

static inline bool does_file_exist_in_directory(
    struct str_view directory,
    struct str_view file_name,
    struct str* full_path)
{
    str_clear(full_path);
    str_printf(full_path, "%.*s/%.*s",
        (int)directory.length, directory.data,
        (int)file_name.length, file_name.data);
    return file_exists(full_path->data);
}

static bool find_include_file_name(
    struct preprocessor* preprocessor,
    struct str_view include_file_name,
    bool is_relative_include,
    struct str* full_path)
{
    if (is_relative_include) {
        for (struct context* context = preprocessor->context; context; context = context->prev) {
            // Walk up the chain of included files, lookup files in the same directory
            if (context->tag != CONTEXT_SOURCE_FILE)
                continue;

            struct str_view include_path = split_path(STR_VIEW(context->source_file.cached_file->file_name)).dir_name;
            if (does_file_exist_in_directory(include_path, include_file_name, full_path))
                return true;
        }
    }
    for (size_t i = 0; preprocessor->include_paths[i]; ++i) {
        struct str_view include_path = STR_VIEW(preprocessor->include_paths[i]);
        if (does_file_exist_in_directory(include_path, include_file_name, full_path))
            return true;
    }
    return false;
}

static struct cached_file* find_include_file(
    struct preprocessor* preprocessor,
    struct str_view include_file_name,
    bool is_relative_include)
{
    struct str full_path = str_create();
    struct cached_file* cached_file = NULL;
    if (find_include_file_name(preprocessor, include_file_name, is_relative_include, &full_path))
        cached_file = file_cache_read(preprocessor->file_cache, full_path.data);
    str_destroy(&full_path);
    return cached_file;
}

static bool skip_include_file_delimiters(struct str_view* include_file_name) {
    assert(include_file_name->length >= 2);

    bool is_relative_include = include_file_name->data[0] == '"';
    assert(include_file_name->data[include_file_name->length - 1] == (is_relative_include ? '"' : '>'));

    include_file_name->data += 1;
    include_file_name->length -= 2;
    return is_relative_include;
}

static void parse_include(struct preprocessor* preprocessor, struct file_loc* loc) {
    struct str_view include_file_name = parse_include_file_name(preprocessor);

    struct cached_file* cached_file = NULL;
    if (include_file_name.length > 0) {
        bool is_relative_include = skip_include_file_delimiters(&include_file_name);
        cached_file = find_include_file(preprocessor, include_file_name, is_relative_include);
        if (!cached_file) {
            log_error(preprocessor->log, loc, "cannot find include file '%.*s'",
                (int)include_file_name.length, include_file_name.data);
        }
    }

    eat_extra_tokens(preprocessor, "include");

    if (cached_file && !cached_file->has_pragma_once)
        push_context(preprocessor, alloc_source_file_context(cached_file, preprocessor->context));
}

static inline bool is_control_directive(enum directive directive) {
    switch (directive) {
        case DIRECTIVE_IF:
        case DIRECTIVE_IFDEF:
        case DIRECTIVE_IFNDEF:
        case DIRECTIVE_ELSE:
        case DIRECTIVE_ELIF:
        case DIRECTIVE_ELIFDEF:
        case DIRECTIVE_ELIFNDEF:
        case DIRECTIVE_ENDIF:
            return true;
        default:
            return false;
    }
}

static void parse_directive(struct preprocessor* preprocessor) {
    struct token token = read_token(preprocessor);
    enum directive directive = directive_from_string(token.contents);

    if (!preprocessor->context->is_active && !is_control_directive(directive)) {
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

    if (directive == DIRECTIVE_NONE) {
        log_error(preprocessor->log, &token.loc, "invalid preprocessor directive '%.*s'",
            (int)token.contents.length, token.contents.data);
        return;
    }

    switch (directive) {
        case DIRECTIVE_IF:       parse_if(preprocessor, &token.loc);        break;
        case DIRECTIVE_ELSE:     parse_else(preprocessor, &token.loc);      break;
        case DIRECTIVE_ELIF:     parse_elif(preprocessor, &token.loc);      break;
        case DIRECTIVE_ENDIF:    parse_endif(preprocessor, &token.loc);     break;
        case DIRECTIVE_IFDEF:    parse_ifdef(preprocessor, &token.loc);     break;
        case DIRECTIVE_IFNDEF:   parse_ifndef(preprocessor, &token.loc);    break;
        case DIRECTIVE_ELIFDEF:  parse_elifdef(preprocessor, &token.loc);   break;
        case DIRECTIVE_ELIFNDEF: parse_elifndef(preprocessor, &token.loc);  break;
        case DIRECTIVE_DEFINE:   parse_define(preprocessor, &token.loc);    break;
        case DIRECTIVE_UNDEF:    parse_undef(preprocessor);                 break;
        case DIRECTIVE_WARNING:  parse_warning(preprocessor);               break;
        case DIRECTIVE_ERROR:    parse_error(preprocessor);                 break;
        case DIRECTIVE_LINE:     parse_line(preprocessor, &token.loc);      break;
        case DIRECTIVE_PRAGMA:   parse_pragma(preprocessor, &token.loc);    break;
        case DIRECTIVE_INCLUDE:  parse_include(preprocessor, &token.loc);   break;
        default:
            assert(false && "invalid preprocessor directive");
            break;
    }
}

static void print_token_error(struct log* log, const struct token* token) {
    assert(token->tag == TOKEN_ERROR);
    switch (token->error) {
        case TOKEN_ERROR_INVALID:
            log_error(log, &token->loc, "invalid token '%.*s'", (int)token->contents.length, token->contents.data);
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

struct token preprocessor_advance(struct preprocessor* preprocessor) {
    while (true) {
        struct token token = expand_token(preprocessor);
        if (token.tag == TOKEN_HASH && token.on_new_line && preprocessor->context->tag == CONTEXT_SOURCE_FILE) {
            parse_directive(preprocessor);
            continue;
        } else if (token.tag == TOKEN_EOF) {
            if (preprocessor->context->prev) {
                pop_context(preprocessor);
                continue;
            }
        } else if (token.tag == TOKEN_NL || !preprocessor->context->is_active) {
            continue;
        } else if (token.tag == TOKEN_ERROR) {
            print_token_error(preprocessor->log, &token);
            continue;
        }

        return token;
    }
}

void preprocessor_register_macro(struct preprocessor* preprocessor, const char* name, const char* expansion) {
    // Internalize strings so that their lifetime is tied to the preprocessor.
    name = str_pool_insert(preprocessor->str_pool, name);
    expansion = str_pool_insert(preprocessor->str_pool, expansion);

    struct macro macro = {
        .tokens = token_vec_create(),
        .name = name,
        .has_params = false,
        .is_variadic = false,
        .param_count = 0,
        .loc = { .file_name = "<builtin macro>" }
    };

    struct lexer lexer = lexer_create(name, STR_VIEW(expansion));
    while (true) {
        struct token token = lexer_advance(&lexer);
        if (token.tag == TOKEN_EOF)
            break;
        token_vec_push(&macro.tokens, &token);
    }

    insert_macro(preprocessor, &macro);
}
