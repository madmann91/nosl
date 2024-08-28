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
    struct ast* matrix_functions;
    struct ast* pattern_gen_functions;
    struct ast* constants;
    struct ast* operators;
    struct mem_pool mem_pool;
    struct type_table* type_table;
};

struct builtin_param {
    enum prim_type_tag tag;
    bool is_output;
};

static inline struct ast* alloc_builtin(struct builtins* builtins, enum builtin_tag tag, const struct type* type) {
    struct ast* ast = MEM_POOL_ALLOC(builtins->mem_pool, struct ast);
    memset(ast, 0, sizeof(struct ast));
    ast->tag = AST_BUILTIN;
    ast->type = type;
    ast->builtin.tag = tag;
    return ast;
}

static inline void append_builtin(struct ast** builtin_list, struct ast* builtin) {
    builtin->next = *builtin_list;
    *builtin_list = builtin;
}

static inline struct ast* make_global_variable_or_constant(
    struct builtins* builtins,
    enum prim_type_tag type_tag,
    enum builtin_tag builtin_tag)
{
    return alloc_builtin(builtins, builtin_tag, type_table_make_prim_type(builtins->type_table, type_tag));
}

static inline struct ast* make_builtin_function(
    struct builtins* builtins,
    enum builtin_tag builtin_tag,
    bool has_ellipsis,
    enum prim_type_tag ret_tag,
    size_t param_count,
    struct builtin_param params[param_count])
{
    const struct type* ret_type = type_table_make_prim_type(builtins->type_table, ret_tag);
    const struct type* last_type = ret_type;
    enum prim_type_tag last_tag = ret_tag;

    struct small_func_param_vec func_params;
    small_func_param_vec_init(&func_params);
    for (size_t i = 0; i < param_count; ++i) {
        const struct type* type = last_tag == params[i].tag
            ? last_type : type_table_make_prim_type(builtins->type_table, params[i].tag);
        small_func_param_vec_push(&func_params, &(struct func_param) { type, params[i].is_output });
        last_tag = params[i].tag;
        last_type = type;
    }

    const struct type* builtin_type =
        type_table_make_func_type(builtins->type_table, ret_type, func_params.elems, param_count, has_ellipsis);
    small_func_param_vec_destroy(&func_params);
    return alloc_builtin(builtins, builtin_tag, builtin_type);
}

static inline enum builtin_tag prim_type_tag_to_constructor_tag(enum prim_type_tag tag) {
    switch (tag) {
#define x(name, ...) case PRIM_TYPE_##name: return BUILTIN_##name;
        BUILTIN_CONSTRUCTORS(x)
#undef x
        default:
            assert(false && "invalid primitive type");
            return BUILTIN_FLOAT;
    }
}

static inline struct ast* make_unary_function(
    struct builtins* builtins,
    enum builtin_tag builtin_tag,
    enum prim_type_tag ret_tag,
    enum prim_type_tag arg_tag)
{
    return make_builtin_function(builtins, builtin_tag, false, ret_tag, 1, (struct builtin_param[]) { { arg_tag, false } });
}

static inline struct ast* make_binary_function(
    struct builtins* builtins,
    enum builtin_tag builtin_tag,
    enum prim_type_tag ret_tag,
    enum prim_type_tag first_arg_tag,
    enum prim_type_tag second_arg_tag)
{
    return make_builtin_function(builtins, builtin_tag, false, ret_tag, 2,
        (struct builtin_param[]) { { first_arg_tag, false }, { second_arg_tag, false } });
}

static inline struct ast* make_ternary_function(
    struct builtins* builtins,
    enum builtin_tag builtin_tag,
    enum prim_type_tag ret_tag,
    enum prim_type_tag first_arg_tag,
    enum prim_type_tag second_arg_tag,
    enum prim_type_tag third_arg_tag)
{
    return make_builtin_function(builtins, builtin_tag, false, ret_tag, 3,
        (struct builtin_param[]) { { first_arg_tag, false }, { second_arg_tag, false }, { third_arg_tag, false } });
}

