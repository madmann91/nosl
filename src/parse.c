#include "ast.h"
#include "parse.h"
#include "lexer.h"
#include "preprocessor.h"

#include <overture/log.h>
#include <overture/str.h>
#include <overture/mem_pool.h>
#include <overture/mem.h>

#include <assert.h>
#include <string.h>

#define TOKENS_AHEAD 3
#define TOKENS_BEHIND 3

struct parse_input {
    void* data;
    struct token (*next_token)(void* data);
};

struct parser {
    struct token ahead[TOKENS_AHEAD];
    struct token behind[TOKENS_BEHIND];
    struct parse_input input;
    struct mem_pool* mem_pool;
    struct log* log;
};

static struct ast* parse_type(struct parser*);
static struct ast* parse_expr(struct parser*);
static struct ast* parse_stmt(struct parser*);

static inline void read_token(struct parser* parser) {
    for (size_t i = 1; i < TOKENS_BEHIND; ++i)
        parser->behind[i] = parser->behind[i - 1];
    parser->behind[0] = parser->ahead[0];
    for (size_t i = 1; i < TOKENS_AHEAD; ++i)
        parser->ahead[i - 1] = parser->ahead[i];
    parser->ahead[TOKENS_AHEAD - 1] = parser->input.next_token(parser->input.data);
}

static inline void eat_token(struct parser* parser, [[maybe_unused]] enum token_tag tag) {
    assert(parser->ahead[0].tag == tag);
    read_token(parser);
}

static inline bool accept_token(struct parser* parser, enum token_tag tag) {
    if (parser->ahead->tag == tag) {
        read_token(parser);
        return true;
    }
    return false;
}

static inline bool expect_token(struct parser* parser, enum token_tag tag) {
    if (!accept_token(parser, tag)) {
        log_error(parser->log,
            &parser->ahead->loc,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)parser->ahead->contents.length, parser->ahead->contents.data);
        return false;
    }
    return true;
}

static inline struct ast* alloc_ast(
    struct parser* parser,
    const struct file_loc* begin_loc,
    const struct ast* ast)
{
    struct ast* copy = MEM_POOL_ALLOC(*parser->mem_pool, struct ast);
    memcpy(copy, ast, sizeof(struct ast));

    struct file_loc* end_loc = &parser->behind->loc;
    const bool is_after = end_loc->end.row > begin_loc->begin.row ||
        (end_loc->end.row == begin_loc->begin.row && end_loc->end.col >= begin_loc->begin.col);
    const bool is_same_file = str_view_is_equal(&end_loc->file_name, &begin_loc->file_name);

    copy->loc.file_name = begin_loc->file_name;
    copy->loc.begin = begin_loc->begin;
    if (is_after && is_same_file) {
        copy->loc.end = end_loc->end;
    } else {
        // Make sure the location shows up as one entire line from the first token, as this is the
        // best we can do to assign a single location to an AST that spans multiple files or comes
        // from macro expansion in various places.
        copy->loc.end.row = begin_loc->end.row + 1;
        copy->loc.end.col = 1;
    }
    return copy;
}

static const char* parse_ident(struct parser* parser) {
    struct str_view contents  = parser->ahead->contents;
    char* name = mem_pool_alloc(parser->mem_pool, contents.length + 1, alignof(char));
    xmemcpy(name, contents.data, contents.length);
    name[contents.length] = 0;
    expect_token(parser, TOKEN_IDENT);
    return name;
}

static struct ast* parse_many(
    struct parser* parser,
    enum token_tag stop,
    enum token_tag sep,
    struct ast* (parse_func)(struct parser*))
{
    struct ast* first = NULL;
    struct ast** prev = &first;
    if (parser->ahead->tag != stop) {
        while (sep != TOKEN_ERROR || parser->ahead->tag != stop) {
            *prev = parse_func(parser);
            prev = &(*prev)->next;
            if (sep != TOKEN_ERROR && !accept_token(parser, sep))
                break;
        }
    }
    expect_token(parser, stop);
    return first;
}

static struct ast* parse_error(struct parser* parser, const char* msg) {
    struct file_loc begin_loc = parser->ahead->loc;
    log_error(parser->log,
        &parser->ahead->loc,
        "expected %s, but got '%.*s'",
        msg, (int)parser->ahead->contents.length, parser->ahead->contents.data);
    read_token(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) { .tag = AST_ERROR });
}

