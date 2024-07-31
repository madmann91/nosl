#pragma once

#include "type.h"

struct type_table;

[[nodiscard]] struct type_table* type_table_create(void);
void type_table_destroy(struct type_table*);

[[nodiscard]] const struct type* type_table_insert(struct type_table*, const struct type*);
[[nodiscard]] struct type* type_table_create_nominal_type(struct type_table*, enum type_tag);
void type_table_finalize(struct type_table*, struct type*);
