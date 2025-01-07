#pragma once

#include "type.h"

#define BUILTIN_CONSTANTS(x) \
    x(M_PI,       "M_PI")  \
    x(M_PI_2,     "M_PI_2") \
    x(M_2_PI,     "M_2_PI") \
    x(M_2PI,      "M_2PI") \
    x(M_4PI,      "M_4PI") \
    x(M_2_SQRTPI, "M_2_SQRTPI") \
    x(M_E,        "M_E") \
    x(M_LN2,      "M_LN2") \
    x(M_LN10,     "M_LN10") \
    x(M_LOG2E,    "M_LOG2E") \
    x(M_LOG10E,   "M_LOG10E") \
    x(M_SQRT2,    "M_SQRT2") \
    x(M_SQRT1_2,  "M_SQRT1_2")

#define BUILTIN_GLOBALS(x) \
    x(P,       "P") \
    x(I,       "I") \
    x(N,       "N") \
    x(NG,      "Ng") \
    x(DPDU,    "dPdu") \
    x(DPDV,    "dPdv") \
    x(PS,      "Ps") \
    x(U,       "u") \
    x(V,       "v") \
    x(TIME,    "time") \
    x(DTIME,   "dtime") \
    x(DPDTIME, "dPdtime") \
    x(CI,      "Ci") 

#define BUILTIN_MATH_FUNCTIONS(x) \
    x(RADIANS,     "radians") \
    x(DEGREES,     "degrees") \
    x(COS,         "cos") \
    x(SIN,         "sin") \
    x(SINCOS,      "sincos") \
    x(TAN,         "tan") \
    x(COSH,        "cosh") \
    x(SINH,        "sinh") \
    x(TANH,        "tanh") \
    x(ACOS,        "acos") \
    x(ASIN,        "asin") \
    x(ATAN,        "atan") \
    x(ATAN2,       "atan2") \
    x(POW,         "pow") \
    x(EXP,         "exp") \
    x(EXP2,        "exp2") \
    x(EXPM1,       "expm1") \
    x(LOG,         "log") \
    x(LOG2,        "log2") \
    x(LOG10,       "log10") \
    x(LOGB,        "logb") \
    x(SQRT,        "sqrt") \
    x(INVERSESQRT, "inversesqrt") \
    x(CBRT,        "cbrt") \
    x(ABS,         "abs") \
    x(FABS,        "fabs") \
    x(SIGN,        "sign") \
    x(FLOOR,       "floor") \
    x(CEIL,        "ceil") \
    x(ROUND,       "round") \
    x(TRUNC,       "trunc") \
    x(MOD,         "mod") \
    x(FMOD,        "fmod") \
    x(MIN,         "min") \
    x(MAX,         "max") \
    x(CLAMP,       "clamp") \
    x(MIX,         "mix") \
    x(SELECT,      "select") \
    x(HYPOT,       "hypot") \
    x(ISNAN,       "isnan") \
    x(ISINF,       "isinf") \
    x(ISFINITE,    "isfinite") \
    x(ERF,         "erf") \
    x(ERFC,        "erfc")

#define BUILTIN_GEOM_FUNCTIONS(x) \
    x(DOT,         "dot") \
    x(CROSS,       "cross") \
    x(LENGTH,      "length") \
    x(NORMALIZE,   "normalize") \
    x(DISTANCE,    "distance") \
    x(FACEFORWARD, "faceforward") \
    x(REFLECT,     "reflect") \
    x(REFRACT,     "refract") \
    x(ROTATE,      "rotate") \
    x(FRESNEL,     "fresnel") \
    x(TRANSFORM,   "transform") \
    x(TRANSFORMU,  "transformu")

#define BUILTIN_COLOR_FUNCTIONS(x) \
    x(LUMINANCE,        "luminance") \
    x(BLACKBODY,        "blackbody") \
    x(WAVELENGTH_COLOR, "wavelength_color") \
    x(TRANSFORMC,       "transformc")

