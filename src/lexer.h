#pragma once

#include <overture/log.h>

#include "token.h"

struct lexer {
    const char* file_data;
    const char* file_name;
    struct source_pos source_pos;
    size_t bytes_left;
};

[[nodiscard]] struct lexer lexer_create(const char* file_name, const char* file_data, size_t file_size);
struct token lexer_advance(struct lexer*);
