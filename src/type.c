#include "type.h"

bool type_tag_is_nominal(enum type_tag tag) {
    return tag == TYPE_STRUCT || tag == TYPE_FUNC;
}

bool type_is_unsized_array(const struct type* type) {
    return type->tag == TYPE_ARRAY && type->array_type.elem_count == 0;
}

bool type_is_nominal(const struct type* type) {
    return type_tag_is_nominal(type->tag);
}
