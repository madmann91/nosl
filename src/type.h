#pragma once

#include "ast.h"

#include <stddef.h>

enum type_tag {
    TYPE_PRIM,
    TYPE_SHADER,
    TYPE_ARRAY,
    TYPE_FUNC,
    TYPE_STRUCT
};

struct func_param {
    const struct type* type;
    const char* name;
    bool is_output;
};

struct struct_field {
    const struct type* type;
    const char* name;
};

struct type {
    size_t id;
    enum type_tag tag;
    union {
        enum prim_type_tag prim_type;
        enum shader_type_tag shader_type;
        struct {
            const struct type* elem_type;
            size_t elem_count;
        } array_type;
        struct {
            const struct type* ret_type;
            struct func_param* params;
            size_t param_count;
        } func_type;
        struct {
            struct struct_field* fields;
            size_t field_count;
            const char* name;
        } struct_type;
    };
};

[[nodiscard]] bool type_tag_is_nominal(enum type_tag);
[[nodiscard]] bool type_is_unsized_array(const struct type*);
[[nodiscard]] bool type_is_nominal(const struct type* type);
