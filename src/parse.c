#include "ast.h"
#include "parse.h"
#include "lexer.h"

#include <overture/log.h>
#include <overture/str.h>
#include <overture/mem_pool.h>
#include <overture/mem.h>

#include <assert.h>
#include <string.h>

#define TOKEN_LOOKAHEAD 3

struct parser {
    struct lexer lexer;
    struct token ahead[TOKEN_LOOKAHEAD];
    struct source_pos prev_end;
    struct mem_pool* mem_pool;
};

static struct ast* parse_type(struct parser*);
static struct ast* parse_expr(struct parser*);
static struct ast* parse_stmt(struct parser*);

static inline void next_token(struct parser* parser) {
    parser->prev_end = parser->ahead->source_range.end;
    for (size_t i = 1; i < TOKEN_LOOKAHEAD; ++i)
        parser->ahead[i - 1] = parser->ahead[i];
    parser->ahead[TOKEN_LOOKAHEAD - 1] = lexer_advance(&parser->lexer);
}

static inline void eat_token(struct parser* parser, [[maybe_unused]] enum token_tag tag) {
    assert(parser->ahead[0].tag == tag);
    next_token(parser);
}

static inline bool accept_token(struct parser* parser, enum token_tag tag) {
    if (parser->ahead->tag == tag) {
        next_token(parser);
        return true;
    }
    return false;
}

static inline bool expect_token(struct parser* parser, enum token_tag tag) {
    if (!accept_token(parser, tag)) {
        struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
        log_error(parser->lexer.log,
            &parser->ahead->source_range,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)str_view.length, str_view.data);
        return false;
    }
    return true;
}

static inline struct ast* alloc_ast(
    struct parser* parser,
    const struct source_pos* begin_pos,
    const struct ast* ast)
{
    struct ast* copy = MEM_POOL_ALLOC(*parser->mem_pool, struct ast);
    memcpy(copy, ast, sizeof(struct ast));
    copy->source_range.begin = *begin_pos;
    copy->source_range.end = parser->prev_end;
    return copy;
}

static const char* parse_ident(struct parser* parser) {
    struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
    char* name = mem_pool_alloc(parser->mem_pool, str_view.length + 1, alignof(char));
    xmemcpy(name, str_view.data, str_view.length);
    name[str_view.length] = 0;
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
    while (parser->ahead->tag != stop) {
        *prev = parse_func(parser);
        prev = &(*prev)->next;
        if (sep != TOKEN_ERROR && !accept_token(parser, sep))
            break;
    }
    expect_token(parser, stop);
    return first;
}

static struct ast* parse_error(struct parser* parser, const char* msg) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
    log_error(parser->lexer.log,
        &parser->ahead->source_range,
        "expected %s, but got '%.*s'",
        msg, (int)str_view.length, str_view.data);
    next_token(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_ERROR });
}

static struct ast* parse_bool_literal(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    bool bool_literal = parser->ahead->tag == TOKEN_TRUE;
    eat_token(parser, bool_literal ? TOKEN_TRUE : TOKEN_FALSE);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_BOOL_LITERAL,
        .bool_literal = bool_literal
    });
}

static struct ast* parse_int_literal(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    int_literal int_literal = parser->ahead->int_literal;
    eat_token(parser, TOKEN_INT_LITERAL);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_INT_LITERAL,
        .int_literal = int_literal
    });
}

static struct ast* parse_float_literal(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    float_literal float_literal = parser->ahead->float_literal;
    eat_token(parser, TOKEN_FLOAT_LITERAL);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_FLOAT_LITERAL,
        .float_literal = float_literal
    });
}

