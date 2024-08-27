#include "builtins.h"
#include "type_table.h"
#include "ast.h"
#include "env.h"

#include <overture/mem_pool.h>
#include <overture/mem.h>

#include <assert.h>

struct builtins {
    struct ast* constructors[PRIM_TYPE_COUNT];
    struct ast* global_variables;
    struct ast* math_functions;
    struct ast* geom_functions;
    struct ast* color_functions;
    struct ast* constants;
    struct ast* operators;
    struct mem_pool mem_pool;
    struct type_table* type_table;
};

static inline struct ast* alloc_builtin(struct builtins* builtins, const char* name, const struct type* type) {
    struct ast* ast = MEM_POOL_ALLOC(builtins->mem_pool, struct ast);
    memset(ast, 0, sizeof(struct ast));
    ast->tag = AST_BUILTIN;
    ast->type = type;
    ast->builtin.name = name;
    return ast;
}

static inline void append_builtin(struct ast** builtin_list, struct ast* builtin) {
    builtin->next = *builtin_list;
    *builtin_list = builtin;
}

static inline struct ast* make_global_variable_or_constant(
    struct builtins* builtins,
    enum prim_type_tag tag,
    const char* name)
{
    return alloc_builtin(builtins, name, type_table_make_prim_type(builtins->type_table, tag));
}

static inline struct ast* make_quaternary_function(
    struct builtins* builtins,
    const char* name,
    enum prim_type_tag first_tag,
    enum prim_type_tag second_tag,
    enum prim_type_tag third_tag,
    enum prim_type_tag fourth_tag,
    enum prim_type_tag ret_tag,
    bool is_first_output,
    bool is_second_output,
    bool is_third_output,
    bool is_fourth_output)
{
    const struct type* first_type  = type_table_make_prim_type(builtins->type_table, first_tag);
    const struct type* second_type = type_table_make_prim_type(builtins->type_table, second_tag);
    const struct type* third_type  = type_table_make_prim_type(builtins->type_table, third_tag);
    const struct type* fourth_type = type_table_make_prim_type(builtins->type_table, fourth_tag);
    const struct type* ret_type    = type_table_make_prim_type(builtins->type_table, ret_tag);
    struct func_param func_params[] = {
        { first_type,  is_first_output },
        { second_type, is_second_output },
        { third_type,  is_third_output },
        { fourth_type, is_fourth_output }
    };
    return alloc_builtin(builtins, name,
        type_table_make_func_type(builtins->type_table, ret_type, func_params, 4, false));
}

static inline struct ast* make_ternary_function(
    struct builtins* builtins,
    const char* name,
    enum prim_type_tag first_tag,
    enum prim_type_tag second_tag,
    enum prim_type_tag third_tag,
    enum prim_type_tag ret_tag,
    bool is_first_output,
    bool is_second_output,
    bool is_third_output)
{
    const struct type* first_type  = type_table_make_prim_type(builtins->type_table, first_tag);
    const struct type* second_type = type_table_make_prim_type(builtins->type_table, second_tag);
    const struct type* third_type  = type_table_make_prim_type(builtins->type_table, third_tag);
    const struct type* ret_type    = type_table_make_prim_type(builtins->type_table, ret_tag);
    struct func_param func_params[] = {
        { first_type,  is_first_output },
        { second_type, is_second_output },
        { third_type,  is_third_output }
    };
    return alloc_builtin(builtins, name,
        type_table_make_func_type(builtins->type_table, ret_type, func_params, 3, false));
}

static inline struct ast* make_binary_function(
    struct builtins* builtins,
    const char* name,
    enum prim_type_tag left_tag,
    enum prim_type_tag right_tag,
    enum prim_type_tag ret_tag,
    bool is_left_output,
    bool is_right_output)
{
    const struct type* left_type  = type_table_make_prim_type(builtins->type_table, left_tag);
    const struct type* right_type = type_table_make_prim_type(builtins->type_table, right_tag);
    const struct type* ret_type   = type_table_make_prim_type(builtins->type_table, ret_tag);
    struct func_param func_params[] = {
        { left_type,  is_left_output },
        { right_type, is_right_output }
    };
    return alloc_builtin(builtins, name,
        type_table_make_func_type(builtins->type_table, ret_type, func_params, 2, false));
}

