#include "type.h"
#include "ast.h"

#include <overture/mem_stream.h>
#include <overture/term.h>

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

struct styles {
    const char* reset;
    const char* error;
    const char* keyword;
};

SMALL_VEC_IMPL(small_type_vec, const struct type*, PUBLIC)
SMALL_VEC_IMPL(small_func_param_vec, struct func_param, PUBLIC)

bool prim_type_tag_is_triple(enum prim_type_tag tag) {
    return
        tag == PRIM_TYPE_COLOR ||
        tag == PRIM_TYPE_POINT ||
        tag == PRIM_TYPE_VECTOR ||
        tag == PRIM_TYPE_NORMAL;
}

size_t prim_type_tag_component_count(enum prim_type_tag tag) {
    switch (tag) {
#define x(name, str, type, comp) case PRIM_TYPE_##name: return comp;
        PRIM_TYPE_LIST(x)
#undef x
        default:
            return 1;
    }
}

const char* prim_type_tag_to_string(enum prim_type_tag tag) {
    switch (tag) {
#define x(name, str, ...) case PRIM_TYPE_##name: return str;
        PRIM_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid prim type");
            return "";
    }
}

const char* shader_type_tag_to_string(enum shader_type_tag tag) {
    switch (tag) {
#define x(name, str, ...) case SHADER_TYPE_##name: return str;
        SHADER_TYPE_LIST(x)
#undef x
        default:
            assert(false && "invalid shader type");
            return "";
    }
}

static enum coercion_rank type_coercion_rank_prim(const struct type* from, enum prim_type_tag to_tag) {
    if (from->tag == TYPE_PRIM) {
        if (from->prim_type == to_tag)
            return COERCION_EXACT;

        static const enum coercion_rank rank_matrix[PRIM_TYPE_COUNT][PRIM_TYPE_COUNT] = {
#define x(name, ...) [PRIM_TYPE_##name][PRIM_TYPE_VOID] = COERCION_TO_VOID,
            PRIM_TYPE_LIST(x)
#undef x
            [PRIM_TYPE_BOOL  ][PRIM_TYPE_MATRIX] = COERCION_SCALAR_TO_MATRIX,
            [PRIM_TYPE_INT   ][PRIM_TYPE_MATRIX] = COERCION_SCALAR_TO_MATRIX,
            [PRIM_TYPE_FLOAT ][PRIM_TYPE_MATRIX] = COERCION_SCALAR_TO_MATRIX,
            [PRIM_TYPE_BOOL  ][PRIM_TYPE_COLOR ] = COERCION_SCALAR_TO_COLOR,
            [PRIM_TYPE_INT   ][PRIM_TYPE_COLOR ] = COERCION_SCALAR_TO_COLOR,
            [PRIM_TYPE_FLOAT ][PRIM_TYPE_COLOR ] = COERCION_SCALAR_TO_COLOR,
            [PRIM_TYPE_BOOL  ][PRIM_TYPE_VECTOR] = COERCION_SCALAR_TO_VECTOR,
            [PRIM_TYPE_INT   ][PRIM_TYPE_VECTOR] = COERCION_SCALAR_TO_VECTOR,
            [PRIM_TYPE_FLOAT ][PRIM_TYPE_VECTOR] = COERCION_SCALAR_TO_VECTOR,
            [PRIM_TYPE_BOOL  ][PRIM_TYPE_POINT ] = COERCION_SCALAR_TO_POINT,
            [PRIM_TYPE_INT   ][PRIM_TYPE_POINT ] = COERCION_SCALAR_TO_POINT,
            [PRIM_TYPE_FLOAT ][PRIM_TYPE_POINT ] = COERCION_SCALAR_TO_POINT,
            [PRIM_TYPE_BOOL  ][PRIM_TYPE_NORMAL] = COERCION_SCALAR_TO_NORMAL,
            [PRIM_TYPE_INT   ][PRIM_TYPE_NORMAL] = COERCION_SCALAR_TO_NORMAL,
            [PRIM_TYPE_FLOAT ][PRIM_TYPE_NORMAL] = COERCION_SCALAR_TO_NORMAL,
            [PRIM_TYPE_COLOR ][PRIM_TYPE_VECTOR] = COERCION_COLOR_TO_VECTOR,
            [PRIM_TYPE_COLOR ][PRIM_TYPE_POINT ] = COERCION_COLOR_TO_POINT,
            [PRIM_TYPE_COLOR ][PRIM_TYPE_NORMAL] = COERCION_COLOR_TO_NORMAL,
            [PRIM_TYPE_NORMAL][PRIM_TYPE_COLOR ] = COERCION_SPATIAL_TO_COLOR,
            [PRIM_TYPE_POINT ][PRIM_TYPE_COLOR ] = COERCION_SPATIAL_TO_COLOR,
            [PRIM_TYPE_VECTOR][PRIM_TYPE_COLOR ] = COERCION_SPATIAL_TO_COLOR,
            [PRIM_TYPE_POINT ][PRIM_TYPE_VECTOR] = COERCION_SPATIAL_TO_VECTOR,
            [PRIM_TYPE_NORMAL][PRIM_TYPE_VECTOR] = COERCION_SPATIAL_TO_VECTOR,
            [PRIM_TYPE_VECTOR][PRIM_TYPE_NORMAL] = COERCION_SPATIAL_TO_NORMAL,
            [PRIM_TYPE_POINT ][PRIM_TYPE_NORMAL] = COERCION_SPATIAL_TO_NORMAL,
            [PRIM_TYPE_NORMAL][PRIM_TYPE_POINT ] = COERCION_SPATIAL_TO_POINT,
            [PRIM_TYPE_VECTOR][PRIM_TYPE_POINT ] = COERCION_SPATIAL_TO_POINT,
            [PRIM_TYPE_MATRIX][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_NORMAL][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_POINT ][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_VECTOR][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_COLOR ][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_STRING][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_FLOAT ][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_INT   ][PRIM_TYPE_BOOL  ] = COERCION_TO_BOOL,
            [PRIM_TYPE_INT   ][PRIM_TYPE_FLOAT ] = COERCION_TO_FLOAT,
            [PRIM_TYPE_BOOL  ][PRIM_TYPE_FLOAT ] = COERCION_TO_FLOAT,
            [PRIM_TYPE_BOOL  ][PRIM_TYPE_INT   ] = COERCION_TO_INT
        };
        return rank_matrix[from->prim_type][to_tag];
    }