static struct ast* parse_attr(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    const char* name = parse_ident(parser);
    struct ast* args = NULL;
    if (accept_token(parser, TOKEN_LPAREN))
        args = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_expr);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_ATTR,
        .attr = {
            .name = name,
            .args = args
        }
    });
}

static struct ast* parse_attr_list(struct parser* parser) {
    if (!accept_token(parser, TOKEN_ATTRIBUTE))
        return NULL;
    expect_token(parser, TOKEN_LPAREN);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* attrs = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_attr);
    expect_token(parser, TOKEN_RPAREN);
    return attrs;
}

static struct ast* parse_bool_literal(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    bool bool_literal = parser->ahead->tag == TOKEN_TRUE;
    eat_token(parser, bool_literal ? TOKEN_TRUE : TOKEN_FALSE);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_BOOL_LITERAL,
        .bool_literal = bool_literal
    });
}

static struct ast* parse_int_literal(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    int_literal int_literal = parser->ahead->int_literal;
    eat_token(parser, TOKEN_INT_LITERAL);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_INT_LITERAL,
        .int_literal = int_literal
    });
}

static struct ast* parse_float_literal(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    float_literal float_literal = parser->ahead->float_literal;
    eat_token(parser, TOKEN_FLOAT_LITERAL);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_FLOAT_LITERAL,
        .float_literal = float_literal
    });
}

static struct ast* parse_string_literal(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    struct str str = str_create();
    while (parser->ahead->tag == TOKEN_STRING_LITERAL) {
        str_append(&str, parser->ahead->string_literal);
        eat_token(parser, TOKEN_STRING_LITERAL);
    }
    char* string_literal = mem_pool_alloc(parser->mem_pool, str.length + 1, alignof(char));
    xmemcpy(string_literal, str.data, str.length);
    string_literal[str.length] = 0;
    str_destroy(&str);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_STRING_LITERAL,
        .string_literal = string_literal
    });
}

static struct ast* parse_compound_init(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_LBRACE);
    struct ast* elems = parse_many(parser, TOKEN_RBRACE, TOKEN_COMMA, parse_expr);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_COMPOUND_INIT,
        .compound_expr.elems = elems
    });
}

static struct ast* parse_cast_expr(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_LPAREN);
    struct ast* type = parse_type(parser);
    expect_token(parser, TOKEN_RPAREN);
    struct ast* value = parse_expr(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_CAST_EXPR,
        .cast_expr = {
            .type = type,
            .value = value
        }
    });
}

static struct ast* parse_compound_expr(struct parser* parser) {
    struct ast* first = parse_expr(parser);
    if (parser->ahead->tag != TOKEN_COMMA)
        return first;

    struct ast* prev = first;
    while (accept_token(parser, TOKEN_COMMA)) {
        prev->next = parse_expr(parser);
        prev = prev->next;
    }
    return alloc_ast(parser, &first->loc, &(struct ast) {
        .tag = AST_COMPOUND_EXPR,
        .compound_expr.elems = first
    });
}

static struct ast* parse_paren_expr(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_LPAREN);
    struct ast* inner_expr = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_PAREN_EXPR,
        .paren_expr.inner_expr = inner_expr
    });
}

static struct ast* parse_cast_or_paren_expr(struct parser* parser) {
    switch (parser->ahead[1].tag) {
#define x(name, ...) case TOKEN_##name:
        PRIM_TYPE_LIST(x)
#undef x
            if (parser->ahead[2].tag == TOKEN_RPAREN)
                return parse_cast_expr(parser);
            [[fallthrough]];
        default:
            return parse_paren_expr(parser);
    }
}

static struct ast* parse_ident_expr(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    const char* name = parse_ident(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_IDENT_EXPR,
        .ident_expr.name = name
    });
}

static struct ast* parse_construct_expr(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    struct ast* type = parse_type(parser);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* args = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_expr);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_CONSTRUCT_EXPR,
        .construct_expr = {
            .type = type,
            .args = args
        }
    });
}

