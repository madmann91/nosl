#pragma once

#include "token.h"

struct log;
struct preprocessor;
struct file_cache;

struct preprocessor_config {
    const char* const* include_paths;
    size_t include_path_count;
};

[[nodiscard]] struct preprocessor* preprocessor_open(
    struct log*,
    const char* file_name,
    struct file_cache*,
    const struct preprocessor_config*);

void preprocessor_close(struct preprocessor*);
struct token preprocessor_advance(struct preprocessor*);
struct str_view preprocessor_view(struct preprocessor*, const struct token*);
