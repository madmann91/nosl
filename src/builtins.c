#include "builtins.h"
#include "type_table.h"
#include "ast.h"
#include "env.h"

#include <overture/mem_pool.h>
#include <overture/mem.h>

#include <assert.h>

struct builtins {
    struct ast* constructors[PRIM_TYPE_COUNT];
    struct ast* functions;
    struct ast* globals;
    struct mem_pool mem_pool;
    struct type_table* type_table;
};

#define SPLINE_FUNCTION(type) \
    "__attribute__((builtin)) " type " spline(string, float, " type "[]);\n"
#define SPLINE_FUNCTION_WITH_SIZE(type) \
    "__attribute__((builtin)) " type " spline(string, float, int, " type "[]);\n"
#define SPLIT_FUNCTION_WITH_SEP_AND_SIZE(type) \
    "__attribute__((builtin)) int split(string, output string[], string, int);\n"
#define SPLIT_FUNCTION_WITH_SEP(type) \
    "__attribute__((builtin)) int split(string, output string[], string);\n"
#define SPLIT_FUNCTION(type) \
    "__attribute__((always_inline))\n" \
    "int split(string str, output string results[]) { return split(str, results, \"\"); }\n"
#define REGEX_FUNCTION(name) \
    "__attribute__((builtin)) int " name "(string, int[], string);\n"
#define TEXTURE_FUNCTION(type) \
    "__attribute__((builtin)) " type " texture(string, float, float, ...);\n"
#define TEXTURE_FUNCTION_WITH_DXDY(type) \
    "__attribute__((builtin)) " type " texture(string, float, float, float, float, float, float, ...);\n"
#define TEXTURE3D_FUNCTION(type) \
    "__attribute__((builtin)) " type " texture3d(string, point, ...);\n"
#define TEXTURE3D_FUNCTION_WITH_DXDY(type) \
    "__attribute__((builtin)) " type " texture3d(string, point, vector, vector, vector, ...);\n"
#define ENVIRONMENT_FUNCTION(type) \
    "__attribute__((builtin)) " type " environment(string, vector, ...);\n"
#define ENVIRONMENT_FUNCTION_WITH_DXDY(type) \
    "__attribute__((builtin)) " type " environment(string, vector, vector, vector, ...);\n"
#define GETTEXTUREINFO_FUNCTION(output_type) \
    "__attribute__((builtin)) int gettextureinfo(string, string, output " output_type ");\n"
#define GETTEXTUREINFO_FUNCTION_WITH_COORDS(output_type) \
    "__attribute__((builtin)) int gettextureinfo(string, float, float, string, output " output_type ");\n"
#define POINTCLOUD_SEARCH_FUNCTION \
    "__attribute__((builtin)) int pointcloud_search(string, point, float, int, ...);\n"
#define POINTCLOUD_SEARCH_FUNCTION_WITH_SORT \
    "__attribute__((builtin)) int pointcloud_search(string, point, float, int, int, ...);\n"
#define POINTCLOUD_GET_FUNCTION(output_type) \
    "__attribute__((builtin)) int pointcloud_get(string, int[], int, string, output " output_type ");\n"
#define POINTCLOUD_WRITE_FUNCTION(output_type) \
    "__attribute__((builtin)) int pointcloud_write(string, point);\n" \
#define GLOBAL_VARIABLES \
    "__attribute__((builtin)) point P;\n" \
    "__attribute__((builtin)) vector I;\n" \
    "__attribute__((builtin)) normal N;\n" \
    "__attribute__((builtin)) normal Ng;\n" \
    "__attribute__((builtin)) vector dPdu;\n" \
    "__attribute__((builtin)) vector dPdv;\n" \
    "__attribute__((builtin)) point Ps;\n" \
    "__attribute__((builtin)) float u;\n" \
    "__attribute__((builtin)) float v;\n" \
    "__attribute__((builtin)) float time;\n" \
    "__attribute__((builtin)) float dtime;\n" \
    "__attribute__((builtin)) vector dPdtime;\n" \
    "__attribute__((builtin)) closure color Ci;\n"
#define SINGLE_PARAMETER_CONSTRUCTOR(type, param_type) \
    "__attribute__((constructor, always_inline))\n" \
    type " __constructor__(" param_type " x) { return x; }\n"
#define TRIPLE_CONSTRUCTOR_FROM_COMPONENTS(type) \
    "__attribute__((constructor, always_inline))\n" \
    type " __constructor__(float x, float y, float z) { return { x, y, z }; }\n"