static struct ast* parse_primary_expr(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOKEN_FALSE:
        case TOKEN_TRUE:           return parse_bool_literal(parser);
        case TOKEN_INT_LITERAL:    return parse_int_literal(parser);
        case TOKEN_FLOAT_LITERAL:  return parse_float_literal(parser);
        case TOKEN_STRING_LITERAL: return parse_string_literal(parser);
        case TOKEN_IDENT:          return parse_ident_expr(parser);
        case TOKEN_LBRACE:         return parse_compound_init(parser);
        case TOKEN_LPAREN:         return parse_cast_or_paren_expr(parser);
#define x(name, ...) case TOKEN_##name:
        PRIM_TYPE_LIST(x)
#undef x
            return parse_construct_expr(parser);
        default:
            return parse_error(parser, "expression");
    }
}

static struct ast* parse_pre_unary_expr(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    enum unary_expr_tag tag = UNARY_EXPR_NOT;
    switch (parser->ahead->tag) {
        case TOKEN_NOT: tag = UNARY_EXPR_NOT;     break;
        case TOKEN_SUB: tag = UNARY_EXPR_NEG;     break;
        case TOKEN_INC: tag = UNARY_EXPR_PRE_INC; break;
        case TOKEN_DEC: tag = UNARY_EXPR_PRE_DEC; break;
        default:
            assert(false && "invalid prefix unary operation");
            break;
    }
    read_token(parser);
    struct ast* arg = parse_expr(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_UNARY_EXPR,
        .unary_expr = {
            .tag = tag,
            .arg = arg
        }
    });
}

static struct ast* parse_prefix_expr(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOKEN_NOT:
        case TOKEN_SUB:
        case TOKEN_INC:
        case TOKEN_DEC:
        case TOKEN_ADD:
            return parse_pre_unary_expr(parser);
        default:
            return parse_primary_expr(parser);
    }
}

static struct ast* parse_proj_expr(struct parser* parser, struct ast* value) {
    eat_token(parser, TOKEN_DOT);
    const char* elem = parse_ident(parser);
    return alloc_ast(parser, &value->loc, &(struct ast) {
        .tag = AST_PROJ_EXPR,
        .proj_expr = {
            .value = value,
            .elem = elem,
        }
    });
}

static struct ast* parse_index_expr(struct parser* parser, struct ast* value) {
    eat_token(parser, TOKEN_LBRACKET);
    struct ast* index = parse_expr(parser);
    expect_token(parser, TOKEN_RBRACKET);
    return alloc_ast(parser, &value->loc, &(struct ast) {
        .tag = AST_INDEX_EXPR,
        .index_expr = {
            .value = value,
            .index = index,
        }
    });
}

static struct ast* parse_post_inc_or_dec_expr(struct parser* parser, struct ast* arg) {
    enum unary_expr_tag tag = parser->ahead->tag == TOKEN_INC
        ? UNARY_EXPR_POST_INC : UNARY_EXPR_POST_DEC;
    read_token(parser);
    return alloc_ast(parser, &arg->loc, &(struct ast) {
        .tag = AST_UNARY_EXPR,
        .unary_expr = {
            .tag = tag,
            .arg = arg
        }
    });
}

static struct ast* parse_call_expr(struct parser* parser, struct ast* callee) {
    eat_token(parser, TOKEN_LPAREN);
    struct ast* args = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_expr);
    return alloc_ast(parser, &callee->loc, &(struct ast) {
        .tag = AST_CALL_EXPR,
        .call_expr = {
            .callee = callee,
            .args = args,
        }
    });
}

static struct ast* parse_suffix_expr(struct parser* parser) {
    struct ast* expr = parse_prefix_expr(parser);
    while (true) {
        switch (parser->ahead->tag) {
            case TOKEN_DOT:      expr = parse_proj_expr(parser, expr);            break;
            case TOKEN_LBRACKET: expr = parse_index_expr(parser, expr);           break;
            case TOKEN_INC:
            case TOKEN_DEC:      expr = parse_post_inc_or_dec_expr(parser, expr); break;
            case TOKEN_LPAREN:   expr = parse_call_expr(parser, expr);            break;
            default:             return expr;
        }
    }
}

