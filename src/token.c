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

struct token token_concat(const struct token* left, const struct token* right) {
    if (token_tag_is_keyword(left->tag) || left->tag == TOKEN_IDENT) {
        // Accept numbers, identifiers, or keywords
        if (token_tag_is_keyword(right->tag) || right->tag == TOKEN_IDENT || right->tag == TOKEN_INT_LITERAL) {
            // The result may be a keyword, or an identifier
        }
    }
}