static inline struct ast* make_unary_function(
    struct builtins* builtins,
    const char* name,
    enum prim_type_tag arg_tag,
    enum prim_type_tag ret_tag,
    bool is_arg_output)
{
    const struct type* arg_type = type_table_make_prim_type(builtins->type_table, arg_tag);
    const struct type* ret_type = type_table_make_prim_type(builtins->type_table, ret_tag);
    struct func_param func_params[] = { { arg_type, is_arg_output } };
    return alloc_builtin(builtins, name,
        type_table_make_func_type(builtins->type_table, ret_type, func_params, 1, false));
}

static inline struct ast* make_single_param_constructor(struct builtins* builtins, enum prim_type_tag arg_tag, enum prim_type_tag ret_tag) {
    return make_unary_function(builtins, prim_type_tag_to_string(ret_tag), arg_tag, ret_tag, false);
}

static inline struct ast* make_triple_constructor_from_components(struct builtins* builtins, enum prim_type_tag tag, bool has_space) {
    const struct type* float_type  = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_FLOAT);
    const struct type* string_type = has_space ? type_table_make_prim_type(builtins->type_table, PRIM_TYPE_STRING) : NULL;
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    struct func_param func_params[] = {
        { string_type, false },
        { float_type, false },
        { float_type, false },
        { float_type, false },
    };
    return alloc_builtin(builtins, prim_type_tag_to_string(tag),
        type_table_make_func_type(builtins->type_table, triple_type, func_params + (has_space ? 0 : 1), has_space ? 4 : 3, false));
}

static inline struct ast* make_triple_constructor_from_triple(struct builtins* builtins, enum prim_type_tag tag, enum prim_type_tag other_tag) {
    const struct type* triple_type = type_table_make_prim_type(builtins->type_table, tag);
    const struct type* other_type = type_table_make_prim_type(builtins->type_table, other_tag);
    struct func_param func_params[] = { { other_type, false } };
    return alloc_builtin(builtins, prim_type_tag_to_string(tag),
        type_table_make_func_type(builtins->type_table, triple_type, func_params, 1, false));
}

static inline struct ast* make_matrix_constructor_from_components(struct builtins* builtins, bool has_space) {
    const struct type* float_type  = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_FLOAT);
    const struct type* string_type = has_space ? type_table_make_prim_type(builtins->type_table, PRIM_TYPE_STRING) : NULL;
    const struct type* matrix_type = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_MATRIX);
    struct func_param func_params[] = {
        { string_type, false },

        { float_type, false },
        { float_type, false },
        { float_type, false },
        { float_type, false },

        { float_type, false },
        { float_type, false },
        { float_type, false },
        { float_type, false },

        { float_type, false },
        { float_type, false },
        { float_type, false },
        { float_type, false },

        { float_type, false },
        { float_type, false },
        { float_type, false },
        { float_type, false },
    };
    return alloc_builtin(builtins, "matrix",
        type_table_make_func_type(builtins->type_table, matrix_type, func_params + (has_space ? 0 : 1), has_space ? 17 : 16, false));
}

static inline struct ast* make_matrix_constructor_from_spaces(struct builtins* builtins) {
    return make_binary_function(builtins, "matrix", PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_MATRIX, false, false);
}

static inline struct ast* make_fresnel_function(struct builtins* builtins) {
    const struct type* float_type  = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_FLOAT);
    const struct type* vector_type = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_VECTOR);
    const struct type* normal_type = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_NORMAL);
    const struct type* void_type   = type_table_make_prim_type(builtins->type_table, PRIM_TYPE_VOID);
    struct func_param func_params[] = {
        { vector_type, false },
        { normal_type, false },
        { float_type, false },
        { float_type, true },
        { float_type, true },
        { vector_type, true },
        { vector_type, true },
    };
    return alloc_builtin(builtins, "matrix",
        type_table_make_func_type(builtins->type_table, void_type, func_params, 7, false));
}