static inline struct ast* make_single_param_constructor(struct builtins* builtins, enum prim_type_tag ret_tag, enum prim_type_tag arg_tag) {
    return make_unary_function(builtins, prim_type_tag_to_constructor_tag(ret_tag), ret_tag, arg_tag);
}

static inline struct ast* make_triple_constructor_from_components(struct builtins* builtins, enum prim_type_tag tag, bool has_space) {
    return make_builtin_function(builtins, prim_type_tag_to_constructor_tag(tag), false, tag, has_space ? 4 : 3,
        (struct builtin_param[]) {
            { PRIM_TYPE_STRING, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false }
        } + (has_space ? 0 : 1));
}

static inline struct ast* make_matrix_constructor_from_components(struct builtins* builtins, bool has_space) {
    return make_builtin_function(builtins, BUILTIN_MATRIX, false, PRIM_TYPE_MATRIX, has_space ? 17 : 16,
        (struct builtin_param[]) {
            { PRIM_TYPE_STRING, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false }
        } + (has_space ? 0 : 1));
}

static inline struct ast* make_matrix_constructor_from_spaces(struct builtins* builtins) {
    return make_builtin_function(builtins, BUILTIN_MATRIX, false, PRIM_TYPE_MATRIX, 2,
        (struct builtin_param[]) {
            { PRIM_TYPE_STRING, false },
            { PRIM_TYPE_STRING, false }
        });
}

static inline struct ast* make_fresnel_function(struct builtins* builtins) {
    return make_builtin_function(builtins, BUILTIN_FRESNEL, false, PRIM_TYPE_VOID, 7,
        (struct builtin_param[]) {
            { PRIM_TYPE_VECTOR, false },
            { PRIM_TYPE_NORMAL, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, true },
            { PRIM_TYPE_FLOAT, true },
            { PRIM_TYPE_VECTOR, true },
            { PRIM_TYPE_VECTOR, true },
        });
}

static void register_constants(struct builtins* builtins) {
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_PI));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_PI_2));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_2_PI));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_2PI));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_4PI));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_2_SQRTPI));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_E));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_LN2));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_LN10));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_LOG2E));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_LOG10E));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_SQRT2));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_M_SQRT1_2));
}

