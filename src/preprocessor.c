#include "preprocessor.h"
#include "file_cache.h"
#include "lexer.h"

#include <overture/hash.h>
#include <overture/mem.h>
#include <overture/str_pool.h>
#include <overture/mem_pool.h>
#include <overture/set.h>

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

struct macro {
    bool has_params;
    bool is_variadic;
    size_t param_count;
    const char* name;
    struct token_vec tokens;
    struct file_loc loc;
};

enum directive {
    DIRECTIVE_NONE,
#define x(name, ...) DIRECTIVE_##name,
    DIRECTIVE_LIST(x)
#undef x
};

VEC_DEFINE(cond_stack, struct cond, PRIVATE)
SMALL_VEC_DEFINE(small_str_view_vec, struct str_view, PRIVATE)
SMALL_VEC_DEFINE(small_index_vec, size_t, PRIVATE)

enum context_tag {
    CONTEXT_SOURCE_FILE,
    CONTEXT_MACRO
};

struct context {
    enum context_tag tag;
    bool is_active;
    bool on_new_line;

    struct file_loc line_loc;
    struct context* prev;
    size_t token_index;

    union {
        struct {
            struct source_file* source_file;
            struct cond_stack cond_stack;
            size_t inactive_cond_depth;
        } source_file_context;
        struct {
            const struct macro* macro;
            struct token_vec tokens;
        } macro_context;
    };
};

static inline uint32_t hash_macro_name(uint32_t h, struct macro* const* macro) {
    return hash_uint64(h, (uint64_t)(*macro)->name);
}

static inline bool is_macro_name_equal(struct macro* const* macro, struct macro* const* other_macro) {
    return (*macro)->name == (*other_macro)->name;
}

SET_DEFINE(macro_set, struct macro*, hash_macro_name, is_macro_name_equal, PRIVATE)

struct preprocessor {
    struct log* log;
    struct context* context;
    struct file_cache* file_cache;
    struct macro_set macros;
    struct preprocessor_config config;
    struct str_pool* str_pool;
    struct mem_pool mem_pool;
};

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

static inline struct context* alloc_context(struct context* prev) {
    struct context* context = xcalloc(1, sizeof(struct context));
    context->prev = prev;
    context->on_new_line = true;
    context->is_active = true;
    return context;
}

static inline struct context* alloc_source_file_context(struct context* prev, struct source_file* source_file) {
    struct context* context = alloc_context(prev);
    context->tag = CONTEXT_SOURCE_FILE;
    context->source_file_context.source_file = source_file;
    context->source_file_context.cond_stack = cond_stack_create();
    return context;
}

static inline struct context* alloc_macro_context(struct context* prev, const struct macro* macro) {
    struct context* context = alloc_context(prev);
    context->tag = CONTEXT_MACRO;
    context->macro_context.macro = macro;
    context->macro_context.tokens = token_vec_create();
    return context;
}

static inline void free_context(struct context* context) {
    if (context->tag == CONTEXT_SOURCE_FILE) {
        cond_stack_destroy(&context->source_file_context.cond_stack);
    } else if (context->tag == CONTEXT_MACRO) {
        token_vec_destroy(&context->macro_context.tokens);
    }
    free(context);
}

static inline void push_context(struct preprocessor* preprocessor, struct context* context) {
    context->prev = preprocessor->context;
    preprocessor->context = context;
}

static inline bool is_inside_cond(const struct context* context) {
    return context->tag == CONTEXT_SOURCE_FILE &&
        (!cond_stack_is_empty(&context->source_file_context.cond_stack) ||
         context->source_file_context.inactive_cond_depth > 0);
}

static inline struct context* pop_context(struct preprocessor* preprocessor) {
    assert(preprocessor->context);
    if (is_inside_cond(preprocessor->context)) {
        struct cond* last_cond = cond_stack_last(&preprocessor->context->source_file_context.cond_stack);
        log_error(preprocessor->log, &last_cond->loc, "unterminated '#if'");
    }
    struct context* prev_context = preprocessor->context->prev;
    free_context(preprocessor->context);
    return preprocessor->context = prev_context;
}