    if (from->tag == TYPE_CLOSURE && to_tag == PRIM_TYPE_BOOL)
        return COERCION_TO_BOOL;

    if (from->tag == TYPE_COMPOUND) {
        if (!prim_type_tag_is_triple(to_tag) && to_tag != PRIM_TYPE_MATRIX)
            return COERCION_IMPOSSIBLE;
        if (prim_type_tag_component_count(to_tag) != from->compound_type.elem_count)
            return COERCION_IMPOSSIBLE;
        enum coercion_rank min_rank = COERCION_EXACT;
        for (size_t i = 0; i < from->compound_type.elem_count; ++i) {
            enum coercion_rank rank = type_coercion_rank_prim(from->compound_type.elem_types[i], PRIM_TYPE_FLOAT);
            min_rank = min_rank < rank ? min_rank : rank;
        }
        return min_rank;
    }

    return COERCION_IMPOSSIBLE;
}

enum coercion_rank type_coercion_rank(const struct type* from, const struct type* to) {
    if (from == to)
        return COERCION_EXACT;

    if (to->tag == TYPE_PRIM)
        return type_coercion_rank_prim(from, to->prim_type);

    if (from->tag == TYPE_ARRAY && to->tag == TYPE_ARRAY &&
        from->array_type.elem_type == to->array_type.elem_type &&
        (to->array_type.elem_count == 0 || (from->array_type.elem_count != 0 && from->array_type.elem_count <= to->array_type.elem_count)))
    {
        return COERCION_TO_ARRAY;
    }

    if (from->tag == TYPE_COMPOUND && to->tag == TYPE_STRUCT &&
        from->compound_type.elem_count <= to->struct_type.field_count)
    {
        enum coercion_rank min_rank =
            from->compound_type.elem_count == to->struct_type.field_count ? COERCION_EXACT : COERCION_ELLIPSIS;
        for (size_t i = 0; i < from->compound_type.elem_count; ++i) {
            enum coercion_rank rank = type_coercion_rank(
                from->compound_type.elem_types[i],
                to->struct_type.fields[i].type);
            min_rank = min_rank < rank ? min_rank : rank;
        }
        return min_rank;
    }

    if (from->tag == TYPE_COMPOUND && to->tag == TYPE_ARRAY &&
        (to->array_type.elem_count == 0 || from->compound_type.elem_count <= to->array_type.elem_count))
    {
        enum coercion_rank min_rank =
            from->compound_type.elem_count == to->array_type.elem_count ? COERCION_EXACT : COERCION_ELLIPSIS;
        for (size_t i = 0; i < from->compound_type.elem_count; ++i) {
            enum coercion_rank rank = type_coercion_rank(
                from->compound_type.elem_types[i],
                to->array_type.elem_type);
            min_rank = min_rank < rank ? min_rank : rank;
        }
        return min_rank;
    }

    return COERCION_IMPOSSIBLE;
}

bool type_coercion_is_lossy(const struct type* from, const struct type* to) {
    assert(type_is_coercible_to(from, to));
    return type_is_prim_type(from, PRIM_TYPE_INT) && type_is_prim_type(to, PRIM_TYPE_FLOAT);
}

bool type_coercion_is_incomplete(const struct type* from, const struct type* to) {
    assert(type_is_coercible_to(from, to));
    return
        from->tag == TYPE_COMPOUND && to->tag == TYPE_STRUCT &&
        from->compound_type.elem_count < to->struct_type.field_count;
}

bool type_is_unsized_array(const struct type* type) {
    return type->tag == TYPE_ARRAY && type->array_type.elem_count == 0;
}

bool type_is_prim_type(const struct type* type, enum prim_type_tag tag) {
    return type->tag == TYPE_PRIM && type->prim_type == tag;
}