static void register_global_variables(struct builtins* builtins) {
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_POINT, BUILTIN_P));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, BUILTIN_I));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_NORMAL, BUILTIN_N));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_NORMAL, BUILTIN_NG));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, BUILTIN_DPDU));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, BUILTIN_DPDV));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_POINT, BUILTIN_PS));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_U));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_V));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_TIME));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_FLOAT, BUILTIN_DTIME));
    append_builtin(&builtins->constants, make_global_variable_or_constant(builtins, PRIM_TYPE_VECTOR, BUILTIN_DPDTIME));

    append_builtin(&builtins->constants,
        alloc_builtin(builtins, BUILTIN_CI, type_table_make_closure_type(
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
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_RADIANS, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_DEGREES, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_COS, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_SIN, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_builtin_function(builtins, BUILTIN_SINCOS, false, PRIM_TYPE_VOID, 3,
            (struct builtin_param[]) {
                { tags[i], false },
                { tags[i], true },
                { tags[i], true },
            }));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_TAN, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_COSH, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_SINH, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_TANH, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ACOS, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ASIN, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ATAN, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_ATAN2, tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_POW, tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_EXP, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_EXP2, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_EXPM1, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_LOG, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_LOG2, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_LOG10, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_LOGB, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_LOG, tags[i], PRIM_TYPE_FLOAT, tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_SQRT, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_INVERSESQRT, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_CBRT, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ABS, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_FABS, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_SIGN, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_FLOOR, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_CEIL, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ROUND, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_TRUNC, tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_MOD, tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_FMOD, tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_MIN, tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_MAX, tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_ternary_function(builtins, BUILTIN_CLAMP, tags[i], tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_ternary_function(builtins, BUILTIN_MIX, tags[i], tags[i], tags[i], tags[i]));
        append_builtin(&builtins->math_functions, make_ternary_function(builtins, BUILTIN_SELECT, tags[i], tags[i], tags[i], tags[i]));
    }
    append_builtin(&builtins->math_functions, make_binary_function(builtins, BUILTIN_HYPOT, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ISNAN, PRIM_TYPE_BOOL, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ISINF, PRIM_TYPE_BOOL, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ISFINITE, PRIM_TYPE_BOOL, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ERF, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->math_functions, make_unary_function(builtins, BUILTIN_ERFC, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
}

static void register_geom_functions(struct builtins* builtins) {
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_DOT, PRIM_TYPE_FLOAT, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_CROSS, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_unary_function(builtins, BUILTIN_LENGTH, PRIM_TYPE_FLOAT, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_unary_function(builtins, BUILTIN_NORMALIZE, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_DISTANCE, PRIM_TYPE_FLOAT, PRIM_TYPE_POINT, PRIM_TYPE_POINT));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, BUILTIN_DISTANCE,
        PRIM_TYPE_FLOAT, PRIM_TYPE_POINT, PRIM_TYPE_POINT, PRIM_TYPE_POINT));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, BUILTIN_FACEFORWARD,
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_FACEFORWARD,
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_REFLECT,
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, BUILTIN_REFRACT,
        PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, BUILTIN_ROTATE,
        PRIM_TYPE_POINT, PRIM_TYPE_POINT, PRIM_TYPE_FLOAT, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->geom_functions, make_builtin_function(builtins, BUILTIN_ROTATE, false, PRIM_TYPE_POINT, 4,
        (struct builtin_param[]) {
            { PRIM_TYPE_POINT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_POINT, false },
            { PRIM_TYPE_POINT, false }
        }));
    append_builtin(&builtins->geom_functions, make_fresnel_function(builtins));

    static const enum prim_type_tag tags[] = {
        PRIM_TYPE_VECTOR,
        PRIM_TYPE_POINT,
        PRIM_TYPE_NORMAL
    };
    static const size_t tag_count = sizeof(tags) / sizeof(tags[0]);
    for (size_t i = 0; i < tag_count; ++i) {
        append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_TRANSFORM, tags[i], PRIM_TYPE_MATRIX, tags[i]));
        append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_TRANSFORM, tags[i], PRIM_TYPE_STRING, tags[i]));
        append_builtin(&builtins->geom_functions, make_ternary_function(builtins, BUILTIN_TRANSFORM, tags[i], PRIM_TYPE_STRING, PRIM_TYPE_STRING, tags[i]));
    }
    append_builtin(&builtins->geom_functions, make_binary_function(builtins, BUILTIN_TRANSFORMU,
        PRIM_TYPE_FLOAT, PRIM_TYPE_STRING, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->geom_functions, make_ternary_function(builtins, BUILTIN_TRANSFORMU,
        PRIM_TYPE_FLOAT, PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_FLOAT));
}