static struct ast* parse_string_literal(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    struct str str = str_create();
    while (parser->ahead->tag == TOKEN_STRING_LITERAL) {
        str_append(&str, str_view_shrink(token_str_view(parser->lexer.data, parser->ahead), 1, 1));
        eat_token(parser, TOKEN_STRING_LITERAL);
    }
    char* string_literal = mem_pool_alloc(parser->mem_pool, str.length + 1, alignof(char));
    xmemcpy(string_literal, str.data, str.length);
    string_literal[str.length] = 0;
    str_destroy(&str);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_STRING_LITERAL,
        .string_literal = string_literal
    });
}

static struct ast* parse_compound_init(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_LBRACE);
    struct ast* elems = parse_many(parser, TOKEN_RBRACE, TOKEN_COMMA, parse_expr);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_COMPOUND_INIT,
        .compound_expr.elems = elems
    });
}

static struct ast* parse_cast_expr(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_LPAREN);
    struct ast* type = parse_type(parser);
    expect_token(parser, TOKEN_RPAREN);
    struct ast* value = parse_expr(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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
    return alloc_ast(parser, &first->source_range.begin, &(struct ast) {
        .tag = AST_COMPOUND_EXPR,
        .compound_expr.elems = first
    });
}

static struct ast* parse_paren_expr(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_LPAREN);
    struct ast* inner_expr = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = parse_ident(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_IDENT_EXPR,
        .ident_expr.name = name
    });
}

static struct ast* parse_construct_expr(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    struct ast* type = parse_type(parser);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* args = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_expr);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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
        case TOKEN_CLOSURE:
            return parse_construct_expr(parser);
        default:
            return parse_error(parser, "expression");
    }
}

