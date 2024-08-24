#pragma once

#include <stdbool.h>

struct ast;
struct log;
struct mem_pool;
struct type_table;
struct preamble;

void check(
    struct mem_pool* mem_pool,
    struct type_table* type_table,
    const struct preamble* preamble,
    struct ast* ast,
    struct log* log);
