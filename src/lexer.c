#include "lexer.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>

struct lexer lexer_create(const char* file_name, const char* file_data, size_t size) {
    return (struct lexer) {
        .file_data = file_data,
        .bytes_left = size,
        .source_pos = { .row = 1, .col = 1, .bytes = 0 },
        .file_name = file_name
    };
}

static inline bool is_eof(const struct lexer* lexer) {
    return lexer->bytes_left == 0;
}

static inline char next_char(const struct lexer* lexer, size_t i) {
    assert(i < lexer->bytes_left);
    return lexer->file_data[lexer->source_pos.bytes + i];
}

static inline char cur_char(const struct lexer* lexer) {
    return next_char(lexer, 0);
}

static inline void eat_char(struct lexer* lexer) {
    assert(!is_eof(lexer));
    if (cur_char(lexer) == '\n') {
        lexer->source_pos.row++;
        lexer->source_pos.col = 1;
    } else {
        lexer->source_pos.col++;
    }
    lexer->source_pos.bytes++;
    lexer->bytes_left--;
}

static inline bool accept_char(struct lexer* lexer, char c) {
    if (!is_eof(lexer) && cur_char(lexer) == c) {
        eat_char(lexer);
        return true;
    }
    return false;
}

static inline void eat_spaces(struct lexer* lexer) {
    while (!is_eof(lexer) && isspace(cur_char(lexer)) && cur_char(lexer) != '\n')
        eat_char(lexer);
}

static inline struct token make_token(
    struct lexer* lexer,
    const struct source_pos* begin_pos,
    enum token_tag tag)
{
    return (struct token) {
        .tag = tag,
        .loc = {
            .file_name = lexer->file_name,
            .begin = *begin_pos,
            .end = lexer->source_pos
        }
    };
}

static inline struct token make_error_token(
    struct lexer* lexer,
    const struct source_pos* begin_pos,
    enum token_error error)
{
    struct token token = make_token(lexer, begin_pos, TOKEN_ERROR);
    token.error = error;
    return token;
}

static inline bool accept_digit(struct lexer* lexer, int base) {
    if (is_eof(lexer))
        return false;
    char c = cur_char(lexer);
    if ((base ==  2 && (c == '0' || c == '1')) ||
        (base == 10 && isdigit(c)) ||
        (base == 16 && isxdigit(c)))
    {
        eat_char(lexer);
        return true;
    }
    return false;
}

static inline bool accept_exp(struct lexer* lexer, int base) {
    return
        (base == 10 && (accept_char(lexer, 'e') || accept_char(lexer, 'E'))) ||
        (base == 16 && (accept_char(lexer, 'p') || accept_char(lexer, 'P')));
}

static inline struct token parse_literal(struct lexer* lexer) {
    struct source_pos begin_pos = lexer->source_pos;

    int base = 10;
    size_t prefix_len = 0;
    if (accept_char(lexer, '0')) {
        if (accept_char(lexer, 'x'))
            base = 16, prefix_len = 2;
    }

    while (accept_digit(lexer, base)) ;

    bool has_dot = false;
    if (accept_char(lexer, '.')) {
        has_dot = true;
        while (accept_digit(lexer, base)) ;
    }

    bool has_exp = false;
    if (accept_exp(lexer, base)) {
        if (!accept_char(lexer, '+'))
            accept_char(lexer, '-');
        while (accept_digit(lexer, 10)) ;
    }

    bool is_float = has_exp || has_dot;
    struct token token = make_token(lexer, &begin_pos, is_float ? TOKEN_FLOAT_LITERAL : TOKEN_INT_LITERAL);
    if (is_float)
        token.float_literal = strtod(token_view(lexer->file_data, &token).data, NULL);
    else
        token.int_literal = strtoumax(token_view(lexer->file_data, &token).data + prefix_len, NULL, base);
    return token;
}