#define TRIPLE_CONSTRUCTOR_FROM_COMPONENTS_WITH_SPACE(type) \
    "__attribute__((constructor, always_inline))\n" \
    // Bool functions ------------------------------------------------------------------------------
    "__attribute__((builtin)) bool __operator__bitand__(bool, bool);\n"
    "__attribute__((builtin)) bool __operator__xor__(bool, bool);\n"
    "__attribute__((builtin)) bool __operator__bitor__(bool, bool);\n"
    "__attribute__((builtin)) bool __operator__eq__(bool, bool);\n"
    "__attribute__((builtin)) bool __operator__ne__(bool, bool);\n"
    "__attribute__((builtin)) bool __operator__not__(bool);\n"
    "__attribute__((builtin)) bool __operator__compl__(bool);\n"
    "__attribute__((always_inline, constructor)) bool __constructor__(bool b) { return b; }\n"
    "__attribute__((always_inline, constructor)) bool __constructor__(int i) { return (bool)i; }\n"
    "__attribute__((always_inline, constructor)) bool __constructor__(float f) { return (bool)f; }\n"
    // Int functions ------------------------------------------------------------------------------
    "__attribute__((builtin)) int __operator__add__(int, int);\n"
    "__attribute__((builtin)) int __operator__sub__(int, int);\n"
    "__attribute__((builtin)) int __operator__mul__(int, int);\n"
    "__attribute__((builtin)) int __operator__mod__(int, int);\n"
    "__attribute__((builtin)) int __operator__div__(int, int);\n"
    "__attribute__((builtin)) int __operator__shl__(int, int);\n"
    "__attribute__((builtin)) int __operator__shr__(int, int);\n"
    "__attribute__((builtin)) int __operator__bitand__(int, int);\n"
    "__attribute__((builtin)) int __operator__xor__(int, int);\n"
    "__attribute__((builtin)) int __operator__bitor__(int, int);\n"
    "__attribute__((builtin)) bool __operator__eq__(int, int);\n"
    "__attribute__((builtin)) bool __operator__ne__(int, int);\n"
    "__attribute__((builtin)) bool __operator__gt__(int, int);\n"
    "__attribute__((builtin)) bool __operator__ge__(int, int);\n"
    "__attribute__((builtin)) bool __operator__lt__(int, int);\n"
    "__attribute__((builtin)) bool __operator__le__(int, int);\n"
    "__attribute__((builtin)) int __operator__neg__(int);\n"
    "__attribute__((builtin)) int __operator__not__(int);\n"
    "__attribute__((builtin)) int __operator__compl__(int);\n"
    "__attribute__((builtin)) int __operator__pre_inc__(output int);\n"
    "__attribute__((builtin)) int __operator__pre_dec__(output int);\n"
    "__attribute__((builtin)) int __operator__post_inc__(output int);\n"
    "__attribute__((builtin)) int __operator__post_dec__(output int);\n"
    "__attribute__((always_inline, constructor)) int __constructor__(bool b) { return (int)b; }\n"
    "__attribute__((always_inline, constructor)) int __constructor__(int i) { return i; }\n"
    "__attribute__((always_inline, constructor)) int __constructor__(float f) { return (int)f; }\n"
    // Float functions ----------------------------------------------------------------------------
    "__attribute__((builtin)) float __operator__add__(float, float);\n"
    "__attribute__((builtin)) float __operator__sub__(float, float);\n"
    "__attribute__((builtin)) float __operator__mul__(float, float);\n"
    "__attribute__((builtin)) float __operator__div__(float, float);\n"
    "__attribute__((builtin)) bool __operator__eq__(float, float);\n"
    "__attribute__((builtin)) bool __operator__ne__(float, float);\n"
    "__attribute__((builtin)) bool __operator__gt__(float, float);\n"
    "__attribute__((builtin)) bool __operator__ge__(float, float);\n"
    "__attribute__((builtin)) bool __operator__lt__(float, float);\n"
    "__attribute__((builtin)) bool __operator__le__(float, float);\n"
    "__attribute__((builtin)) float __operator__neg__(float);\n"
    "__attribute__((builtin)) float __operator__pre_inc__(output float);\n"
    "__attribute__((builtin)) float __operator__pre_dec__(output float);\n"
    "__attribute__((builtin)) float __operator__post_inc__(output float);\n"
    "__attribute__((builtin)) float __operator__post_dec__(output float);\n"
    "__attribute__((always_inline, constructor)) float __constructor__(bool b) { return (float)b; }\n"
    "__attribute__((always_inline, constructor)) float __constructor__(int i) { return (float)i; }\n"
    "__attribute__((always_inline, constructor)) float __constructor__(float f) { return f; }\n"
    // Color functions ----------------------------------------------------------------------------
    "__attribute__((constructor, always_inline)) color __constructor__(float intensity) {\n"
    "    return intensity;\n"
    "}\n"
    "__attribute__((constructor, always_inline)) color __constructor__(float r, float g, float b) {\n"
    "    return { r, g, b };\n"
    "}\n"
    "__attribute__((constructor, always_inline)) color __constructor__(string space, float intensity) {\n"
    "    return transformc(space, \"rgb\", color(intensity));\n"
    "}\n"
    "__attribute__((constructor, always_inline)) color __constructor__(string space, float r, float g, float b) {\n"
    "    return transformc(space, \"rgb\", color(r, g, b));\n"
    "}\n"
    "__attribute__((builtin)) float luminance(color);"
    "__attribute__((builtin)) color blackbody(float);\n"
    "__attribute__((builtin)) color wavelength_color(float);\n"
    "__attribute__((builtin)) color transformc(string, string, color);\n"
    "__attribute__((builtin)) color transformc(string, color);\n"
    "__attribute__((always_inline)) color __operator__add__(color a, color b) {\n"
    "    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };\n"
    "}\n"
    "__attribute__((always_inline)) color __operator__sub__(color a, color b) {\n"
    "    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };\n"
    "}\n"
    "__attribute__((always_inline)) color __operator__mul__(color a, color b) {\n"
    "    return { a[0] * b[0], a[1] * b[1], a[2] * b[2] };\n"
    "}\n"
    "__attribute__((always_inline)) bool __operator__eq__(color a, color b) {\n"
    "    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];\n"
    "}\n"
    "__attribute__((always_inline)) bool __operator__ne__(color a, color b) {\n"
    "    return !(a == b);\n"
    "}\n"
    "__attribute__((always_inline)) color __operator__neg__(color c) {\n"
    "    return { -c[0], -c[1], -c[2] };\n"
    "}\n"
    // Matrix functions ---------------------------------------------------------------------------
    "__attribute__((always_inline)) matrix __operator__add__(matrix a, matrix b) {\n"
    "    return {\n"
    "        a[0][0] + b[0][0], a[0][1] + b[0][1], a[0][2] + b[0][2], a[0][3] + b[0][3],\n"
    "        a[1][0] + b[1][0], a[1][1] + b[1][1], a[1][2] + b[1][2], a[1][3] + b[1][3],\n"
    "        a[2][0] + b[2][0], a[2][1] + b[2][1], a[2][2] + b[2][2], a[2][3] + b[2][3],\n"
    "        a[3][0] + b[3][0], a[3][1] + b[3][1], a[3][2] + b[3][2], a[3][3] + b[3][3],\n"
    "    };\n"
    "}\n"
    "__attribute__((builtin)) matrix __operator__sub__(matrix a, matrix b) {\n"
    "    return {\n"
    "        a[0][0] - b[0][0], a[0][1] - b[0][1], a[0][2] - b[0][2], a[0][3] - b[0][3],\n"
    "        a[1][0] - b[1][0], a[1][1] - b[1][1], a[1][2] - b[1][2], a[1][3] - b[1][3],\n"
    "        a[2][0] - b[2][0], a[2][1] - b[2][1], a[2][2] - b[2][2], a[2][3] - b[2][3],\n"
    "        a[3][0] - b[3][0], a[3][1] - b[3][1], a[3][2] - b[3][2], a[3][3] - b[3][3],\n"
    "    };\n"
    "}\n"
    "__attribute__((builtin)) matrix __operator__mul__(matrix a, matrix b) {\n"
    "    return {\n"
    "        a[0][0] * b[0][0] + a[0][1] * b[1][0] + a[0][2] * b[2][0] + a[0][3] * b[3][0],\n"
    "        a[0][0] * b[0][1] + a[0][1] * b[1][1] + a[0][2] * b[2][1] + a[0][3] * b[3][1],\n"
    "        a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2] * b[2][2] + a[0][3] * b[3][2],\n"
    "        a[0][0] * b[0][3] + a[0][1] * b[1][3] + a[0][2] * b[2][3] + a[0][3] * b[3][3],\n"
    "\n"
    "        a[1][0] * b[0][0] + a[1][1] * b[1][0] + a[1][2] * b[2][0] + a[1][3] * b[3][0],\n"
    "        a[1][0] * b[0][1] + a[1][1] * b[1][1] + a[1][2] * b[2][1] + a[1][3] * b[3][1],\n"
    "        a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2] * b[2][2] + a[1][3] * b[3][2],\n"
    "        a[1][0] * b[0][3] + a[1][1] * b[1][3] + a[1][2] * b[2][3] + a[1][3] * b[3][3],\n"
    "\n"
    "        a[2][0] * b[0][0] + a[2][1] * b[1][0] + a[2][2] * b[2][0] + a[2][3] * b[3][0],\n"
    "        a[2][0] * b[0][1] + a[2][1] * b[1][1] + a[2][2] * b[2][1] + a[2][3] * b[3][1],\n"
    "        a[2][0] * b[0][2] + a[2][1] * b[1][2] + a[2][2] * b[2][2] + a[2][3] * b[3][2],\n"
    "        a[2][0] * b[0][3] + a[2][1] * b[1][3] + a[2][2] * b[2][3] + a[2][3] * b[3][3],\n"
    "\n"
    "        a[3][0] * b[0][0] + a[3][1] * b[1][0] + a[3][2] * b[2][0] + a[3][3] * b[3][0],\n"
    "        a[3][0] * b[0][1] + a[3][1] * b[1][1] + a[3][2] * b[2][1] + a[3][3] * b[3][1],\n"
    "        a[3][0] * b[0][2] + a[3][1] * b[1][2] + a[3][2] * b[2][2] + a[3][3] * b[3][2],\n"
    "        a[3][0] * b[0][3] + a[3][1] * b[1][3] + a[3][2] * b[2][3] + a[3][3] * b[3][3],\n"
    "    };\n"
    "}\n"
    "__attribute__((builtin)) matrix __operator__div__(matrix, matrix);\n" // TODO
    "__attribute__((builtin)) int getmatrix(string, string, output matrix);\n"
    "__attribute__((constructor, always_inline)) matrix __constructor__(float x) { return x; }\n"
    "__attribute__((constructor, always_inline))\n"
    "matrix __constructor__(float m00, float m01, float m02, float m03,\n"
    "                       float m10, float m11, float m12, float m13,\n"
    "                       float m20, float m21, float m22, float m23,\n"
    "                       float m30, float m31, float m32, float m33)\n"
    "{\n"
    "    return { m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33 };\n"
    "}\n"
    "__attribute__((constructor, always_inline)) matrix __constructor__(string from, string to) {\n"
    "    matrix result = 1;\n"
    "    if (getmatrix(from, to, result) == 0)\n"
    "        error(\"cannot construct matrix from space %s to %s\", from, to);\n"
    "    return result;\n"
    "}\n"
    "__attribute__((constructor, always_inline)) matrix __constructor__(string space, float x) {\n"
    "    return matrix(space, \"common\") * x;\n"
    "}\n"
    "__attribute__((constructor, always_inline))\n"
    "matrix __constructor__(string space,\n"
    "                       float m00, float m01, float m02, float m03,\n"
    "                       float m10, float m11, float m12, float m13,\n"
    "                       float m20, float m21, float m22, float m23,\n"
    "                       float m30, float m31, float m32, float m33)\n"
    "{\n"
    "    return matrix(space, \"common\") * { m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33 };\n"
    "}\n"
    "__attribute__((builtin)) float determinant(matrix);\n" // TODO
    "__attribute__((always_inline)) matrix transpose(matrix m) {\n"
    "    return {\n"
    "        m[0][0], m[1][0], m[2][0], m[3][0],\n"
    "        m[0][1], m[1][1], m[2][1], m[3][1],\n"
    "        m[0][2], m[1][2], m[2][2], m[3][2],\n"
    "        m[0][3], m[1][3], m[2][3], m[3][3]\n"
    "    };\n"
    "}\n"
    // Triple functions ---------------------------------------------------------------------------
    "__attribute__((constructor, always_inline))\n"
    "vector __constructor__(string space, float x, float y, float z) {\n"
    "    return transform(matrix(space, \"common\"), vector(x, y, z));\n"
    "}\n"
    "__attribute__((constructor, always_inline))\n"
    "point __constructor__(string space, float x, float y, float z) {\n"
    "    return transform(matrix(space, \"common\"), point(x, y, z));\n"
    "}\n"
    // Fresnel function ---------------------------------------------------------------------------
    "void fresnel(\n"
    "    vector I,\n"
    "    normal N,\n"
    "    float eta,\n"
    "    output float Kr,\n"
    "    output float Kt,\n"
    "    output vector R,\n"
    "    output vector T)\n"
    "{\n"
    "    float c = dot(I, N);\n"
    "    if (c < 0)\n"
    "        c = -c;\n"
    "    R = reflect(I, N);\n"
    "    float g = 1.0 / (eta * eta) - 1.0 + c * c;\n"
    "    if (g >= 0) {\n"
    "        g = sqrt(g);\n"
    "        float beta = g - c;\n"
    "        float F = (c * (g + c) - 1.0) / (c * beta + 1.0);\n"
    "        F = 0.5 * (1.0 + F * F);\n"
    "        F *= beta * beta / ((g + c) * (g+c));\n"
    "        Kr = F;\n"
    "        Kt = (1.0 - F) * eta * eta;\n"
    "        T = refract(I, N, eta);\n"
    "    } else {\n"
    "        Kr = 1.0;\n"
    "        Kt = 0.0;\n"
    "        T = 0;\n"
    "    }\n"
    "}\n"
