#include "builtins.h"

struct builtins;

struct builtins* builtins_create(struct type_table*) { return NULL; }
void builtins_destroy(struct builtins*) {}

void builtins_populate_env(const struct builtins*, struct env*) {}
struct ast* builtins_constructors(const struct builtins*, enum prim_type_tag) { return NULL; }
