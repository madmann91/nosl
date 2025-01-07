#pragma once

#include <stddef.h>

struct mem_pool;
struct log;
struct ast;
struct preprocessor;

struct ast* parse(struct mem_pool*, struct preprocessor*, struct log*);
