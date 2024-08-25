#pragma once

#include "ast.h"

struct env;
struct mem_pool;
struct type_table;

[[nodiscard]] struct builtins* builtins_create(struct type_table*);
void builtins_destroy(struct builtins*);

void builtins_populate_env(const struct builtins*, struct env*);
[[nodiscard]] struct small_ast_vec builtins_list_constructors(const struct builtins*, enum prim_type_tag);