#define MATH_FUNCTIONS(type) \
    "__attribute__((always_inline)) " type " radians(" type " x) { return x * (M_PI / 180.0); }\n" \
    "__attribute__((always_inline)) " type " degrees(" type " x) { return x * (180.0 / M_PI); }\n" \
    "__attribute__((builtin)) " type " cos(" type ");\n" \
    "__attribute__((builtin)) " type " sin(" type ");\n" \
    "__attribute__((builtin)) void sincos(" type ", output " type ", output " type ");\n" \
    "__attribute__((builtin)) " type " tan(" type ");\n" \
    "__attribute__((builtin)) " type " cosh(" type ");\n" \
    "__attribute__((builtin)) " type " sinh(" type ");\n" \
    "__attribute__((builtin)) " type " tanh(" type ");\n" \
    "__attribute__((builtin)) " type " acos(" type ");\n" \
    "__attribute__((builtin)) " type " asin(" type ");\n" \
    "__attribute__((builtin)) " type " atan(" type ");\n" \
    "__attribute__((builtin)) " type " atan2(" type ", " type ");\n" \
    "__attribute__((builtin)) " type " pow(" type ", " type ");\n" \
    "__attribute__((builtin)) " type " exp(" type ");\n" \
    "__attribute__((builtin)) " type " exp2(" type ");\n" \
    "__attribute__((builtin)) " type " expm1(" type ");\n" \
    "__attribute__((builtin)) " type " log(" type ");\n" \
    "__attribute__((builtin)) " type " log2(" type ");\n" \
    "__attribute__((builtin)) " type " log10(" type ");\n" \
    "__attribute__((builtin)) " type " logb(" type ");\n" \
    "__attribute__((builtin)) " type " log(" type ", float);\n" \
    "__attribute__((builtin)) " type " sqrt(" type ");\n" \
    "__attribute__((builtin)) " type " inversesqrt(" type ");\n" \
    "__attribute__((builtin)) " type " cbrt(" type ");\n" \
    "__attribute__((builtin)) " type " abs(" type ");\n" \
    "__attribute__((builtin)) " type " fabs(" type ");\n" \
    "__attribute__((builtin)) " type " sign(" type ");\n" \
    "__attribute__((builtin)) " type " floor(" type ");\n" \
    "__attribute__((builtin)) " type " ceil(" type ");\n" \
    "__attribute__((builtin)) " type " round(" type ");\n" \
    "__attribute__((builtin)) " type " trunc(" type ");\n" \
    "__attribute__((builtin)) " type " mod(" type ", " type ");\n" \
    "__attribute__((builtin)) " type " fmod(" type ", " type ");\n" \
    "__attribute__((builtin)) " type " min(" type ", " type ");\n" \
    "__attribute__((builtin)) " type " max(" type ", " type ");\n" \
    "__attribute__((builtin)) " type " clamp(" type ", " type ", " type ");\n" \
    "__attribute__((builtin)) " type " mix(" type ", " type ", " type ");\n" \
    "__attribute__((builtin)) " type " select(" type ", " type ", " type ");\n" \
    "__attribute__((builtin)) " type " select(" type ", " type ", bool);\n"
