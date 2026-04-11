#pragma once

#include <overture/str.h>
#include <overture/log.h>

#include <stddef.h>

struct cached_file {
    const char* file_name;
    struct str_view file_data;
    struct str_view* lines;
    size_t line_count;
    bool has_pragma_once;
};

struct file_cache;

[[nodiscard]] struct file_cache* file_cache_create(void);
void file_cache_destroy(struct file_cache*);
void file_cache_reset(struct file_cache*);
[[nodiscard]] struct cached_file* file_cache_find(struct file_cache*, const char* file_name);
struct cached_file* file_cache_read(struct file_cache*, const char* file_name);