static inline enum token_tag find_keyword(struct str_view ident) {
#define x(tag, str, ...) if (str_view_is_equal(&ident, &STR_VIEW(str))) return TOKEN_##tag;
    KEYWORD_LIST(x)
#undef x
    if (str_view_is_equal(&ident, &STR_VIEW("and"))) return TOKEN_AND;
    if (str_view_is_equal(&ident, &STR_VIEW("or")))  return TOKEN_OR;
    if (str_view_is_equal(&ident, &STR_VIEW("not"))) return TOKEN_NOT;
    return TOKEN_ERROR;
}

struct token lexer_advance(struct lexer* lexer) {
    while (true) {
        eat_spaces(lexer);

        struct source_pos begin_pos = lexer->source_pos;
        if (is_eof(lexer))
            return make_token(lexer, &begin_pos, TOKEN_EOF);

        if (accept_char(lexer, '\n')) return make_token(lexer, &begin_pos, TOKEN_NL);
        if (accept_char(lexer, '(' )) return make_token(lexer, &begin_pos, TOKEN_LPAREN);
        if (accept_char(lexer, ')' )) return make_token(lexer, &begin_pos, TOKEN_RPAREN);
        if (accept_char(lexer, '{' )) return make_token(lexer, &begin_pos, TOKEN_LBRACE);
        if (accept_char(lexer, '}' )) return make_token(lexer, &begin_pos, TOKEN_RBRACE);
        if (accept_char(lexer, ';' )) return make_token(lexer, &begin_pos, TOKEN_SEMICOLON);
        if (accept_char(lexer, ',' )) return make_token(lexer, &begin_pos, TOKEN_COMMA);
        if (accept_char(lexer, '~' )) return make_token(lexer, &begin_pos, TOKEN_TILDE);
        if (accept_char(lexer, '?' )) return make_token(lexer, &begin_pos, TOKEN_QUESTION);
        if (accept_char(lexer, ':' )) return make_token(lexer, &begin_pos, TOKEN_COLON);

        if (accept_char(lexer, '#' )) {
            if (accept_char(lexer, '#'))
                return make_token(lexer, &begin_pos, TOKEN_CONCAT);
            return make_token(lexer, &begin_pos, TOKEN_HASH);
        }

        if (accept_char(lexer, '.')) {
            if (isdigit(cur_char(lexer)))
                return parse_literal(lexer);
            if (lexer->bytes_left >= 2 && cur_char(lexer) == '.' && next_char(lexer, 1) == '.') {
                eat_char(lexer);
                eat_char(lexer);
                return make_token(lexer, &begin_pos, TOKEN_ELLIPSIS);
            }
            return make_token(lexer, &begin_pos, TOKEN_DOT);
        }

        if (accept_char(lexer, '[')) {
            if (accept_char(lexer, '['))
                return make_token(lexer, &begin_pos, TOKEN_LMETA);
            return make_token(lexer, &begin_pos, TOKEN_LBRACKET);
        }

        if (accept_char(lexer, ']')) {
            if (accept_char(lexer, ']'))
                return make_token(lexer, &begin_pos, TOKEN_RMETA);
            return make_token(lexer, &begin_pos, TOKEN_RBRACKET);
        }

        if (accept_char(lexer, '!')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_CMP_NE);
            return make_token(lexer, &begin_pos, TOKEN_NOT);
        }