static void register_color_functions(struct builtins* builtins) {
    append_builtin(&builtins->color_functions, make_unary_function(builtins, BUILTIN_LUMINANCE, PRIM_TYPE_FLOAT, PRIM_TYPE_COLOR));
    append_builtin(&builtins->color_functions, make_unary_function(builtins, BUILTIN_BLACKBODY, PRIM_TYPE_COLOR, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->color_functions, make_unary_function(builtins, BUILTIN_WAVELENGTH_COLOR, PRIM_TYPE_COLOR, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->color_functions, make_binary_function(builtins, BUILTIN_TRANSFORMC,
        PRIM_TYPE_COLOR, PRIM_TYPE_STRING, PRIM_TYPE_COLOR));
    append_builtin(&builtins->color_functions, make_ternary_function(builtins, BUILTIN_TRANSFORMC,
        PRIM_TYPE_COLOR, PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_COLOR));
}

static void register_matrix_functions(struct builtins* builtins) {
    append_builtin(&builtins->matrix_functions, make_builtin_function(builtins, BUILTIN_GETMATRIX, false, PRIM_TYPE_INT, 3,
        (struct builtin_param[]) {
            { PRIM_TYPE_STRING, false },
            { PRIM_TYPE_STRING, false },
            { PRIM_TYPE_MATRIX, true }
        }));
    append_builtin(&builtins->geom_functions, make_unary_function(builtins, BUILTIN_DETERMINANT, PRIM_TYPE_FLOAT, PRIM_TYPE_MATRIX ));
    append_builtin(&builtins->geom_functions, make_unary_function(builtins, BUILTIN_TRANSPOSE, PRIM_TYPE_MATRIX, PRIM_TYPE_MATRIX));
}

static void register_pattern_gen_functions(struct builtins* builtins) {
    static const enum prim_type_tag tags[] = {
        PRIM_TYPE_FLOAT,
        PRIM_TYPE_COLOR,
        PRIM_TYPE_VECTOR,
        PRIM_TYPE_POINT,
        PRIM_TYPE_NORMAL
    };
    static const size_t tag_count = sizeof(tags) / sizeof(tags[0]);
    for (size_t i = 0; i < tag_count; ++i) {
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_STEP, tags[i], tags[i], tags[i]));
        append_builtin(&builtins->pattern_gen_functions, make_ternary_function(builtins, BUILTIN_LINEARSTEP, tags[i], tags[i], tags[i], tags[i]));
        append_builtin(&builtins->pattern_gen_functions, make_ternary_function(builtins, BUILTIN_SMOOTHSTEP, tags[i], tags[i], tags[i], tags[i]));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_SMOOTH_LINEARSTEP, false, tags[i], 4,
            (struct builtin_param[]) {
                { tags[i], false },
                { tags[i], false },
                { tags[i], false },
                { tags[i], false }
            }));

        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_NOISE, true, tags[i], 2,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_FLOAT, false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_NOISE, true, tags[i], 3,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_NOISE, true, tags[i], 2,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_POINT, false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_NOISE, true, tags[i], 3,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_POINT, false },
                { PRIM_TYPE_FLOAT, false },
            }));

        append_builtin(&builtins->pattern_gen_functions, make_ternary_function(builtins, BUILTIN_PNOISE, tags[i],
            PRIM_TYPE_STRING, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_ternary_function(builtins, BUILTIN_PNOISE, tags[i],
            PRIM_TYPE_STRING, PRIM_TYPE_POINT, PRIM_TYPE_POINT));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_PNOISE, false, tags[i], 5,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_PNOISE, false, tags[i], 5,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_POINT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_POINT, false },
                { PRIM_TYPE_FLOAT, false },
            }));
    }
}

static void register_triple_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_components(builtins, tag, false));
    append_builtin(&builtins->constructors[tag], make_triple_constructor_from_components(builtins, tag, true));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_COLOR));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_POINT));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_NORMAL));
}

static void register_scalar_constructors(struct builtins* builtins, enum prim_type_tag tag) {
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_INT));
    append_builtin(&builtins->constructors[tag], make_single_param_constructor(builtins, tag, PRIM_TYPE_BOOL));
}

static void register_matrix_constructors(struct builtins* builtins) {
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_single_param_constructor(builtins, PRIM_TYPE_MATRIX, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_matrix_constructor_from_components(builtins, false));
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_matrix_constructor_from_components(builtins, true));
    append_builtin(&builtins->constructors[PRIM_TYPE_MATRIX], make_matrix_constructor_from_spaces(builtins));
}

