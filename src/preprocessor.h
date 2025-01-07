#pragma once

#include "token.h"

struct log;
struct preprocessor;

struct preprocessor_config {
    const char* const* include_paths;
    size_t include_path_count;
};

struct preprocessor* preprocessor_create(struct log*, const struct preprocessor_config*);
void preprocessor_destroy(struct preprocessor*);
bool preprocessor_include(struct preprocessor*, const char* file_name);
struct token preprocessor_advance(struct preprocessor*);
struct str_view preprocessor_view(struct preprocessor*, const struct token*);
