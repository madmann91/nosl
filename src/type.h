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
            bool has_ellipsis;
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

enum coercion_rank {
    COERCION_IMPOSSIBLE,
    COERCION_ELLIPSIS,
    COERCION_SCALAR_TO_MATRIX,
    COERCION_SCALAR_TO_TRIPLE,
    COERCION_TRIPLE,
    COERCION_POINT_LIKE,
    COERCION_ARRAY,
    COERCION_INT_TO_FLOAT,
    COERCION_BOOL_TO_FLOAT,
    COERCION_BOOL_TO_INT,
    COERCION_EXACT
};

SMALL_VEC_DECL(small_type_vec, const struct type*, PUBLIC)

[[nodiscard]] enum coercion_rank type_coercion_rank(const struct type*, const struct type*);

[[nodiscard]] bool type_tag_is_nominal(enum type_tag);
[[nodiscard]] bool type_is_unsized_array(const struct type*);
[[nodiscard]] bool type_is_nominal(const struct type*);
[[nodiscard]] bool type_is_void(const struct type*);
[[nodiscard]] bool type_is_prim_type(const struct type*, enum prim_type_tag);
[[nodiscard]] bool type_is_triple(const struct type*);
[[nodiscard]] bool type_is_point_like(const struct type*);
[[nodiscard]] bool type_is_coercible_to(const struct type*, const struct type*);
[[nodiscard]] bool type_is_castable_to(const struct type*, const struct type*);
[[nodiscard]] bool type_has_same_param_and_ret_types(const struct type*, const struct type*);
[[nodiscard]] size_t type_component_count(const struct type*);

struct type_print_options {
    bool disable_colors;
};

void type_print(FILE*, const struct type*, const struct type_print_options*);
void type_dump(const struct type*);

[[nodiscard]] char* type_to_string(const struct type*, const struct type_print_options*);
