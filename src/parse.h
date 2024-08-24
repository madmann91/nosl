#pragma once

#include <stddef.h>

struct mem_pool;
struct log;
struct ast;

struct ast* parse(struct mem_pool*, const char* file_name, const char* data, size_t size, struct log*);