static struct ast* parse_pre_unary_expr(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    enum unary_expr_tag tag = UNARY_EXPR_PLUS;
    switch (parser->ahead->tag) {
        case TOKEN_NOT: tag = UNARY_EXPR_NOT;     break;
        case TOKEN_SUB: tag = UNARY_EXPR_NEG;     break;
        case TOKEN_INC: tag = UNARY_EXPR_PRE_INC; break;
        case TOKEN_DEC: tag = UNARY_EXPR_PRE_DEC; break;
        case TOKEN_ADD: tag = UNARY_EXPR_PLUS;    break;
        default:
            assert(false && "invalid prefix unary operation");
            break;
    }
    next_token(parser);
    struct ast* arg = parse_expr(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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

static struct ast* parse_ternary_expr(struct parser* parser, struct ast* cond) {
    eat_token(parser, TOKEN_QUESTION);
    struct ast* then_expr = parse_expr(parser);
    expect_token(parser, TOKEN_COLON);
    struct ast* else_expr = parse_expr(parser);
    return alloc_ast(parser, &cond->source_range.begin, &(struct ast) {
        .tag = AST_TERNARY_EXPR,
        .ternary_expr = {
            .cond = cond,
            .then_expr = then_expr,
            .else_expr = else_expr
        }
    });
}

static struct ast* parse_proj_expr(struct parser* parser, struct ast* value) {
    eat_token(parser, TOKEN_DOT);
    const char* elem = parse_ident(parser);
    return alloc_ast(parser, &value->source_range.begin, &(struct ast) {
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
    return alloc_ast(parser, &value->source_range.begin, &(struct ast) {
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
    next_token(parser);
    return alloc_ast(parser, &arg->source_range.begin, &(struct ast) {
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
    return alloc_ast(parser, &callee->source_range.begin, &(struct ast) {
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
            case TOKEN_QUESTION: expr = parse_ternary_expr(parser, expr);         break;
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
            BINARY_EXPR_LIST(x)

#undef x
            default: return left;
        }

        int next_prec = binary_expr_tag_precedence(tag);
        if (next_prec < prec) {
            left = parse_binary_expr(parser, left, next_prec);
        } else if (next_prec > prec) {
            return left;
        } else {
            next_token(parser);
            struct ast* right = parse_binary_expr(parser, parse_suffix_expr(parser), prec - 1);
            left = alloc_ast(parser, &left->source_range.begin, &(struct ast) {
                .tag = AST_BINARY_EXPR,
                .binary_expr = {
                    .tag = tag,
                    .left = left,
                    .right = right
                }
            });
        }
    }
    return parse_suffix_expr(parser);
}

static struct ast* parse_expr(struct parser* parser) {
    int max_precedence = 0;
#define x(name, tok, str, prec) max_precedence = max_precedence < prec ? prec : max_precedence;
    BINARY_EXPR_LIST(x)
#undef x
    return parse_binary_expr(parser, parse_suffix_expr(parser), max_precedence);
}

static struct ast* parse_prim_type(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
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
    next_token(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_PRIM_TYPE,
        .prim_type = {
            .is_closure = is_closure,
            .tag = tag
        }
    });
}

static struct ast* parse_shader_type(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    enum shader_type_tag tag = SHADER_TYPE_SHADER;
    switch (parser->ahead->tag) {
#define x(name, ...) case TOKEN_##name: tag = SHADER_TYPE_##name; break;
        SHADER_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid prim type tag");
            break;
    }
    next_token(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_SHADER_TYPE,
        .shader_type.tag = tag
    });
}

static struct ast* parse_named_type(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = parse_ident(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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

static struct ast* parse_array_dim(struct parser* parser) {
    if (accept_token(parser, TOKEN_LBRACKET)) {
        struct ast* dim = parse_expr(parser);
        expect_token(parser, TOKEN_RBRACKET);
        return dim;
    }
    return NULL;
}

static struct ast* parse_metadatum(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    struct ast* type = parse_type(parser);
    const char* name = parse_ident(parser);
    expect_token(parser, TOKEN_EQ);
    struct ast* init = parse_expr(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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
    struct source_range source_range = parser->ahead->source_range;
    struct ast* metadata = parse_metadata(parser);
    source_range.end = parser->prev_end;
    if (metadata)
        log_warn(parser->lexer.log, &source_range, "shader metadata is not allowed here");
}

static struct ast* parse_param(struct parser* parser, bool is_shader_param) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    bool is_output = accept_token(parser, TOKEN_OUTPUT);
    struct ast* type = parse_type(parser);
    const char* name = parse_ident(parser);
    struct ast* dim = parse_array_dim(parser);
    struct ast* init = NULL;
    struct ast* metadata = NULL;
    if (is_shader_param) {
        expect_token(parser, TOKEN_EQ);
        init = parse_expr(parser);
        metadata = parse_metadata(parser);
    }
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_PARAM,
        .param = {
            .is_output = is_output,
            .name = name,
            .type = type,
            .dim = dim,
            .init = init,
            .metadata = metadata
        }
    });
}

static struct ast* parse_func_param(struct parser* parser) {
    return parse_param(parser, false);
}

static struct ast* parse_shader_param(struct parser* parser) {
    return parse_param(parser, true);
}

static struct ast* parse_if_stmt(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_IF);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* cond = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    struct ast* then_stmt = parse_stmt(parser);
    struct ast* else_stmt = NULL;
    if (accept_token(parser, TOKEN_ELSE))
        else_stmt = parse_stmt(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_IF_STMT,
        .if_stmt = {
            .cond = cond,
            .then_stmt = then_stmt,
            .else_stmt = else_stmt
        }
    });
}

static struct ast* parse_break_stmt(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_BREAK);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_BREAK_STMT });
}

static struct ast* parse_continue_stmt(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_CONTINUE);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_CONTINUE_STMT });
}

static struct ast* parse_return_stmt(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_RETURN);
    struct ast* value = NULL;
    if (!accept_token(parser, TOKEN_SEMICOLON)) {
        value = parse_expr(parser);
        expect_token(parser, TOKEN_SEMICOLON);
    }
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_RETURN_STMT,
        .return_stmt.value = value
    });
}

static struct ast* parse_while_loop(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_WHILE);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* cond = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    struct ast* body = parse_stmt(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_WHILE_LOOP,
        .while_loop = {
            .cond = cond,
            .body = body
        }
    });
}