        if (accept_char(lexer, '=')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_CMP_EQ);
            return make_token(lexer, &begin_pos, TOKEN_EQ);
        }

        if (accept_char(lexer, '>')) {
            if (accept_char(lexer, '>')) {
                if (accept_char(lexer, '='))
                    return make_token(lexer, &begin_pos, TOKEN_RSHIFT_EQ);
                return make_token(lexer, &begin_pos, TOKEN_RSHIFT);
            }
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_CMP_GE);
            return make_token(lexer, &begin_pos, TOKEN_CMP_GT);
        }

        if (accept_char(lexer, '<')) {
            if (accept_char(lexer, '<')) {
                if (accept_char(lexer, '='))
                    return make_token(lexer, &begin_pos, TOKEN_LSHIFT_EQ);
                return make_token(lexer, &begin_pos, TOKEN_LSHIFT);
            }
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_CMP_LE);
            return make_token(lexer, &begin_pos, TOKEN_CMP_LT);
        }

        if (accept_char(lexer, '+')) {
            if (accept_char(lexer, '+'))
                return make_token(lexer, &begin_pos, TOKEN_INC);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_ADD_EQ);
            return make_token(lexer, &begin_pos, TOKEN_ADD);
        }

        if (accept_char(lexer, '-')) {
            if (accept_char(lexer, '-'))
                return make_token(lexer, &begin_pos, TOKEN_DEC);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_SUB_EQ);
            return make_token(lexer, &begin_pos, TOKEN_SUB);
        }

        if (accept_char(lexer, '*')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_MUL_EQ);
            return make_token(lexer, &begin_pos, TOKEN_MUL);
        }

        if (accept_char(lexer, '/')) {
            if (accept_char(lexer, '/')) {
                while (!is_eof(lexer) && cur_char(lexer) != '\n')
                    eat_char(lexer);
                continue;
            }
            if (accept_char(lexer, '*')) {
                while (true) {
                    if (is_eof(lexer))
                        return make_error_token(lexer, &begin_pos, TOKEN_ERROR_UNTERMINATED_COMMENT);
                    if (accept_char(lexer, '*')) {
                        if (accept_char(lexer, '/'))
                            break;
                    } else {
                        eat_char(lexer);
                    }
                }
                continue;
            }
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_DIV_EQ);
            return make_token(lexer, &begin_pos, TOKEN_DIV);
        }

        if (accept_char(lexer, '%')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_REM_EQ);
            return make_token(lexer, &begin_pos, TOKEN_REM);
        }

        if (accept_char(lexer, '&')) {
            if (accept_char(lexer, '&'))
                return make_token(lexer, &begin_pos, TOKEN_LOGIC_AND);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_AND_EQ);
            return make_token(lexer, &begin_pos, TOKEN_AND);
        }

        if (accept_char(lexer, '|')) {
            if (accept_char(lexer, '|'))
                return make_token(lexer, &begin_pos, TOKEN_LOGIC_OR);
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_OR_EQ);
            return make_token(lexer, &begin_pos, TOKEN_OR);
        }

        if (accept_char(lexer, '^')) {
            if (accept_char(lexer, '='))
                return make_token(lexer, &begin_pos, TOKEN_XOR_EQ);
            return make_token(lexer, &begin_pos, TOKEN_XOR);
        }

        if (isdigit(cur_char(lexer)))
            return parse_literal(lexer);

        if (accept_char(lexer, '"')) {
            while (!is_eof(lexer) && cur_char(lexer) != '\n') {
                if (accept_char(lexer, '"'))
                    return make_token(lexer, &begin_pos, TOKEN_STRING_LITERAL);
                eat_char(lexer);
            }
            return make_error_token(lexer, &begin_pos, TOKEN_ERROR_UNTERMINATED_STRING);
        }

        if (isalpha(cur_char(lexer)) || cur_char(lexer) == '_') {
            while (!is_eof(lexer) && (isalnum(cur_char(lexer)) || cur_char(lexer) == '_'))
                eat_char(lexer);
            struct token token = make_token(lexer, &begin_pos, TOKEN_IDENT);
            enum token_tag keyword_tag = find_keyword(token_view(lexer->file_data, &token));
            if (keyword_tag != TOKEN_ERROR)
                token.tag = keyword_tag;
            return token;
        }

        eat_char(lexer);
        return make_error_token(lexer, &begin_pos, TOKEN_ERROR_INVALID);
    }
}
