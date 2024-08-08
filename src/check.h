#pragma once

#include <stdbool.h>

struct ast;
struct log;
struct mem_pool;

void check(struct mem_pool*, struct ast*, struct log*);