#define BUILTIN_MATRIX_FUNCTIONS(x) \
    x(GETMATRIX,   "getmatrix") \
    x(DETERMINANT, "determinant") \
    x(TRANSPOSE,   "transpose")

#define BUILTIN_PATTERN_GEN_FUNCTIONS(x) \
    x(STEP,              "step") \
    x(LINEARSTEP,        "linearstep") \
    x(SMOOTHSTEP,        "smoothstep") \
    x(SMOOTH_LINEARSTEP, "smooth_linearstep") \
    x(NOISE,             "noise") \
    x(PNOISE,            "pnoise") \
    x(SNOISE,            "snoise") \
    x(PSNOISE,           "psnoise") \
    x(CELLNOISE,         "cellnoise") \
    x(HASHNOISE,         "hashnoise") \
    x(HASH,              "hash") \
    x(SPLINE,            "spline") \
    x(SPLINEINVERSE,     "splineinverse")

#define BUILTIN_DERIV_FUNCTIONS(x) \
    x(DX,              "Dx") \
    x(DY,              "Dy") \
    x(DZ,              "Dz") \
    x(FILTERWIDTH,     "filterwidth") \
    x(AREA,            "area") \
    x(CALCULATENORMAL, "calculatenormal") \
    x(AASTEP,          "aastep")

#define BUILTIN_DISPLACE_FUNCTIONS(x) \
    x(DISPLACE, "displace") \
    x(BUMP,     "bump")

#define BUILTIN_STRING_FUNCTIONS(x) \
    x(PRINTF,       "printf") \
    x(FORMAT,       "format") \
    x(ERROR,        "error") \
    x(WARNING,      "warning") \
    x(FPRINTF,      "fprintf") \
    x(CONCAT,       "concat") \
    x(STRLEN,       "strlen") \
    x(STARTSWITH,   "startswith") \
    x(ENDSWITH,     "endswith") \
    x(STOI,         "stoi") \
    x(STOF,         "stof") \
    x(SPLIT,        "split") \
    x(SUBSTR,       "substr") \
    x(GETCHAR,      "getchar") \
    x(REGEX_SEARCH, "regex_search") \
    x(REGEX_MATCH,  "regex_match")

#define BUILTIN_TEXTURE_FUNCTIONS(x) \
    x(TEXTURE,           "texture") \
    x(TEXTURE3D,         "texture3d") \
    x(ENVIRONMENT,       "environment") \
    x(GETTEXTUREINFO,    "gettextureinfo") \
    x(POINTCLOUD_SEARCH, "pointcloud_search") \
    x(POINTCLOUD_GET,    "pointcloud_get") \
    x(POINTCLOUD_WRITE,  "pointcloud_write")

#define BUILTIN_CLOSURE_FUNCTIONS(x) \
    x(OREN_NAYAR_DIFFUSE_BSDF,  "oren_nayar_diffuse_bsdf") \
    x(BURLEY_DIFFUSE_BSDF,      "burley_diffuse_bsdf") \
    x(DIELECTRIC_BSDF,          "dielectric_bsdf") \
    x(CONDUCTOR_BSDF,           "conductor_bsdf") \
    x(GENERALIZED_SCHLICK_BSDF, "generalized_schlick_bsdf") \
    x(TRANSLUCENT_BSDF,         "translucent_bsdf") \
    x(TRANSPARENT_BSDF,         "transparent_bsdf") \
    x(SUBSURFACE_BSDF,          "subsurface_bsdf") \
    x(SHEEN_BSDF,               "sheen_bsdf") \
    x(ANISOTROPIC_VDF,          "anisotropic_vdf") \
    x(MEDIUM_VDF,               "medium_vdf") \
    x(UNIFORM_EDF,              "uniform_edf") \
    x(LAYER,                    "layer") \
    x(HOLDOUT,                  "holdout") \
    x(DEBUG,                    "debug") \
    x(ARTISTIC_IOR,             "artistic_ior")

