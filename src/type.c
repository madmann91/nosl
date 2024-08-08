#include "type.h"
#include "ast.h"

#include <overture/mem_stream.h>
#include <overture/term.h>

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

enum styles {
    STYLE_RESET,
    STYLE_ERROR,
    STYLE_KEYWORD,
    STYLE_COUNT
};

SMALL_VEC_IMPL(small_type_vec, const struct type*, PUBLIC)

bool type_tag_is_nominal(enum type_tag tag) {
    return tag == TYPE_STRUCT || tag == TYPE_FUNC;
}

bool type_is_unsized_array(const struct type* type) {
    return type->tag == TYPE_ARRAY && type->array_type.elem_count == 0;
}

bool type_is_nominal(const struct type* type) {
    return type_tag_is_nominal(type->tag);
}

bool type_is_prim_type(const struct type* type, enum prim_type_tag tag) {
    return type->tag == TYPE_PRIM && type->prim_type == tag;
}

bool type_is_void(const struct type* type) {
    return type_is_prim_type(type, PRIM_TYPE_VOID);
}

bool type_is_triple(const struct type* type) {
    if (type->tag != TYPE_PRIM)
        return false;
    return
        type->prim_type == PRIM_TYPE_COLOR ||
        type->prim_type == PRIM_TYPE_POINT ||
        type->prim_type == PRIM_TYPE_VECTOR ||
        type->prim_type == PRIM_TYPE_NORMAL;
}

bool type_is_implicitly_convertible_to(const struct type* from, const struct type* to) {
    if (from == to)
        return true;
    if (from->tag == TYPE_PRIM && to->tag == TYPE_PRIM) {
        if (to->prim_type == PRIM_TYPE_INT)
            return from->prim_type == PRIM_TYPE_BOOL;
        if (to->prim_type == PRIM_TYPE_FLOAT)
            return from->prim_type == PRIM_TYPE_BOOL || from->prim_type == PRIM_TYPE_INT;
        if (type_is_triple(to)) {
            return
                from->prim_type == PRIM_TYPE_BOOL ||
                from->prim_type == PRIM_TYPE_INT ||
                from->prim_type == PRIM_TYPE_FLOAT;
        }
    }
    if (from->tag == TYPE_ARRAY && to->tag == TYPE_ARRAY) {
        return
            from->array_type.elem_type == to->array_type.elem_type &&
            (from->array_type.elem_count == 0 || (from->array_type.elem_count <= to->array_type.elem_count));
    }
    return false;
}

bool type_is_explicitly_convertible_to(const struct type* from, const struct type* to) {
    if (type_is_implicitly_convertible_to(from, to))
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

static void print(FILE* file, const struct type* type, const char* styles[STYLE_COUNT]) {
    switch (type->tag) {
        case TYPE_ERROR:
            fprintf(file, "%s<error>%s", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            break;
        case TYPE_PRIM:
            fprintf(file, "%s%s%s", styles[STYLE_KEYWORD], prim_type_tag_to_string(type->prim_type), styles[STYLE_RESET]);
            break;
        case TYPE_SHADER:
            fprintf(file, "%s%s%s", styles[STYLE_KEYWORD], shader_type_tag_to_string(type->shader_type), styles[STYLE_RESET]);
            break;
        case TYPE_CLOSURE:
            fprintf(file, "%sclosure%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            print(file, type->closure_type.inner_type, styles);
            break;
        case TYPE_FUNC:
            print(file, type->func_type.ret_type, styles);
            fputs(" (", file);
            for (size_t i = 0; i < type->func_type.param_count; ++i) {
                print(file, type->func_type.params[i].type, styles);
                if (i != type->func_type.param_count)
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
    const char* styles[STYLE_COUNT] = {
        [STYLE_RESET]   = options->disable_colors ? "" : TERM1(TERM_RESET),
        [STYLE_KEYWORD] = options->disable_colors ? "" : TERM2(TERM_FG_BLUE, TERM_BOLD),
        [STYLE_ERROR]   = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD),
    };
    print(file, type, styles);
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