static void register_constants(struct builtins* builtins) {
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_PI"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_PI_2"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_2_PI"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_2PI"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_4PI"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_2_SQRTPI"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_E"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_LN2"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_LN10"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_LOG2E"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_LOG10E"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_SQRT2"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "M_SQRT1_2"));
}

static void register_global_variables(struct builtins* builtins) {
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_POINT, "P"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, "I"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_NORMAL, "N"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_NORMAL, "Ng"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, "dPdu"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, "dPdv"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_POINT, "Ps"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "u"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "v"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "time"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, "dtime"));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, "dPdtime"));

    append_builtin(&builtins->constants,
        alloc_builtin(builtins, "Ci", type_table_make_closure_type(
            builtins->type_table, type_table_make_prim_type(builtins->type_table, PRIM_TYPE_COLOR))));
}

static void register_math_functions(struct builtins* builtins) {
    static const enum prim_type_tag tags[] = { 
        PRIM_TYPE_FLOAT,
        PRIM_TYPE_COLOR,
        PRIM_TYPE_VECTOR,
        PRIM_TYPE_POINT,
        PRIM_TYPE_NORMAL
    };
    static const size_t tag_count = sizeof(tags) / sizeof(tags[0]);
    for (size_t i = 0; i < tag_count; ++i) {
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "radians", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "degrees", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "cos", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "sin", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_ternary_function(builtins, "sincos", tags[i], tags[i], tags[i], PRIM_TYPE_VOID, false, true, true));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "tan", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "cosh", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "sinh", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "tanh", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "acos", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "asin", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "atan", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, "atan2", tags[i], tags[i], tags[i], false, false));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, "pow", tags[i], tags[i], tags[i], false, false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "exp", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "exp2", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "expm1", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "log", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "log2", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "log10", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "logb", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, "log", tags[i], PRIM_TYPE_FLOAT, tags[i], false, false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "sqrt", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "inversesqrt", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "cbrt", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "abs", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "fabs", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "sign", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "floor", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "ceil", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "round", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, "trunc", tags[i], tags[i], false));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, "mod", tags[i], tags[i], tags[i], false, false));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, "fmod", tags[i], tags[i], tags[i], false, false));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, "min", tags[i], tags[i], tags[i], false, false));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, "max", tags[i], tags[i], tags[i], false, false));
        append_builtin(&builtins->math_functions, make_ternary_function(builtins, "clamp", tags[i], tags[i], tags[i], tags[i], false, false, false));
        append_builtin(&builtins->math_functions, make_ternary_function(builtins, "mix", tags[i], tags[i], tags[i], tags[i], false, false, false));
        append_builtin(&builtins->math_functions, make_ternary_function(builtins, "select", tags[i], tags[i], tags[i], tags[i], false, false, false));
    }
    append_builtin(&builtins->math_functions, make_binary_function(builtins, "hypot", PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, false, false));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, "isnan", PRIM_TYPE_FLOAT, PRIM_TYPE_BOOL, false));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, "isinf", PRIM_TYPE_FLOAT, PRIM_TYPE_BOOL, false));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, "isfinite", PRIM_TYPE_FLOAT, PRIM_TYPE_BOOL, false));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, "erf", PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, false));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, "erfc", PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, false));
}