#define FLOAT_FUNCTIONS \
    "__attribute__((always_inline)) float hypot(float x, float y) { return sqrt(x * x + y * y); }\n" \
    "__attribute__((always_inline)) float hypot(float x, float y, float z) { return sqrt(x * x + y * y + z * z); }\n" \
    "__attribute__((builtin)) bool isnan(float x);\n" \
    "__attribute__((builtin)) bool isinf(float x);\n" \
    "__attribute__((builtin)) bool isfinite(float x);\n" \
    "__attribute__((builtin)) float erf(float x);\n" \
    "__attribute__((builtin)) float erfc(float x);\n"

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

        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_NOISE, tags[i], PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_NOISE, tags[i], PRIM_TYPE_POINT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_NOISE, tags[i], PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_NOISE, tags[i], PRIM_TYPE_POINT, PRIM_TYPE_FLOAT));

        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_SNOISE, tags[i], PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_SNOISE, tags[i], PRIM_TYPE_POINT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_SNOISE, tags[i], PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_SNOISE, tags[i], PRIM_TYPE_POINT, PRIM_TYPE_FLOAT));

        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_CELLNOISE, tags[i], PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_CELLNOISE, tags[i], PRIM_TYPE_POINT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_CELLNOISE, tags[i], PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_CELLNOISE, tags[i], PRIM_TYPE_POINT, PRIM_TYPE_FLOAT));

        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_HASHNOISE, tags[i], PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_HASHNOISE, tags[i], PRIM_TYPE_POINT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_HASHNOISE, tags[i], PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_HASHNOISE, tags[i], PRIM_TYPE_POINT, PRIM_TYPE_FLOAT));

        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_PNOISE, tags[i], PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_PNOISE, tags[i], PRIM_TYPE_POINT, PRIM_TYPE_POINT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_PSNOISE, tags[i], PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_PSNOISE, tags[i], PRIM_TYPE_POINT, PRIM_TYPE_POINT));

        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_PNOISE, false, tags[i], 4,
            (struct builtin_param[]) {
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_PNOISE, false, tags[i], 4,
            (struct builtin_param[]) {
                { PRIM_TYPE_POINT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_POINT, false },
                { PRIM_TYPE_FLOAT, false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_PSNOISE, false, tags[i], 4,
            (struct builtin_param[]) {
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_FLOAT, false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_PSNOISE, false, tags[i], 4,
            (struct builtin_param[]) {
                { PRIM_TYPE_POINT, false },
                { PRIM_TYPE_FLOAT, false },
                { PRIM_TYPE_POINT, false },
                { PRIM_TYPE_FLOAT, false },
            }));

        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_SPLINE, true, tags[i], 3,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_FLOAT, false },
                { tags[i], false },
            }));
        append_builtin(&builtins->pattern_gen_functions, make_builtin_function(builtins, BUILTIN_SPLINE, true, tags[i], 3,
            (struct builtin_param[]) {
                { PRIM_TYPE_STRING, false },
                { PRIM_TYPE_FLOAT, false },
                { tags[i], false },
            }));

        append_builtin(&builtins->pattern_gen_functions, make_spline_function(builtins, tags[i], false));
        append_builtin(&builtins->pattern_gen_functions, make_spline_function(builtins, tags[i], true));
    }
    append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_HASH, PRIM_TYPE_INT, PRIM_TYPE_INT));
    append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_HASH, PRIM_TYPE_INT, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->pattern_gen_functions, make_unary_function(builtins, BUILTIN_HASH, PRIM_TYPE_INT, PRIM_TYPE_POINT));
    append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_HASH, PRIM_TYPE_INT, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->pattern_gen_functions, make_binary_function(builtins, BUILTIN_HASH, PRIM_TYPE_INT, PRIM_TYPE_POINT, PRIM_TYPE_FLOAT));
}

