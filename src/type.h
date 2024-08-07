#pragma once

#include "ast.h"

#include <stddef.h>

#include <overture/vec.h>

enum type_tag {
    TYPE_ERROR,
    TYPE_PRIM,
    TYPE_CLOSURE,
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
            const struct type* inner_type;
        } closure_type;
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

SMALL_VEC_DECL(small_type_vec, const struct type*, PUBLIC)

[[nodiscard]] bool type_tag_is_nominal(enum type_tag);
[[nodiscard]] bool type_is_unsized_array(const struct type*);
[[nodiscard]] bool type_is_nominal(const struct type*);
[[nodiscard]] bool type_is_void(const struct type*);
[[nodiscard]] bool type_is_prim_type(const struct type*, enum prim_type_tag);

struct type_print_options {
    bool disable_colors;
};

[[nodiscard]] char* type_to_string(const struct type*, const struct type_print_options*);

void type_print(FILE*, const struct type*, const struct type_print_options*);