bool type_is_void(const struct type* type) {
    return type_is_prim_type(type, PRIM_TYPE_VOID);
}

bool type_is_triple(const struct type* type) {
    return type->tag == TYPE_PRIM && prim_type_tag_is_triple(type->prim_type);
}

bool type_is_point_like(const struct type* type) {
    return type_is_triple(type) && type->prim_type != PRIM_TYPE_COLOR;
}

bool type_is_coercible_to(const struct type* from, const struct type* to) {
    return type_coercion_rank(from, to) != COERCION_IMPOSSIBLE;
}

bool type_is_castable_to(const struct type* from, const struct type* to) {
    if (type_is_coercible_to(from, to))
        return true;
    if (from->tag == TYPE_PRIM && to->tag == TYPE_PRIM) {
        if (type_is_triple(from) && type_is_triple(to))
            return true;
        if (to->prim_type == PRIM_TYPE_BOOL) {
            return
                from->prim_type == PRIM_TYPE_FLOAT ||
                from->prim_type == PRIM_TYPE_INT;
        }
        if (to->prim_type == PRIM_TYPE_INT)
            return from->prim_type == PRIM_TYPE_FLOAT;
    }
    return false;
}

size_t type_component_count(const struct type* type) {
    return type->tag == TYPE_PRIM ? prim_type_tag_component_count(type->prim_type) : 1;
}

const char* type_constructor_name(const struct type* type) {
    if (type->tag == TYPE_STRUCT)
        return type->struct_type.name;
    if (type->tag == TYPE_PRIM) {
        switch (type->prim_type) {
            case PRIM_TYPE_BOOL:   return "bool";
            case PRIM_TYPE_FLOAT:  return "float";
            case PRIM_TYPE_INT:    return "int";
            case PRIM_TYPE_COLOR:  return "color";
            case PRIM_TYPE_POINT:  return "point";
            case PRIM_TYPE_VECTOR: return "vector";
            case PRIM_TYPE_NORMAL: return "normal";
            case PRIM_TYPE_MATRIX: return "matrix";
            default:               return NULL;
        }
    }
    return NULL;
}

static void print(FILE* file, const struct type* type, const struct styles* styles) {
    switch (type->tag) {
        case TYPE_ERROR:
            fprintf(file, "%s<error>%s", styles->error, styles->reset);
            break;
        case TYPE_PRIM:
            fprintf(file, "%s%s%s", styles->keyword, prim_type_tag_to_string(type->prim_type), styles->reset);
            break;
        case TYPE_SHADER:
            fprintf(file, "%s%s%s", styles->keyword, shader_type_tag_to_string(type->shader_type), styles->reset);
            break;
        case TYPE_CLOSURE:
            fprintf(file, "%sclosure%s ", styles->keyword, styles->reset);
            print(file, type->closure_type.inner_type, styles);
            break;
        case TYPE_FUNC:
            print(file, type->func_type.ret_type, styles);
            fputs(" (", file);
            for (size_t i = 0; i < type->func_type.param_count; ++i) {
                if (type->func_type.params[i].is_output)
                    fprintf(file, "%soutput%s ", styles->keyword, styles->reset);
                print(file, type->func_type.params[i].type, styles);
                if (i != type->func_type.param_count - 1 || type->func_type.has_ellipsis)
                    fputs(", ", file);
            }
            if (type->func_type.has_ellipsis)
                fputs("...", file);
            fputs(")", file);
            break;
        case TYPE_COMPOUND:
            fputs("{ ", file);
            for (size_t i = 0; i < type->compound_type.elem_count; ++i) {
                print(file, type->compound_type.elem_types[i], styles);
                if (i != type->compound_type.elem_count - 1)
                    fputs(", ", file);
            }
            fputs(" }", file);
            break;
        case TYPE_STRUCT:
            fputs(type->struct_type.name, file);
            break;
        case TYPE_ARRAY:
            print(file, type->array_type.elem_type, styles);
            fputs("[", file);
            if (type->array_type.elem_count > 0)
                fprintf(file, "%zu", type->array_type.elem_count);
            fputs("]", file);
            break;
        default:
            assert(false && "invalid type");
            break;
    }
}

void type_print(FILE* file, const struct type* type, const struct type_print_options* options) {
    struct styles styles = {
        .reset   = options->disable_colors ? "" : TERM1(TERM_RESET),
        .keyword = options->disable_colors ? "" : TERM2(TERM_FG_BLUE, TERM_BOLD),
        .error   = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD),
    };
    print(file, type, &styles);
}

void type_dump(const struct type* type) {
    type_print(stdout, type, &(struct type_print_options) {
        .disable_colors = !is_term(stdout)
    });
    fputs("\n", stdout);
    fflush(stdout);
}

char* type_to_string(const struct type* type, const struct type_print_options* options) {
    struct mem_stream mem_stream;
    mem_stream_init(&mem_stream);
    type_print(mem_stream.file, type, options);
    mem_stream_destroy(&mem_stream);
    return mem_stream.buf;
}
