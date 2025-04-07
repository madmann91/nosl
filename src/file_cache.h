#pragma once

#include "token.h"

struct source_file {
    const char* file_name;
    const char* file_data;
    size_t file_size;
    struct token_vec tokens;
    const char* include_guard;
    bool has_include_guard;
};

struct file_cache;

struct file_cache* file_cache_create(void);
void file_cache_destroy(struct file_cache*);
struct source_file* file_cache_find(const struct file_cache*, const char*);
struct source_file* file_cache_insert(struct file_cache*, const char*);
