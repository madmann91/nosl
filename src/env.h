#pragma once

#include "ast.h"

#include <stdbool.h>

struct env;

[[nodiscard]] struct env* env_create(void);
void env_destroy(struct env*);
[[nodiscard]] struct ast* env_find_one_symbol(struct env*, const char*);
[[nodiscard]] struct small_ast_vec env_find_all_symbols(struct env*, const char*);
bool env_insert_symbol(struct env*, const char*, struct ast*, bool);
void env_push_scope(struct env*);
void env_pop_scope(struct env*);