static void register_geom_functions(struct builtins* builtins) {
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, "dot", PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_FLOAT, false, false));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, "cross", PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, false, false));
    append_builtin(&builtins->geom_functions, make_unary_function(builtins, "length", PRIM_TYPE_VECTOR, PRIM_TYPE_FLOAT, false));
    append_builtin(&builtins->geom_functions, make_unary_function(builtins, "normalize", PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, false));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, "distance", PRIM_TYPE_POINT, PRIM_TYPE_POINT, PRIM_TYPE_FLOAT, false, false));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, "distance",
        PRIM_TYPE_POINT, PRIM_TYPE_POINT, PRIM_TYPE_POINT, PRIM_TYPE_FLOAT, false, false, false));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, "faceforward",
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, false, false, false));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, "faceforward",
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, false, false));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, "reflect",
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, false, false));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, "refract",
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_FLOAT, PRIM_TYPE_VECTOR, false, false, false));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, "rotate",
        PRIM_TYPE_POINT, PRIM_TYPE_FLOAT, PRIM_TYPE_VECTOR, PRIM_TYPE_POINT, false, false, false));
    append_builtin(&builtins->geom_functions, make_quaternary_function(builtins, "rotate",
        PRIM_TYPE_POINT, PRIM_TYPE_FLOAT, PRIM_TYPE_POINT, PRIM_TYPE_POINT, PRIM_TYPE_POINT, false, false, false, false));
    append_builtin(&builtins->geom_functions, make_fresnel_function(builtins));

    static const enum prim_type_tag tags[] = {
        PRIM_TYPE_VECTOR,
        PRIM_TYPE_POINT,
        PRIM_TYPE_NORMAL
    };
    static const size_t tag_count = sizeof(tags) / sizeof(tags[0]);
    for (size_t i = 0; i < tag_count; ++i) {
        append_builtin(&builtins->geom_functions, make_binary_function(builtins, "transform", PRIM_TYPE_MATRIX, tags[i], tags[i], false, false));
        append_builtin(&builtins->geom_functions, make_binary_function(builtins, "transform", PRIM_TYPE_STRING, tags[i], tags[i], false, false));
        append_builtin(&builtins->geom_functions, make_ternary_function(builtins, "transform", PRIM_TYPE_STRING, PRIM_TYPE_STRING, tags[i], tags[i], false, false, false));
    }
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, "transformu",
        PRIM_TYPE_STRING, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, false, false));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, "transformu",
        PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, false, false, false));
}

static void register_color_functions(struct builtins* builtins) {
    append_builtin(&builtins->color_functions, make_unary_function(builtins, "luminance", PRIM_TYPE_COLOR, PRIM_TYPE_FLOAT, false));
    append_builtin(&builtins->color_functions, make_unary_function(builtins, "blackbody", PRIM_TYPE_FLOAT, PRIM_TYPE_COLOR, false));
    append_builtin(&builtins->color_functions, make_unary_function(builtins, "wavelength_color", PRIM_TYPE_FLOAT, PRIM_TYPE_COLOR, false));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, "transformc",
        PRIM_TYPE_STRING, PRIM_TYPE_COLOR, PRIM_TYPE_COLOR, false, false));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, "transformc",
        PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_COLOR, PRIM_TYPE_COLOR, false, false, false));
}

static void register_triple_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, PRIM_TYPE_FLOAT, tag));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_components(builtins, tag, false));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_components(builtins, tag, true));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_COLOR));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_POINT));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_triple(builtins, tag, PRIM_TYPE_NORMAL));
}

static void register_scalar_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, PRIM_TYPE_FLOAT, tag));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, PRIM_TYPE_INT, tag));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, PRIM_TYPE_BOOL, tag));
}

static void register_matrix_constructors(struct builtins* builtins) {
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_single_param_constructor(builtins, PRIM_TYPE_FLOAT, PRIM_TYPE_MATRIX));
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_matrix_constructor_from_components(builtins, false));
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_matrix_constructor_from_components(builtins, true));
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_matrix_constructor_from_spaces(builtins));
}

static void register_scalar_operators(struct builtins* builtins, enum prim_type_tag tag) {
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_FLOAT) {
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__add__", tag, tag, tag, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__sub__", tag, tag, tag, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__mul__", tag, tag, tag, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__div__", tag, tag, tag, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__mod__", tag, tag, tag, false, false));
        append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__pre_inc__", tag, tag, true));
        append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__pre_dec__", tag, tag, true));
        append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__post_inc__", tag, tag, true));
        append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__post_dec__", tag, tag, true));
        append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__neg__", tag, tag, false));
    }
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_FLOAT || tag == PRIM_TYPE_STRING) {
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__lt__", tag, tag, PRIM_TYPE_BOOL, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__le__", tag, tag, PRIM_TYPE_BOOL, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__gt__", tag, tag, PRIM_TYPE_BOOL, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__ge__", tag, tag, PRIM_TYPE_BOOL, false, false));
    }
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_BOOL) {
        append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__not__", tag, tag, false));
        append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__compl__", tag, tag, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__bitand__", tag, tag, tag, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__xor__", tag, tag, tag, false, false));
        append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__bitor__", tag, tag, tag, false, false));
    }
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__eq__", tag, tag, PRIM_TYPE_BOOL, false, false));
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__ne__", tag, tag, PRIM_TYPE_BOOL, false, false));
}

