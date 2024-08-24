#pragma once

#include "ast.h"

struct mem_pool;
struct type_table;

struct preamble {
    struct ast* first_decl;
    struct ast* last_decl;
    struct ast** constructors[PRIM_TYPE_COUNT];
    size_t constructor_count[PRIM_TYPE_COUNT];
};

struct preamble preamble_build(struct mem_pool*, struct type_table*);