#define BUILTIN_STATE_FUNCTIONS(x) \
    x(GETATTRIBUTE, "getattribute") \
    x(SETMESSAGE,   "setmessage") \
    x(GETMESSAGE,   "getmessage") \
    x(SURFACEAREA,  "surfacearea") \
    x(RAYTYPE,      "raytype") \
    x(BACKFACING,   "backfacing") \
    x(ISCONNECTED,  "isconnected") \
    x(ISCONSTANT,   "isconstant")

#define BUILTIN_DICT_FUNCTIONS(x) \
    x(DICT_FIND,  "dict_find") \
    x(DICT_NEXT,  "dict_next") \
    x(DICT_VALUE, "dict_value")

#define BUILTIN_OTHER_FUNCTIONS(x) \
    x(TRACE,       "trace") \
    x(ARRAYLENGTH, "arraylength") \
    x(EXIT,        "exit")

#define BUILTIN_OPERATORS(x) \
    x(OPERATOR_ADD,      "__operator__add__") \
    x(OPERATOR_SUB,      "__operator__sub__") \
    x(OPERATOR_MUL,      "__operator__mul__") \
    x(OPERATOR_DIV,      "__operator__div__") \
    x(OPERATOR_MOD,      "__operator__mod__") \
    x(OPERATOR_PRE_INC,  "__operator__pre_inc__") \
    x(OPERATOR_PRE_DEC,  "__operator__pre_dec__") \
    x(OPERATOR_POST_INC, "__operator__post_inc__") \
    x(OPERATOR_POST_DEC, "__operator__post_dec__") \
    x(OPERATOR_NEG,      "__operator__neg__") \
    x(OPERATOR_LT,       "__operator__lt__") \
    x(OPERATOR_LE,       "__operator__le__") \
    x(OPERATOR_GT,       "__operator__gt__") \
    x(OPERATOR_GE,       "__operator__ge__") \
    x(OPERATOR_NOT,      "__operator__not__") \
    x(OPERATOR_COMPL,    "__operator__compl__") \
    x(OPERATOR_BITAND,   "__operator__bitand__") \
    x(OPERATOR_XOR,      "__operator__xor__") \
    x(OPERATOR_BITOR,    "__operator__bitor__") \
    x(OPERATOR_EQ,       "__operator__eq__") \
    x(OPERATOR_NE,       "__operator__ne__")

#define BUILTIN_CONSTRUCTORS(x) \
    x(BOOL,   "bool") \
    x(FLOAT,  "float") \
    x(INT,    "int") \
    x(COLOR,  "color") \
    x(POINT,  "point") \
    x(VECTOR, "vector") \
    x(NORMAL, "normal") \
    x(MATRIX, "matrix")

#define BUILTIN_LIST(x) \
    BUILTIN_CONSTANTS(x) \
    BUILTIN_GLOBALS(x) \
    BUILTIN_MATH_FUNCTIONS(x) \
    BUILTIN_GEOM_FUNCTIONS(x) \
    BUILTIN_COLOR_FUNCTIONS(x) \
    BUILTIN_MATRIX_FUNCTIONS(x) \
    BUILTIN_PATTERN_GEN_FUNCTIONS(x) \
    BUILTIN_DERIV_FUNCTIONS(x) \
    BUILTIN_DISPLACE_FUNCTIONS(x) \
    BUILTIN_STRING_FUNCTIONS(x) \
    BUILTIN_TEXTURE_FUNCTIONS(x) \
    BUILTIN_CLOSURE_FUNCTIONS(x) \
    BUILTIN_OPERATORS(x) \
    BUILTIN_CONSTRUCTORS(x)

struct ast;
struct env;
struct mem_pool;
struct type_table;

[[nodiscard]] struct builtins* builtins_create(struct type_table*);
void builtins_destroy(struct builtins*);

void builtins_populate_env(const struct builtins*, struct env*);
[[nodiscard]] struct ast* builtins_constructors(const struct builtins*, enum prim_type_tag);