static inline const struct token_vec* context_tokens(const struct context* context) {
    return context->tag == CONTEXT_SOURCE_FILE
        ? &context->source_file_context.source_file->tokens
        : &context->macro_context.tokens;
}

static inline size_t context_size(const struct context* context) {
    return context_tokens(context)->elem_count;
}

static inline struct token peek_token(const struct context* context) {
    return context_tokens(context)->elems[context->token_index];
}

static inline struct token last_token(const struct context* context) {
    return context_tokens(context)->elems[context_size(context) - 1];
}

static inline struct token read_token(struct preprocessor* preprocessor) {
    struct context* context = preprocessor->context;
    assert(context);

    while (context->token_index >= context_size(context)) {
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
    context->token_index++;
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

static inline struct context* expand_macro(
    struct preprocessor* preprocessor,
    const struct macro* macro,
    const struct file_loc* loc)
{
    struct context* context = alloc_macro_context(preprocessor->context, macro);

    struct small_token_vec args;
    struct small_index_vec arg_indices;
    small_token_vec_init(&args);
    small_index_vec_init(&arg_indices);

    if (macro->has_params) {
        eat_token(preprocessor, TOKEN_LPAREN);
        size_t paren_depth = 0;

        small_index_vec_push(&arg_indices, &args.elem_count);
        while (true) {
            struct token token = read_token(preprocessor);
            if (token.tag == TOKEN_EOF) {
                log_error(preprocessor->log, &token.loc, "unterminated argument list for macro '%s'", macro->name);
                break;
            } else if (token.tag == TOKEN_RPAREN) {
                if (paren_depth == 0)
                    break;
                paren_depth--;
            } else if (token.tag == TOKEN_LPAREN) {
                paren_depth++;
            } else if (token.tag == TOKEN_COMMA && paren_depth == 0 && arg_indices.elem_count <= macro->param_count) {
                // Everything after the regular macro arguments belongs to __VA_ARG__, including commas.
                small_index_vec_push(&arg_indices, &args.elem_count);
                continue;
            }

            small_token_vec_push(&args, &token);
        }
    }

    // Place sentinel to simplify the code below.
    const size_t arg_count = arg_indices.elem_count;
    small_index_vec_push(&arg_indices, &args.elem_count);

    if (macro->param_count > arg_count || (!macro->is_variadic && macro->param_count != arg_count)) {
        log_error(preprocessor->log, loc, "expected %zu argument(s) to macro '%s', but got %zu",
            macro->param_count, macro->name, arg_count);
    }

    for (size_t i = 0; i < macro->tokens.elem_count; ++i) {
        struct token token = macro->tokens.elems[i];
        if (token.tag == TOKEN_MACRO_PARAM) {
            // May happen if the error above triggered, or in the absence of an argument for
            // '__VA_ARGS__', in which case we just skip this parameter.
            if (token.macro_param_index >= arg_count)
                continue;

            const size_t begin = arg_indices.elems[token.macro_param_index];
            const size_t end   = arg_indices.elems[token.macro_param_index + 1];
            for (size_t j = begin; j < end; ++j)
                token_vec_push(&context->macro_context.tokens, &args.elems[j]);
        } else {
            token_vec_push(&context->macro_context.tokens, &macro->tokens.elems[i]);
        }
    }
    small_token_vec_destroy(&args);
    small_index_vec_destroy(&arg_indices);
    return context;
}

static inline bool can_expand_macro(struct preprocessor* preprocessor, const struct macro* macro) {
    // Make sure we do not allow recursive macro expansion
    struct context* context = preprocessor->context;
    while (context && context->tag == CONTEXT_MACRO) {
        if (context->macro_context.macro == macro)
            return false;
        context = context->prev;
    }
    return true;
}

static inline struct token expand_token(struct preprocessor* preprocessor) {
    while (true) {
        struct token token = read_token(preprocessor);
        if (token.tag != TOKEN_IDENT)
            return token;

        const char* ident = str_pool_insert_view(preprocessor->str_pool, preprocessor_view(preprocessor, &token));
        const struct macro* macro = find_macro(preprocessor, ident);
        if (!macro || !can_expand_macro(preprocessor, macro))
            return token;

        const bool has_args = peek_token(preprocessor->context).tag == TOKEN_LPAREN;
        if (macro->has_params && !has_args)
            return token;

        push_context(preprocessor, expand_macro(preprocessor, macro, &token.loc));
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
        log_error(preprocessor->log, &loc, "extra tokens after '#%s'", directive_name);
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
    preprocessor->macros = macro_set_create();
    preprocessor->mem_pool = mem_pool_create();
    preprocessor->str_pool = str_pool_create(&preprocessor->mem_pool);

    push_context(preprocessor, alloc_source_file_context(NULL, source_file));
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

static inline bool eval_cond(struct preprocessor* preprocessor, enum cond_value cond_value) {
    switch (cond_value) {
        case COND_PARSE:
            return parse_condition(preprocessor) != 0;
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

static void enter_if(struct preprocessor* preprocessor, const char* directive_name, enum cond_value cond_value) {
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
    if (!preprocessor->context->is_active) {
        preprocessor->context->source_file_context.inactive_cond_depth++;
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

    const bool is_active = eval_cond(preprocessor, cond_value);
    cond_stack_push(&preprocessor->context->source_file_context.cond_stack, &(struct cond) {
        .was_active = is_active,
        .loc = preprocessor->context->line_loc
    });
    preprocessor->context->is_active &= is_active;
    eat_extra_tokens(preprocessor, directive_name);
}

static inline void enter_elif(struct preprocessor* preprocessor, const char* directive_name, enum cond_value cond_value) {
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
    if (preprocessor->context->source_file_context.inactive_cond_depth > 0) {
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

    assert(!cond_stack_is_empty(&preprocessor->context->source_file_context.cond_stack));
    struct cond* last_cond = cond_stack_last(&preprocessor->context->source_file_context.cond_stack);
    if (last_cond->was_last_else)
        log_error(preprocessor->log, &preprocessor->context->line_loc, "'#%s' after '#else'", directive_name);

    const bool is_active = eval_cond(preprocessor, cond_value) & !last_cond->was_active;
    last_cond->was_active |= is_active;
    last_cond->was_last_else = cond_value == COND_TRUE;
    preprocessor->context->is_active = is_active;
    eat_extra_tokens(preprocessor, directive_name);
}

static inline bool error_on_empty_cond_stack(struct preprocessor* preprocessor, const char* directive_name) {
    if (!is_inside_cond(preprocessor->context)) {
        log_error(preprocessor->log, &preprocessor->context->line_loc, "'#%s' without '#if'", directive_name);
        return true;
    }
    return false;
}

static void parse_if(struct preprocessor* preprocessor) {
    enter_if(preprocessor, "if", COND_PARSE);
}

static void parse_else(struct preprocessor* preprocessor) {
    if (!error_on_empty_cond_stack(preprocessor, "else"))
        enter_elif(preprocessor, "else", COND_TRUE);
}

static void parse_elif(struct preprocessor* preprocessor) {
    if (!error_on_empty_cond_stack(preprocessor, "elif"))
        enter_elif(preprocessor, "elif", COND_PARSE);
}

static void parse_endif(struct preprocessor* preprocessor) {
    assert(preprocessor->context->tag == CONTEXT_SOURCE_FILE);
    if (preprocessor->context->source_file_context.inactive_cond_depth > 0) {
        preprocessor->context->source_file_context.inactive_cond_depth--;
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

    if (!error_on_empty_cond_stack(preprocessor, "endif")) {
        cond_stack_pop(&preprocessor->context->source_file_context.cond_stack);
    }
    preprocessor->context->is_active = true;
    eat_extra_tokens(preprocessor, "endif");
}

static inline void parse_ifdef_or_ifndef(struct preprocessor* preprocessor, bool is_ifndef) {
    const char* directive_name = is_ifndef ? "ifndef" : "ifdef";
    enter_if(preprocessor, directive_name, is_ifndef ? COND_IS_NOT_DEFINED : COND_IS_DEFINED);
}

static void parse_ifdef(struct preprocessor* preprocessor) {
    parse_ifdef_or_ifndef(preprocessor, false);
}

static void parse_ifndef(struct preprocessor* preprocessor) {
    parse_ifdef_or_ifndef(preprocessor, true);
}

static void parse_elifdef_or_elifndef(struct preprocessor* preprocessor, bool is_elifndef) {
    const char* directive_name = is_elifndef ? "elifndef" : "elifdef";
    if (!error_on_empty_cond_stack(preprocessor, "elif"))
        enter_elif(preprocessor, directive_name, is_elifndef ? COND_IS_NOT_DEFINED : COND_IS_DEFINED);
}

static void parse_elifdef(struct preprocessor* preprocessor) {
    parse_elifdef_or_elifndef(preprocessor, false);
}

static void parse_elifndef(struct preprocessor* preprocessor) {
    parse_elifdef_or_elifndef(preprocessor, true);
}

static inline size_t find_macro_param_index(
    struct preprocessor* preprocessor,
    const struct small_str_view_vec* params,
    const struct token* token,
    bool is_variadic)
{
    struct str_view ident = preprocessor_view(preprocessor, token);
    if (str_view_is_equal(&ident, &STR_VIEW("__VA_ARGS__"))) {
        if (!is_variadic) {
            log_warn(preprocessor->log, &token->loc, "'__VA_ARGS__' is only allowed inside variadic macros");
            return SIZE_MAX;
        }
        return params->elem_count;
    }

    for (size_t i = 0; i < params->elem_count; ++i) {
        if (str_view_is_equal(&params->elems[i], &ident))
            return i;
    }
    return SIZE_MAX;
}

static void parse_define(struct preprocessor* preprocessor) {
    const char* name = parse_ident(preprocessor);

    bool has_params = false;
    bool is_variadic = false;
    struct small_str_view_vec params;
    small_str_view_vec_init(&params);
    if (accept_token(preprocessor, TOKEN_LPAREN)) {
        has_params = true;
        while (peek_token(preprocessor->context).tag == TOKEN_IDENT) {
            struct token token = read_token(preprocessor);
            struct str_view param = preprocessor_view(preprocessor, &token);
            small_str_view_vec_push(&params, &param);
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
        .param_count = params.elem_count
    };

    struct token token;
    do {
        token = read_token(preprocessor);
        if (token.tag == TOKEN_IDENT) {
            const size_t macro_param_index = find_macro_param_index(preprocessor, &params, &token, is_variadic);
            if (macro_param_index != SIZE_MAX) {
                token.tag = TOKEN_MACRO_PARAM;
                token.macro_param_index = macro_param_index;
            }
        }
        token_vec_push(&macro.tokens, &token);
    } while (token.tag != TOKEN_NL);
    small_str_view_vec_destroy(&params);

    insert_macro(preprocessor, &macro);
}

static void parse_undef(struct preprocessor* preprocessor) {
    const struct file_loc loc = peek_token(preprocessor->context).loc;
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
    log_warn(preprocessor->log, &preprocessor->context->line_loc, "ignoring '#%s'", directive_name);
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

static void parse_directive(struct preprocessor* preprocessor, enum directive directive) {
    if (!preprocessor->context->is_active && !is_control_directive(directive)) {
        eat_extra_tokens(preprocessor, NULL);
        return;
    }

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

static void print_token_error(struct log* log, const char* file_data, const struct token* token) {
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

struct token preprocessor_advance(struct preprocessor* preprocessor) {
    while (true) {
        bool on_new_line = preprocessor->context->on_new_line;
        struct token token = expand_token(preprocessor);
        if (token.tag == TOKEN_HASH && on_new_line && preprocessor->context->tag == CONTEXT_SOURCE_FILE) {
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
            print_token_error(preprocessor->log, file_data, &token);
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
