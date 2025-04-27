#pragma once

#include "token.h"

#include <stddef.h>

#include <overture/vec.h>

enum type_tag {
    TYPE_ERROR,
    TYPE_PRIM,
    TYPE_CLOSURE,
    TYPE_SHADER,
    TYPE_ARRAY,
    TYPE_FUNC,
    TYPE_COMPOUND,
    TYPE_STRUCT
};

enum prim_type_tag {
#define x(name, ...) PRIM_TYPE_##name,
    PRIM_TYPE_LIST(x)
#undef x
    PRIM_TYPE_COUNT
};

enum shader_type_tag {
#define x(name, ...) SHADER_TYPE_##name,
    SHADER_TYPE_LIST(x)
#undef x
};

struct func_param {
    const struct type* type;
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
            const struct func_param* params;
            size_t param_count;
            bool has_ellipsis;
        } func_type;
        struct {
            const struct type* const* elem_types;
            size_t elem_count;
        } compound_type;
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
    COERCION_SCALAR_TO_COLOR,
    COERCION_SCALAR_TO_VECTOR,
    COERCION_SCALAR_TO_POINT,
    COERCION_SCALAR_TO_NORMAL,

    COERCION_COLOR_TO_VECTOR,
    COERCION_COLOR_TO_POINT,
    COERCION_COLOR_TO_NORMAL,
    COERCION_SPATIAL_TO_COLOR,
    COERCION_SPATIAL_TO_VECTOR,
    COERCION_SPATIAL_TO_POINT,
    COERCION_SPATIAL_TO_NORMAL,

    COERCION_TO_ARRAY,
    COERCION_TO_BOOL,
    COERCION_TO_FLOAT,
    COERCION_TO_INT,
    COERCION_TO_VOID,

    COERCION_EXACT
};

SMALL_VEC_DECL(small_type_vec, const struct type*, PUBLIC)
SMALL_VEC_DECL(small_func_param_vec, struct func_param, PUBLIC)

[[nodiscard]] bool prim_type_tag_is_triple(enum prim_type_tag);
[[nodiscard]] size_t prim_type_tag_component_count(enum prim_type_tag);
[[nodiscard]] const char* prim_type_tag_to_string(enum prim_type_tag);
[[nodiscard]] const char* shader_type_tag_to_string(enum shader_type_tag);

[[nodiscard]] enum coercion_rank type_coercion_rank(const struct type*, const struct type*);
[[nodiscard]] bool type_coercion_is_lossy(const struct type*, const struct type*);
[[nodiscard]] bool type_coercion_is_incomplete(const struct type*, const struct type*);

[[nodiscard]] bool type_is_unsized_array(const struct type*);
[[nodiscard]] bool type_is_void(const struct type*);
[[nodiscard]] bool type_is_prim_type(const struct type*, enum prim_type_tag);
[[nodiscard]] bool type_is_triple(const struct type*);
[[nodiscard]] bool type_is_point_like(const struct type*);
[[nodiscard]] bool type_is_coercible_to(const struct type*, const struct type*);
[[nodiscard]] bool type_is_castable_to(const struct type*, const struct type*);
[[nodiscard]] size_t type_component_count(const struct type*);
[[nodiscard]] const char* type_constructor_name(const struct type*);

struct type_print_options {
    bool disable_colors;
};

void type_print(FILE*, const struct type*, const struct type_print_options*);
void type_dump(const struct type*);

[[nodiscard]] char* type_to_string(const struct type*, const struct type_print_options*);