static struct ast* parse_binary_expr(struct parser* parser, struct ast* left, int prec) {
    while (true) {
        enum binary_expr_tag tag;
        switch (parser->ahead->tag) {
#define x(name, tok, ...) case TOKEN_##tok: tag = BINARY_EXPR_##name; break;
            ARITH_EXPR_LIST(x)
            SHIFT_EXPR_LIST(x)
            CMP_EXPR_LIST(x)
            BIT_EXPR_LIST(x)
            LOGIC_EXPR_LIST(x)
#undef x
            default: return left;
        }

        int next_prec = binary_expr_tag_precedence(tag);
        if (next_prec < prec) {
            left = parse_binary_expr(parser, left, next_prec);
        } else if (next_prec > prec) {
            return left;
        } else {
            read_token(parser);
            left->next = parse_binary_expr(parser, parse_suffix_expr(parser), prec - 1);
            left = alloc_ast(parser, &left->loc, &(struct ast) {
                .tag = AST_BINARY_EXPR,
                .binary_expr = {
                    .tag = tag,
                    .args = left
                }
            });
        }
    }
    return parse_suffix_expr(parser);
}

static inline int max_precedence() {
    int max_prec = 0;
#define x(name, tok, str, prec) max_prec = max_prec < prec ? prec : max_prec;
    ARITH_EXPR_LIST(x)
    SHIFT_EXPR_LIST(x)
    CMP_EXPR_LIST(x)
    BIT_EXPR_LIST(x)
    LOGIC_EXPR_LIST(x)
#undef x
    return max_prec;
}

static struct ast* parse_ternary_expr(struct parser* parser) {
    struct ast* cond = parse_binary_expr(parser, parse_suffix_expr(parser), max_precedence());
    if (!accept_token(parser, TOKEN_QUESTION))
        return cond;

    struct ast* then_expr = parse_expr(parser);
    expect_token(parser, TOKEN_COLON);
    struct ast* else_expr = parse_expr(parser);
    return alloc_ast(parser, &cond->loc, &(struct ast) {
        .tag = AST_TERNARY_EXPR,
        .ternary_expr = {
            .cond = cond,
            .then_expr = then_expr,
            .else_expr = else_expr
        }
    });
}

static struct ast* parse_assign_expr(struct parser* parser) {
    struct ast* left = parse_ternary_expr(parser);

    enum binary_expr_tag tag;
    switch (parser->ahead->tag) {
#define x(name, tok, ...) case TOKEN_##tok: tag = BINARY_EXPR_##name; break;
        ASSIGN_EXPR_LIST(x)
#undef x
        default:
            return left;
    }

    read_token(parser);
    left->next = parse_assign_expr(parser);
    return alloc_ast(parser, &left->loc, &(struct ast) {
        .tag = AST_BINARY_EXPR,
        .binary_expr = {
            .tag = tag,
            .args = left
        }
    });
}

static struct ast* parse_expr(struct parser* parser) {
    return parse_assign_expr(parser);
}

static struct ast* parse_prim_type(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    bool is_closure = accept_token(parser, TOKEN_CLOSURE);
    enum prim_type_tag tag = PRIM_TYPE_VOID;
    switch (parser->ahead->tag) {
#define x(name, ...) case TOKEN_##name: tag = PRIM_TYPE_##name; break;
        PRIM_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid prim type tag");
            break;
    }
    read_token(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_PRIM_TYPE,
        .prim_type = {
            .is_closure = is_closure,
            .tag = tag
        }
    });
}

static struct ast* parse_shader_type(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    enum shader_type_tag tag = SHADER_TYPE_SHADER;
    switch (parser->ahead->tag) {
#define x(name, ...) case TOKEN_##name: tag = SHADER_TYPE_##name; break;
        SHADER_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid prim type tag");
            break;
    }
    read_token(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_SHADER_TYPE,
        .shader_type.tag = tag
    });
}

static struct ast* parse_named_type(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    const char* name = parse_ident(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_NAMED_TYPE,
        .named_type.name = name
    });
}

static struct ast* parse_type(struct parser* parser) {
    switch (parser->ahead->tag) {
#define x(name, ...) case TOKEN_##name:
        PRIM_TYPE_LIST(x)
#undef x
        case TOKEN_CLOSURE:
            return parse_prim_type(parser);
        case TOKEN_IDENT:
            return parse_named_type(parser);
        default:
            return parse_error(parser, "type");
    }
}

static struct ast* parse_unsized_dim(struct parser* parser, const struct file_loc* begin_loc) {
    return alloc_ast(parser, begin_loc, &(struct ast) { .tag = AST_UNSIZED_DIM });
}

static struct ast* parse_array_dim(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    if (accept_token(parser, TOKEN_LBRACKET)) {
        struct ast* dim = parser->ahead->tag == TOKEN_RBRACKET
            ? parse_unsized_dim(parser, &begin_loc)
            : parse_expr(parser);
        expect_token(parser, TOKEN_RBRACKET);
        return dim;
    }
    return NULL;
}