static void register_scalar_operators(struct builtins* builtins, enum prim_type_tag tag) {
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_FLOAT) {
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_ADD, tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_SUB, tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_MUL, tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_DIV, tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_MOD, tag, tag, tag));
        append_builtin(&builtins->operators, make_builtin_function(builtins, BUILTIN_OPERATOR_PRE_INC, false, tag, 1, (struct builtin_param[]) { { tag, true } }));
        append_builtin(&builtins->operators, make_builtin_function(builtins, BUILTIN_OPERATOR_PRE_DEC, false, tag, 1, (struct builtin_param[]) { { tag, true } }));
        append_builtin(&builtins->operators, make_builtin_function(builtins, BUILTIN_OPERATOR_POST_INC, false, tag, 1, (struct builtin_param[]) { { tag, true } }));
        append_builtin(&builtins->operators, make_builtin_function(builtins, BUILTIN_OPERATOR_POST_DEC, false, tag, 1, (struct builtin_param[]) { { tag, true } }));
        append_builtin(&builtins->operators, make_unary_function(builtins, BUILTIN_OPERATOR_NEG, tag, tag));
    }
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_FLOAT || tag == PRIM_TYPE_STRING) {
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_LT, PRIM_TYPE_BOOL, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_LE, PRIM_TYPE_BOOL, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_GT, PRIM_TYPE_BOOL, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_GE, PRIM_TYPE_BOOL, tag, tag));
    }
    if (tag == PRIM_TYPE_INT || tag == PRIM_TYPE_BOOL) {
        append_builtin(&builtins->operators, make_unary_function(builtins, BUILTIN_OPERATOR_NOT, tag, tag));
        append_builtin(&builtins->operators, make_unary_function(builtins, BUILTIN_OPERATOR_COMPL, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_BITAND, tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_XOR, tag, tag, tag));
        append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_BITOR, tag, tag, tag));
    }
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_EQ, PRIM_TYPE_BOOL, tag, tag));
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_NE, PRIM_TYPE_BOOL, tag, tag));
}

static void register_matrix_or_triple_operators(struct builtins* builtins, enum prim_type_tag tag) {
    enum prim_type_tag neg_or_sub_tag = tag != PRIM_TYPE_COLOR && tag != PRIM_TYPE_MATRIX ? PRIM_TYPE_VECTOR : tag;
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_ADD, tag, tag, tag));
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_SUB, neg_or_sub_tag, tag, tag));
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_MUL, tag, tag, tag));
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_DIV, tag, tag, tag));
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_EQ, PRIM_TYPE_BOOL, tag, tag));
    append_builtin(&builtins->operators, make_binary_function(builtins, BUILTIN_OPERATOR_NE, PRIM_TYPE_BOOL, tag, tag));
    append_builtin(&builtins->operators, make_unary_function(builtins, BUILTIN_OPERATOR_NEG, neg_or_sub_tag, tag));
}

struct builtins* builtins_create(struct type_table* type_table) {
    struct builtins* builtins = xcalloc(1, sizeof(struct builtins));

    builtins->mem_pool = mem_pool_create();
    builtins->type_table = type_table;

    register_constants(builtins);
    register_global_variables(builtins);

    register_math_functions(builtins);
    register_geom_functions(builtins);
    register_color_functions(builtins);
    register_matrix_functions(builtins);
    register_pattern_gen_functions(builtins);

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
        env_insert_symbol(env, builtin_tag_to_string(builtin->builtin.tag), builtin, allow_overloading);
}

void builtins_populate_env(const struct builtins* builtins, struct env* env) {
    insert_builtins(env, builtins->constants, false);
    insert_builtins(env, builtins->global_variables, false);

    insert_builtins(env, builtins->math_functions, true);
    insert_builtins(env, builtins->geom_functions, true);
    insert_builtins(env, builtins->color_functions, true);
    insert_builtins(env, builtins->matrix_functions, true);
    insert_builtins(env, builtins->pattern_gen_functions, true);
    insert_builtins(env, builtins->operators, true);
}

struct ast* builtins_constructors(const struct builtins* builtins, enum prim_type_tag tag) {
    return builtins->constructors[tag];
}
