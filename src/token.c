#include "token.h"

#include <assert.h>

VEC_IMPL(token_vec, struct token, PUBLIC)

const char* token_tag_to_string(enum token_tag tag) {
    switch (tag) {
#define x(tag, str, ...) case TOKEN_##tag: return str;
        TOKEN_LIST(x)
#undef x
        default:
            assert(false && "invalid token");
            return NULL;
    }
}

bool token_tag_is_symbol(enum token_tag tag) {
    switch (tag) {
#define x(tag, ...) case TOKEN_##tag:
        SYMBOL_LIST(x)
#undef x
            return true;
        default:
            return false;
    }
}

bool token_tag_is_keyword(enum token_tag tag) {
    switch (tag) {
#define x(tag, ...) case TOKEN_##tag:
        KEYWORD_LIST(x)
#undef x
            return true;
        default:
            return false;
    }
}

struct str_view token_view(const char* file_data, const struct token* token) {
    assert(file_data || token->loc.end.bytes == token->loc.begin.bytes);
    return (struct str_view) {
        .data = file_data + token->loc.begin.bytes,
        .length = token->loc.end.bytes - token->loc.begin.bytes
    };
}

void token_log_error(struct log* log, const char* file_data, const struct token* token) {
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