static struct ast* parse_metadatum(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    struct ast* type = parse_type(parser);
    const char* name = parse_ident(parser);
    expect_token(parser, TOKEN_EQ);
    struct ast* init = parse_expr(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_METADATUM,
        .metadatum = {
            .type = type,
            .name = name,
            .init = init
        }
    });
}

static struct ast* parse_metadata(struct parser* parser) {
    if (accept_token(parser, TOKEN_LMETA))
        return parse_many(parser, TOKEN_RMETA, TOKEN_COMMA, parse_metadatum);
    return NULL;
}

static void parse_ignored_metadata(struct parser* parser) {
    struct file_loc loc = parser->ahead->loc;
    struct ast* metadata = parse_metadata(parser);
    loc.end = parser->behind->loc.end;
    if (metadata)
        log_warn(parser->log, &loc, "shader metadata is not allowed here");
}

static struct ast* parse_ellipsis(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_ELLIPSIS);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_PARAM,
        .param.is_ellipsis = true
    });
}

static struct ast* parse_param(struct parser* parser, bool is_shader_param) {
    struct file_loc begin_loc = parser->ahead->loc;
    bool is_output = accept_token(parser, TOKEN_OUTPUT);
    struct ast* type = parse_type(parser);
    const char* name = NULL;
    if (is_shader_param || parser->ahead->tag == TOKEN_IDENT)
        name = parse_ident(parser);
    struct ast* dim = parse_array_dim(parser);
    struct ast* init = NULL;
    struct ast* metadata = NULL;
    if (is_shader_param) {
        expect_token(parser, TOKEN_EQ);
        init = parse_expr(parser);
        metadata = parse_metadata(parser);
    }
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_PARAM,
        .param = {
            .is_output = is_output,
            .is_ellipsis = false,
            .name = name,
            .type = type,
            .dim = dim,
            .init = init,
            .metadata = metadata
        }
    });
}

static struct ast* parse_func_param(struct parser* parser) {
    if (parser->ahead->tag == TOKEN_ELLIPSIS)
        return parse_ellipsis(parser);
    return parse_param(parser, false);
}

static struct ast* parse_shader_param(struct parser* parser) {
    return parse_param(parser, true);
}

static struct ast* parse_if_stmt(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_IF);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* cond = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    struct ast* then_stmt = parse_stmt(parser);
    struct ast* else_stmt = NULL;
    if (accept_token(parser, TOKEN_ELSE))
        else_stmt = parse_stmt(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_IF_STMT,
        .if_stmt = {
            .cond = cond,
            .then_stmt = then_stmt,
            .else_stmt = else_stmt
        }
    });
}

static struct ast* parse_break_stmt(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_BREAK);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_loc, &(struct ast) { .tag = AST_BREAK_STMT });
}

static struct ast* parse_continue_stmt(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_CONTINUE);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_loc, &(struct ast) { .tag = AST_CONTINUE_STMT });
}

static struct ast* parse_return_stmt(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_RETURN);
    struct ast* value = NULL;
    if (!accept_token(parser, TOKEN_SEMICOLON)) {
        value = parse_expr(parser);
        expect_token(parser, TOKEN_SEMICOLON);
    }
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_RETURN_STMT,
        .return_stmt.value = value
    });
}

static struct ast* parse_while_loop(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_WHILE);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* cond = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    struct ast* body = parse_stmt(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_WHILE_LOOP,
        .while_loop = {
            .cond = cond,
            .body = body
        }
    });
}

static struct ast* parse_do_while_loop(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_DO);
    struct ast* body = parse_stmt(parser);
    expect_token(parser, TOKEN_WHILE);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* cond = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_DO_WHILE_LOOP,
        .while_loop = {
            .cond = cond,
            .body = body
        }
    });
}

static struct ast* parse_var(struct parser* parser, bool with_init) {
    struct file_loc begin_loc = parser->ahead->loc;
    const char* name = parse_ident(parser);
    struct ast* dim = parse_array_dim(parser);
    struct ast* init = NULL;
    if (with_init && accept_token(parser, TOKEN_EQ))
        init = parse_expr(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_VAR,
        .var = {
            .name = name,
            .dim  = dim,
            .init = init
        }
    });
}

