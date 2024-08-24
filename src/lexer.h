#pragma once

#include <overture/log.h>

#include "token.h"

struct log;

struct lexer {
    const char* data;
    size_t bytes_left;
    struct source_pos source_pos;
    const char* file_name;
    struct log* log;
};

struct lexer lexer_create(const char* file_name, const char* data, size_t size, struct log*);
struct token lexer_advance(struct lexer*);