static void register_matrix_or_triple_operators(struct builtins* builtins, enum prim_type_tag tag) {
    enum prim_type_tag neg_or_sub_tag = tag != PRIM_TYPE_COLOR && tag != PRIM_TYPE_MATRIX ? PRIM_TYPE_VECTOR : tag;
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__add__", tag, tag, tag, false, false));
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__sub__", tag, tag, neg_or_sub_tag, false, false));
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__mul__", tag, tag, tag, false, false));
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__div__", tag, tag, tag, false, false));
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__eq__", tag, tag, PRIM_TYPE_BOOL, false, false));
    append_builtin(&builtins->operators, make_binary_function(builtins, "__operator__ne__", tag, tag, PRIM_TYPE_BOOL, false, false));
    append_builtin(&builtins->operators, make_unary_function(builtins, "__operator__neg__", tag, neg_or_sub_tag, false));
}

struct builtins* builtins_create(struct type_table* type_table) {
    struct builtins* builtins = xcalloc(1, sizeof(struct builtins));

    builtins->mem_pool = mem_pool_create();
    builtins->type_table = type_table;

    register_constants(builtins);
    register_math_functions(builtins);
    register_geom_functions(builtins);
    register_color_functions(builtins);
    register_global_variables(builtins);

    register_scalar_constructors(builtins, PRIM_TYPE_BOOL);
    register_scalar_constructors(builtins, PRIM_TYPE_INT);
    register_scalar_constructors(builtins, PRIM_TYPE_FLOAT);

    register_triple_constructors(builtins, PRIM_TYPE_COLOR);
    register_triple_constructors(builtins, PRIM_TYPE_VECTOR);
    register_triple_constructors(builtins, PRIM_TYPE_POINT);
    register_triple_constructors(builtins, PRIM_TYPE_NORMAL);

    register_matrix_constructors(builtins);

    register_scalar_operators(builtins, PRIM_TYPE_BOOL);
    register_scalar_operators(builtins, PRIM_TYPE_INT);
    register_scalar_operators(builtins, PRIM_TYPE_FLOAT);
    register_scalar_operators(builtins, PRIM_TYPE_STRING);

    register_matrix_or_triple_operators(builtins, PRIM_TYPE_COLOR);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_VECTOR);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_POINT);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_NORMAL);
    register_matrix_or_triple_operators(builtins, PRIM_TYPE_MATRIX);

    return builtins;
}

void builtins_destroy(struct builtins* builtins) {
    mem_pool_destroy(&builtins->mem_pool);
    free(builtins);
}

static inline void insert_builtins(struct env* env, struct ast* builtins, bool allow_overloading) {
    for (struct ast* builtin = builtins; builtin; builtin = builtin->next)
        env_insert_symbol(env, builtin->builtin.name, builtin, allow_overloading);
}

void builtins_populate_env(const struct builtins* builtins, struct env* env) {
    insert_builtins(env, builtins->constants, false);
    insert_builtins(env, builtins->global_variables, false);
    insert_builtins(env, builtins->math_functions, true);
    insert_builtins(env, builtins->geom_functions, true);
    insert_builtins(env, builtins->color_functions, true);
    insert_builtins(env, builtins->operators, true);
}

struct small_ast_vec builtins_list_constructors(const struct builtins* builtins, enum prim_type_tag tag) {
    struct small_ast_vec constructors;
    small_ast_vec_init(&constructors);
    for (struct ast* constructor = builtins->constructors[tag]; constructor; constructor = constructor->next)
        small_ast_vec_push(&constructors, &constructor);
    return constructors;
}
