#pragma once

#include "token.h"

struct log;
struct preprocessor;

[[nodiscard]] struct preprocessor* preprocessor_open(
    struct log*,
    const char* file_name,
    const char* const* include_paths);

void preprocessor_close(struct preprocessor*);
struct token preprocessor_advance(struct preprocessor*);

void preprocessor_register_macro(struct preprocessor*, const char* name, const char* expansion);