static struct ast* parse_do_while_loop(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_DO);
    struct ast* body = parse_stmt(parser);
    expect_token(parser, TOKEN_WHILE);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* cond = parse_compound_expr(parser);
    expect_token(parser, TOKEN_RPAREN);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_DO_WHILE_LOOP,
        .while_loop = {
            .cond = cond,
            .body = body
        }
    });
}

static struct ast* parse_var(struct parser* parser, bool with_init) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = parse_ident(parser);
    struct ast* dim = parse_array_dim(parser);
    struct ast* init = NULL;
    if (with_init && accept_token(parser, TOKEN_EQ))
        init = parse_expr(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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

static struct ast* parse_var_decl(struct parser* parser, struct ast* type, bool with_init) {
    struct ast* vars = parse_many(parser, TOKEN_SEMICOLON, TOKEN_COMMA,
        with_init ? parse_var_with_init : parse_var_without_init);
    return alloc_ast(parser, &type->source_range.begin, &(struct ast) {
        .tag = AST_VAR_DECL,
        .var_decl = {
            .type = type,
            .vars = vars
        }
    });
}

static struct ast* parse_block(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_LBRACE);
    struct ast* stmts = parse_many(parser, TOKEN_RBRACE, TOKEN_ERROR, parse_stmt);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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
    struct ast* body = parse_block_or_error(parser);
    return alloc_ast(parser, &ret_type->source_range.begin, &(struct ast) {
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
            return parse_var_decl(parser, parse_type(parser), true);
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
    struct source_pos begin_pos = parser->ahead->source_range.begin;
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
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_FOR_LOOP,
        .for_loop = {
            .init = init,
            .cond = cond,
            .inc  = inc,
            .body = body
        }
    });
}

static struct ast* parse_local_decl(struct parser* parser) {
    struct ast* type = parse_type(parser);
    if (parser->ahead[0].tag == TOKEN_IDENT &&
        parser->ahead[1].tag == TOKEN_LPAREN)
        return parse_func_decl(parser, type);
    return parse_var_decl(parser, type, true);
}

static struct ast* parse_empty_stmt(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_EMPTY_STMT });
}

static struct ast* parse_stmt(struct parser* parser) {
    switch (parser->ahead->tag) {
#define x(name, ...) case TOKEN_##name:
        PRIM_TYPE_LIST(x)
#undef x
        case TOKEN_CLOSURE:
            return parse_local_decl(parser);
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
                return parse_local_decl(parser);
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
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    struct ast* type = parse_shader_type(parser);
    const char* name = parse_ident(parser);
    struct ast* metadata = parse_metadata(parser);
    expect_token(parser, TOKEN_LPAREN);
    struct ast* params = parse_many(parser, TOKEN_RPAREN, TOKEN_COMMA, parse_shader_param);
    struct ast* body = parse_block_or_error(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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
    return parse_var_decl(parser, parse_type(parser), false);
}

static struct ast* parse_struct_decl(struct parser* parser) {
    struct source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOKEN_STRUCT);
    const char* name = parse_ident(parser);
    expect_token(parser, TOKEN_LBRACE);
    struct ast* fields = parse_many(parser, TOKEN_RBRACE, TOKEN_ERROR, parse_field_decl);
    expect_token(parser, TOKEN_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
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
            return parse_func_decl(parser, parse_type(parser));
        default:
            return parse_error(parser, "top-level declaration");
    }
}

struct ast* parse(struct mem_pool* mem_pool, const char* data, size_t size, struct log* log) {
    struct parser parser = {
        .mem_pool = mem_pool,
        .lexer = lexer_create(data, size, log)
    };
    for (size_t i = 0; i < TOKEN_LOOKAHEAD; ++i)
        next_token(&parser);
    return parse_many(&parser, TOKEN_EOF, TOKEN_ERROR, parse_top_level_decl);
}
