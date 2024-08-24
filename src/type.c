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

enum coercion_rank type_coercion_rank(const struct type* from, const struct type* to) {
    if (from == to)
        return COERCION_EXACT;
    if (from->tag == TYPE_PRIM && to->tag == TYPE_PRIM) {
        if (to->prim_type == PRIM_TYPE_INT && from->prim_type == PRIM_TYPE_BOOL)
            return COERCION_BOOL_TO_INT;
        if (to->prim_type == PRIM_TYPE_FLOAT) {
            if (from->prim_type == PRIM_TYPE_BOOL)
                return COERCION_BOOL_TO_FLOAT;
            if (from->prim_type == PRIM_TYPE_INT)
                return COERCION_INT_TO_FLOAT;
        }
        if (type_is_triple(to) && type_is_triple(from)) {
            if (type_is_point_like(to) && type_is_point_like(from))
                return COERCION_POINT_LIKE;
            return COERCION_TRIPLE;
        }
        if (from->prim_type == PRIM_TYPE_BOOL ||
            from->prim_type == PRIM_TYPE_INT ||
            from->prim_type == PRIM_TYPE_FLOAT)
        {
            if (type_is_triple(to))
                return COERCION_SCALAR_TO_TRIPLE;
            if (to->prim_type == PRIM_TYPE_MATRIX)
                return COERCION_SCALAR_TO_MATRIX;
        }
    }
    if (from->tag == TYPE_ARRAY && to->tag == TYPE_ARRAY) {
        if (from->array_type.elem_type == to->array_type.elem_type &&
            (from->array_type.elem_count == 0 || (from->array_type.elem_count <= to->array_type.elem_count)))
            return COERCION_ARRAY;
    }
    return COERCION_IMPOSSIBLE;
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
    return
        type->prim_type == PRIM_TYPE_COLOR ||
        type->prim_type == PRIM_TYPE_POINT ||
        type->prim_type == PRIM_TYPE_VECTOR ||
        type->prim_type == PRIM_TYPE_NORMAL;
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

bool type_has_same_param_and_ret_types(const struct type* type, const struct type* other) {
    assert(type->tag == TYPE_FUNC);
    assert(other->tag == TYPE_FUNC);
    if (type->func_type.param_count != other->func_type.param_count ||
        type->func_type.ret_type != other->func_type.ret_type)
        return false;
    for (size_t i = 0, param_count = type->func_type.param_count; i < param_count; ++i) {
        if (type->func_type.params[i].type != other->func_type.params[i].type ||
            type->func_type.params[i].is_output != other->func_type.params[i].is_output)
            return false;
    }
    return true;
}

size_t type_component_count(const struct type* type) {
    if (type->tag != TYPE_PRIM)
        return 1;
    switch (type->prim_type) {
#define x(name, str, type, comp) case PRIM_TYPE_##name: return comp;
        PRIM_TYPE_LIST(x)
#undef x
        default:
            return 1;
    }
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
                if (i != type->func_type.param_count - 1)
                    fputs(", ", file);
            }
            fputs(")", file);
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