static void register_deriv_functions(struct builtins* builtins) {
    static const enum prim_type_tag tags[] = {
        PRIM_TYPE_FLOAT,
        PRIM_TYPE_COLOR,
        PRIM_TYPE_VECTOR,
        PRIM_TYPE_POINT
    };
    static const size_t tag_count = sizeof(tags) / sizeof(tags[0]);
    for (size_t i = 0; i < tag_count; ++i) {
        append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_DX, tags[i], tags[i]));
        append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_DY, tags[i], tags[i]));
        append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_DZ, tags[i], tags[i]));
    }
    append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_FILTERWIDTH, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_FILTERWIDTH, PRIM_TYPE_VECTOR, PRIM_TYPE_POINT));
    append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_FILTERWIDTH, PRIM_TYPE_VECTOR, PRIM_TYPE_VECTOR));
    append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_AREA, PRIM_TYPE_FLOAT, PRIM_TYPE_POINT));
    append_builtin(&builtins->deriv_functions, make_unary_function(builtins, BUILTIN_CALCULATENORMAL, PRIM_TYPE_VECTOR, PRIM_TYPE_POINT));
    append_builtin(&builtins->deriv_functions, make_binary_function(builtins, BUILTIN_AASTEP, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->deriv_functions, make_ternary_function(builtins, BUILTIN_AASTEP, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->deriv_functions, make_builtin_function(builtins, BUILTIN_AASTEP, false, PRIM_TYPE_FLOAT, 4,
        (struct builtin_param[]) {
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false },
            { PRIM_TYPE_FLOAT, false }
        }));
}