static struct ast* parse_var_with_init(struct parser* parser) {
    return parse_var(parser, true);
}

static struct ast* parse_var_without_init(struct parser* parser) {
    return parse_var(parser, false);
}

static struct ast* parse_var_decl(struct parser* parser, struct ast* type, bool with_init, bool is_global) {
    struct ast* vars = parse_many(parser, TOKEN_SEMICOLON, TOKEN_COMMA,
        with_init ? parse_var_with_init : parse_var_without_init);
    for (struct ast* var = vars; var; var = var->next)
        var->var.is_global = is_global;
    return alloc_ast(parser, &type->loc, &(struct ast) {
        .tag = AST_VAR_DECL,
        .var_decl = {
            .type = type,
            .vars = vars
        }
    });
}

static struct ast* parse_block(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_LBRACE);
    struct ast* stmts = parse_many(parser, TOKEN_RBRACE, TOKEN_ERROR, parse_stmt);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_BLOCK,
        .block.stmts = stmts
    });
}

static struct ast* parse_block_or_error(struct parser* parser) {
    if (parser->ahead->tag == TOKEN_LBRACE)
        return parse_block(parser);
    return parse_error(parser, "block");
}

static struct ast* parse_func_decl(struct parser* parser, struct ast* ret_type) {
    const char* name = parse_ident(parser);
    parse_ignored_metadata(parser);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* params = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_func_param);

    struct ast* body = NULL;
    if (!accept_token(parser, TOKEN_SEMICOLON))
        body = parse_block_or_error(parser);

    return alloc_ast(parser, &ret_type->loc, &(struct ast) {
        .tag = AST_FUNC_DECL,
        .func_decl = {
            .ret_type = ret_type,
            .name = name,
            .params = params,
            .body = body
        }
    });
}

static struct ast* parse_for_init(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOKEN_IDENT:
            if (parser->ahead[1].tag != TOKEN_IDENT)
                break;
            [[fallthrough]];
#define x(name, ...) case TOKEN_##name:
        PRIM_TYPE_LIST(x)
        case TOKEN_CLOSURE:
            return parse_var_decl(parser, parse_type(parser), true, false);
        case TOKEN_SEMICOLON:
            return NULL;
        default:
            break;
    }
    struct ast* expr = parse_expr(parser);
    expect_token(parser, TOKEN_SEMICOLON);
    return expr;
}

static struct ast* parse_for_loop(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_FOR);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* init = parse_for_init(parser);
    struct ast* cond = NULL;
    if (!accept_token(parser, TOKEN_SEMICOLON)) {
        cond = parse_compound_expr(parser);
        expect_token(parser, TOKEN_SEMICOLON);
    }
    struct ast* inc = NULL;
    if (!accept_token(parser, TOKEN_RPAREN)) {
        inc  = parse_compound_expr(parser);
        expect_token(parser, TOKEN_RPAREN);
    }
    struct ast* body = parse_stmt(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_FOR_LOOP,
        .for_loop = {
            .init = init,
            .cond = cond,
            .inc  = inc,
            .body = body
        }
    });
}

static struct ast* parse_var_or_func_decl(struct parser* parser, bool is_top_level) {
    struct ast* type = parse_type(parser);
    if (parser->ahead[0].tag == TOKEN_IDENT &&
        parser->ahead[1].tag == TOKEN_LPAREN)
        return parse_func_decl(parser, type);
    return parse_var_decl(parser, type, true, is_top_level);
}

static struct ast* parse_empty_stmt(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_loc, &(struct ast) { .tag = AST_EMPTY_STMT });
}

