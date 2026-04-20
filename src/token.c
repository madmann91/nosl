#include "token.h"

#include <assert.h>

VEC_IMPL(token_vec, struct token, PUBLIC)
SMALL_VEC_IMPL(small_token_vec, struct token, PUBLIC)

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

struct str_view token_printable_contents(const struct token* token) {
    if (token_tag_is_keyword(token->tag) ||
        token_tag_is_symbol(token->tag) ||
        token->tag == TOKEN_ERROR ||
        token->tag == TOKEN_IDENT ||
        token->tag == TOKEN_INT_LITERAL ||
        token->tag == TOKEN_FLOAT_LITERAL ||
        token->tag == TOKEN_STRING_LITERAL)
        return token->contents;
    const char* raw_str = token_tag_to_string(token->tag);
    return STR_VIEW(raw_str);
}
