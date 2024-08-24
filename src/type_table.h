#pragma once

#include "type.h"

struct type_table;
struct mem_pool;

[[nodiscard]] struct type_table* type_table_create(struct mem_pool*);
void type_table_destroy(struct type_table*);

[[nodiscard]] struct type* type_table_create_func_type(struct type_table*, size_t param_count);
[[nodiscard]] struct type* type_table_create_struct_type(struct type_table*, size_t param_count);
void type_table_finalize_type(struct type_table*, struct type*);

[[nodiscard]] const struct type* type_table_get_error_type(struct type_table*);
[[nodiscard]] const struct type* type_table_get_prim_type(struct type_table*, enum prim_type_tag);
[[nodiscard]] const struct type* type_table_get_shader_type(struct type_table*, enum shader_type_tag);
[[nodiscard]] const struct type* type_table_get_closure_type(struct type_table*, const struct type*);
[[nodiscard]] const struct type* type_table_get_sized_array_type(struct type_table*, const struct type*, size_t);
[[nodiscard]] const struct type* type_table_get_unsized_array_type(struct type_table*, const struct type*);