static struct ast* parse_stmt(struct parser* parser) {
    switch (parser->ahead->tag) {
#define x(name, ...) case TOKEN_##name:
        PRIM_TYPE_LIST(x)
#undef x
        case TOKEN_CLOSURE:
            return parse_var_or_func_decl(parser, false);
        case TOKEN_IF:
            return parse_if_stmt(parser);
        case TOKEN_BREAK:
            return parse_break_stmt(parser);
        case TOKEN_CONTINUE:
            return parse_continue_stmt(parser);
        case TOKEN_RETURN:
            return parse_return_stmt(parser);
        case TOKEN_WHILE:
            return parse_while_loop(parser);
        case TOKEN_DO:
            return parse_do_while_loop(parser);
        case TOKEN_FOR:
            return parse_for_loop(parser);
        case TOKEN_LBRACE:
            return parse_block(parser);
        case TOKEN_IDENT:
            if (parser->ahead[1].tag == TOKEN_IDENT)
                return parse_var_or_func_decl(parser, false);
            [[fallthrough]];
        case TOKEN_INC:
        case TOKEN_DEC:
        case TOKEN_NOT:
        case TOKEN_SUB:
        case TOKEN_ADD:
        case TOKEN_TILDE:
        case TOKEN_INT_LITERAL:
        case TOKEN_FLOAT_LITERAL:
        case TOKEN_LPAREN:
        {
            struct ast* expr = parse_compound_expr(parser);
            expect_token(parser, TOKEN_SEMICOLON);
            return expr;
        }
        case TOKEN_SEMICOLON:
            return parse_empty_stmt(parser);
        default:
            return parse_error(parser, "statement");
    }
}

static struct ast* parse_shader_decl(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    struct ast* type = parse_shader_type(parser);
    const char* name = parse_ident(parser);
    struct ast* metadata = parse_metadata(parser);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* params = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_shader_param);
    struct ast* body = parse_block_or_error(parser);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_SHADER_DECL,
        .shader_decl = {
            .type = type,
            .name = name,
            .params = params,
            .body = body,
            .metadata = metadata
        }
    });
}

static struct ast* parse_field_decl(struct parser* parser) {
    return parse_var_decl(parser, parse_type(parser), false, false);
}

static struct ast* parse_struct_decl(struct parser* parser) {
    struct file_loc begin_loc = parser->ahead->loc;
    eat_token(parser, TOKEN_STRUCT);
    const char* name = parse_ident(parser);
    expect_token(parser, TOKEN_LBRACE);
    struct ast* fields = parse_many(parser, TOKEN_RBRACE, TOKEN_ERROR, parse_field_decl);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_loc, &(struct ast) {
        .tag = AST_STRUCT_DECL,
        .struct_decl = {
            .name = name,
            .fields = fields
        }
    });
}

static struct ast* parse_top_level_decl(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOKEN_STRUCT:
            return parse_struct_decl(parser);
#define x(name, ...) case TOKEN_##name:
        SHADER_TYPE_LIST(x)
#undef x
            return parse_shader_decl(parser);
#define x(name, ...) case TOKEN_##name:
        PRIM_TYPE_LIST(x)
#undef x
        case TOKEN_CLOSURE:
        case TOKEN_IDENT:
            return parse_var_or_func_decl(parser, true);
        default:
            return parse_error(parser, "top-level declaration");
    }
}

static struct ast* parse_top_level_decl_with_attrs(struct parser* parser) {
    struct ast* attrs = parse_attr_list(parser);
    struct ast* decl = parse_top_level_decl(parser);
    decl->attrs = attrs;
    return decl;
}

static struct ast* parse(
    struct mem_pool* mem_pool,
    const struct parse_input* input,
    struct log* log)
{
    struct parser parser = {
        .mem_pool = mem_pool,
        .input = *input,
        .log = log
    };
    for (size_t i = 0; i < TOKENS_AHEAD; ++i)
        read_token(&parser);
    return parse_many(&parser, TOKEN_EOF, TOKEN_ERROR, parse_top_level_decl_with_attrs);
}

static struct token next_token_from_lexer(void* data) {
    struct lexer* lexer = data;
    // Skip new lines. Those are needed by the preprocessor, but we do not need them during parsing.
    while (true) {
        struct token token = lexer_advance(lexer);
        if (token.tag != TOKEN_NL)
            return token;
    }
}

struct ast* parse_with_lexer(struct mem_pool* mem_pool, struct lexer* lexer, struct log* log) {
    struct parse_input input = {
        .data = lexer,
        .next_token = next_token_from_lexer
    };
    return parse(mem_pool, &input, log);
}

static struct token next_token_from_preprocessor(void* data) {
    struct preprocessor* preprocessor = data;
    return preprocessor_advance(preprocessor);
}

struct ast* parse_with_preprocessor(
    struct mem_pool* mem_pool,
    struct preprocessor* preprocessor,
    struct log* log)
{
    struct parse_input input = {
        .data = preprocessor,
        .next_token = next_token_from_preprocessor
    };
    return parse(mem_pool, &input, log);
}
