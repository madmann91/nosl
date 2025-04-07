#include "token.h"

#include <assert.h>

VEC_IMPL(token_vec, struct token, PUBLIC)

struct str_view token_view(const struct token* token, const char* file_data) {
    return file_loc_view(&token->loc, file_data);
}

struct str_view token_string_literal_view(const struct token* token, const char* file_data) {
    assert(token->tag == TOKEN_STRING_LITERAL);
    struct str_view str_view = token_view(token, file_data);
    if (str_view.length != 0 && str_view.data[0] == '\"') {
        assert(str_view.data[str_view.length - 1] == '\"');
        str_view.data += 1;
        str_view.length -= 2;
    }
    return str_view;
}

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