static void register_displace_functions(struct builtins* builtins) {
    static const enum builtin_tag builtin_tags[] = { BUILTIN_DISPLACE, BUILTIN_BUMP };
    static const size_t builtin_tag_count = sizeof(builtin_tags) / sizeof(builtin_tags[0]);
    for (size_t i = 0; i < builtin_tag_count; ++i) {
        append_builtin(&builtins->displace_functions, make_unary_function(builtins, builtin_tags[i], PRIM_TYPE_VOID, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->displace_functions, make_binary_function(builtins, builtin_tags[i], PRIM_TYPE_VOID, PRIM_TYPE_STRING, PRIM_TYPE_FLOAT));
        append_builtin(&builtins->displace_functions, make_unary_function(builtins, builtin_tags[i], PRIM_TYPE_VOID, PRIM_TYPE_VECTOR));
    }
}

static void register_string_functions(struct builtins* builtins) {
    append_builtin(&builtins->string_functions, make_builtin_function(builtins, BUILTIN_PRINTF, true, PRIM_TYPE_VOID, 1,
        (struct builtin_param[]) { { PRIM_TYPE_STRING, false } }));
    append_builtin(&builtins->string_functions, make_builtin_function(builtins, BUILTIN_ERROR, true, PRIM_TYPE_VOID, 1,
        (struct builtin_param[]) { { PRIM_TYPE_STRING, false } }));
    append_builtin(&builtins->string_functions, make_builtin_function(builtins, BUILTIN_WARNING, true, PRIM_TYPE_VOID, 1,
        (struct builtin_param[]) { { PRIM_TYPE_STRING, false } }));
    append_builtin(&builtins->string_functions, make_builtin_function(builtins, BUILTIN_FORMAT, true, PRIM_TYPE_STRING, 1,
        (struct builtin_param[]) { { PRIM_TYPE_STRING, false } }));
    append_builtin(&builtins->string_functions, make_builtin_function(builtins, BUILTIN_FPRINTF, true, PRIM_TYPE_VOID, 2,
        (struct builtin_param[]) { { PRIM_TYPE_STRING, false }, { PRIM_TYPE_STRING, false } }));
    append_builtin(&builtins->string_functions, make_binary_function(builtins, BUILTIN_CONCAT, PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_unary_function(builtins, BUILTIN_STRLEN, PRIM_TYPE_INT, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_binary_function(builtins, BUILTIN_STARTSWITH, PRIM_TYPE_BOOL, PRIM_TYPE_STRING, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_binary_function(builtins, BUILTIN_ENDSWITH, PRIM_TYPE_BOOL, PRIM_TYPE_STRING, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_unary_function(builtins, BUILTIN_STOI, PRIM_TYPE_INT, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_unary_function(builtins, BUILTIN_STOF, PRIM_TYPE_FLOAT, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_split_function(builtins, 2));
    append_builtin(&builtins->string_functions, make_split_function(builtins, 3));
    append_builtin(&builtins->string_functions, make_split_function(builtins, 4));
    append_builtin(&builtins->string_functions, make_binary_function(builtins, BUILTIN_SUBSTR, PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_INT));
    append_builtin(&builtins->string_functions, make_ternary_function(builtins, BUILTIN_SUBSTR, PRIM_TYPE_STRING, PRIM_TYPE_STRING, PRIM_TYPE_INT, PRIM_TYPE_INT));
    append_builtin(&builtins->string_functions, make_binary_function(builtins, BUILTIN_GETCHAR, PRIM_TYPE_INT, PRIM_TYPE_STRING, PRIM_TYPE_INT));
    append_builtin(&builtins->string_functions, make_unary_function(builtins, BUILTIN_HASH, PRIM_TYPE_INT, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_binary_function(builtins, BUILTIN_REGEX_SEARCH, PRIM_TYPE_INT, PRIM_TYPE_STRING, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_binary_function(builtins, BUILTIN_REGEX_MATCH, PRIM_TYPE_INT, PRIM_TYPE_STRING, PRIM_TYPE_STRING));
    append_builtin(&builtins->string_functions, make_regex_function(builtins, BUILTIN_REGEX_SEARCH));
    append_builtin(&builtins->string_functions, make_regex_function(builtins, BUILTIN_REGEX_MATCH));
}

static void register_texture_functions(struct builtins* builtins) {
    append_builtin(&builtins->texture_functions, make_texture_function(builtins, false, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->texture_functions, make_texture_function(builtins, false, PRIM_TYPE_COLOR));
    append_builtin(&builtins->texture_functions, make_texture_function(builtins, true, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->texture_functions, make_texture_function(builtins, true, PRIM_TYPE_COLOR));

    append_builtin(&builtins->texture_functions, make_texture3d_function(builtins, false, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->texture_functions, make_texture3d_function(builtins, false, PRIM_TYPE_COLOR));
    append_builtin(&builtins->texture_functions, make_texture3d_function(builtins, true, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->texture_functions, make_texture3d_function(builtins, true, PRIM_TYPE_COLOR));

    append_builtin(&builtins->texture_functions, make_environment_function(builtins, false, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->texture_functions, make_environment_function(builtins, false, PRIM_TYPE_COLOR));
    append_builtin(&builtins->texture_functions, make_environment_function(builtins, true, PRIM_TYPE_FLOAT));
    append_builtin(&builtins->texture_functions, make_environment_function(builtins, true, PRIM_TYPE_COLOR));

    static const enum prim_type_tag tags[] = {
        PRIM_TYPE_FLOAT,
        PRIM_TYPE_INT,
        PRIM_TYPE_STRING,
        PRIM_TYPE_VECTOR,
        PRIM_TYPE_POINT,
        PRIM_TYPE_NORMAL,
        PRIM_TYPE_COLOR,
        PRIM_TYPE_MATRIX,
    };
    static const size_t tag_count = sizeof(tags) / sizeof(tags[0]);
    for (size_t i = 0; i < tag_count; ++i) {
        const struct type* output_type = type_table_make_prim_type(builtins->type_table, tags[i]);
        const struct type* output_array_type = type_table_make_unsized_array_type(builtins->type_table, output_type);
        append_builtin(&builtins->texture_functions, make_gettextureinfo_function(builtins, false, output_type));
        append_builtin(&builtins->texture_functions, make_gettextureinfo_function(builtins, true,  output_type));
        append_builtin(&builtins->texture_functions, make_gettextureinfo_function(builtins, false, output_array_type));
        append_builtin(&builtins->texture_functions, make_gettextureinfo_function(builtins, true,  output_array_type));
        append_builtin(&builtins->texture_functions, make_pointcloud_get_function(builtins, tags[i]));
    }
    append_builtin(&builtins->texture_functions, make_pointcloud_search_function(builtins, true));
    append_builtin(&builtins->texture_functions, make_pointcloud_search_function(builtins, false));
    append_builtin(&builtins->texture_functions, make_pointcloud_write_function(builtins));
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
    register_deriv_functions(builtins);
    register_displace_functions(builtins);
    register_string_functions(builtins);
    register_texture_functions(builtins);

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
    insert_builtins(env, builtins->deriv_functions, true);
    insert_builtins(env, builtins->displace_functions, true);
    insert_builtins(env, builtins->string_functions, true);
    insert_builtins(env, builtins->texture_functions, true);
    insert_builtins(env, builtins->operators, true);
}

struct ast* builtins_constructors(const struct builtins* builtins, enum prim_type_tag tag) {
    return builtins->constructors[tag];
}
