#pragma once

#include <overture/log.h>
#include <overture/str.h>

#include "token.h"

struct lexer_pos {
    struct source_pos source_pos;
    size_t bytes_read;
};

struct lexer {
    struct str_view file_name;
    struct str_view file_data;
    struct lexer_pos pos;
    bool on_new_line;
    bool has_space_before;
};

[[nodiscard]] struct lexer lexer_create(struct str_view file_name, struct str_view file_data);
struct token lexer_advance(struct lexer*);
