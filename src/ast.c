#include "ast.h"

#include <overture/term.h>

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

enum styles {
    STYLE_RESET,
    STYLE_ERROR,
    STYLE_KEYWORD,
    STYLE_LITERAL,
    STYLE_COMMENT,
    STYLE_COUNT
};

SMALL_VEC_IMPL(small_ast_vec, struct ast*, PUBLIC)

static void print(FILE*, size_t, const struct ast*, const char* [STYLE_COUNT]);

bool unary_expr_tag_is_postfix(enum unary_expr_tag tag) {
    return tag == UNARY_EXPR_POST_INC || tag == UNARY_EXPR_POST_DEC;
}

int binary_expr_tag_precedence(enum binary_expr_tag tag) {
    switch (tag) {
#define x(name, tok, str, prec) case BINARY_EXPR_##name: return prec;
        BINARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid binary expression");
            return 0;
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

const char* binary_expr_tag_to_string(enum binary_expr_tag tag) {
    switch (tag) {
#define x(name, tok, str, ...) case BINARY_EXPR_##name: return str;
        BINARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid binary expression");
            return "";
    }
}

const char* unary_expr_tag_to_string(enum unary_expr_tag tag) {
    switch (tag) {
#define x(name, str, ...) case UNARY_EXPR_##name: return str;
        UNARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid unary expression");
            return "";
    }
}

static inline bool needs_semicolon(const struct ast* stmt) {
    switch (stmt->tag) {
        case AST_VAR_DECL:
        case AST_FUNC_DECL:
        case AST_BLOCK:
        case AST_WHILE_LOOP:
        case AST_FOR_LOOP:
        case AST_DO_WHILE_LOOP:
        case AST_IF_STMT:
        case AST_BREAK_STMT:
        case AST_CONTINUE_STMT:
        case AST_RETURN_STMT:
        case AST_EMPTY_STMT:
            return false;
        default:
            return true;
    }
}

static void print_new_line(FILE* file, size_t indent) {
    fputs("\n", file);
    for (size_t i = 0; i < indent; ++i)
        fputs("    ", file);
}

static void print_many(
    FILE* file,
    size_t indent,
    const char* beg,
    const char* sep,
    const char* end,
    const struct ast* ast,
    const char* styles[STYLE_COUNT])
{
    fputs(beg, file);
    for (; ast; ast = ast->next) {
        print(file, indent, ast, styles);
        if (ast->next)
            fputs(sep, file);
    }
    fputs(end, file);
}

static void print_paren(
    FILE* file,
    size_t indent,
    const struct ast* ast,
    const char* styles[STYLE_COUNT])
{
    fputs("(", file);
    print(file, indent, ast, styles);
    fputs(")", file);
}

static void print_dim(
    FILE* file,
    size_t indent,
    const struct ast* ast,
    const char* styles[STYLE_COUNT])
{
    if (ast) {
        fputs("[", file);
        print(file, indent, ast, styles);
        fputs("]", file);
    }
}

static void print_stmt(
    FILE* file,
    size_t indent,
    const struct ast* ast,
    const char* styles[STYLE_COUNT])
{
    print(file, indent, ast, styles);
    if (needs_semicolon(ast))
        fputs(";", file);
}

static void print(
    FILE* file,
    size_t indent,
    const struct ast* ast,
    const char* styles[STYLE_COUNT])
{
    switch (ast->tag) {
        case AST_ERROR:
            fprintf(file, "%s<error>%s", styles[STYLE_ERROR], styles[STYLE_RESET]);
            break;
        case AST_METADATUM:
            print(file, indent, ast->metadatum.type, styles);
            fprintf(file, " %s = ", ast->metadatum.name);
            print(file, indent, ast->metadatum.init, styles);
            break;
        case AST_PRIM_TYPE:
            if (ast->prim_type.is_closure)
                fprintf(file, "%sclosure%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            fprintf(file, "%s%s%s", styles[STYLE_KEYWORD], prim_type_tag_to_string(ast->prim_type.tag), styles[STYLE_RESET]);
            break;
        case AST_SHADER_TYPE:
            fprintf(file, "%s%s%s", styles[STYLE_KEYWORD], shader_type_tag_to_string(ast->shader_type.tag), styles[STYLE_RESET]);
            break;
        case AST_NAMED_TYPE:
            fprintf(file, "%s", ast->named_type.name);
            break;
        case AST_BOOL_LITERAL:
            fprintf(file, "%s%s%s", styles[STYLE_KEYWORD], ast->bool_literal ? "true" : "false", styles[STYLE_RESET]);
            break;
        case AST_INT_LITERAL:
            fprintf(file, "%s%"PRIuMAX"%s", styles[STYLE_LITERAL], ast->int_literal, styles[STYLE_RESET]);
            break;
        case AST_FLOAT_LITERAL:
            fprintf(file, "%s%f%s", styles[STYLE_LITERAL], ast->float_literal, styles[STYLE_RESET]);
            break;
        case AST_STRING_LITERAL:
            fprintf(file, "%s\"%s\"%s", styles[STYLE_LITERAL], ast->string_literal, styles[STYLE_RESET]);
            break;
        case AST_SHADER_DECL:
            print(file, indent, ast->shader_decl.type, styles);
            fprintf(file, " %s", ast->shader_decl.name);
            if (ast->shader_decl.metadata)
                print_many(file, indent, " [[", ", ", "]]", ast->shader_decl.metadata, styles);
            print_many(file, indent, "(", ", ", ") ", ast->shader_decl.params, styles);
            print(file, indent, ast->shader_decl.body, styles);
            break;
        case AST_FUNC_DECL:
            print(file, indent, ast->func_decl.ret_type, styles);
            fprintf(file, " %s", ast->func_decl.name);
            print_many(file, indent, "(", ", ", ") ", ast->func_decl.params, styles);
            print(file, indent, ast->func_decl.body, styles);
            break;
        case AST_STRUCT_DECL:
            fprintf(file, "%sstruct%s %s {", styles[STYLE_KEYWORD], styles[STYLE_RESET], ast->struct_decl.name);
            for (struct ast* field = ast->struct_decl.fields; field; field = field->next) {
                print_new_line(file, indent + 1);
                print(file, indent + 1, field, styles);
            }
            if (ast->struct_decl.fields)
                print_new_line(file, indent);
            fputs("};", file);
            break;
        case AST_VAR_DECL:
            print(file, indent, ast->var_decl.type, styles);
            print_many(file, indent, " ", ", ", ";", ast->var_decl.vars, styles);
            break;
        case AST_VAR:
            fputs(ast->var.name, file);
            print_dim(file, indent, ast->var.dim, styles);
            if (ast->var.init) {
                fputs(" = ", file);
                print(file, indent, ast->var.init, styles);
            }
            break;
        case AST_PARAM:
            if (ast->param.is_output)
                fprintf(file, "%soutput%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            print(file, indent, ast->param.type, styles);
            fprintf(file, " %s", ast->param.name);
            print_dim(file, indent, ast->param.dim, styles);
            if (ast->param.init) {
                fputs(" = ", file);
                print(file, indent, ast->param.init, styles);
            }
            if (ast->param.metadata)
                print_many(file, indent, " [[", ", ", "]]", ast->param.metadata, styles);
            break;
        case AST_IDENT_EXPR:
            fputs(ast->ident_expr.name, file);
            break;
        case AST_BINARY_EXPR: {
            print(file, indent, ast->binary_expr.left, styles);
            fprintf(file, " %s ", binary_expr_tag_to_string(ast->binary_expr.tag));
            print(file, indent, ast->binary_expr.right, styles);
            break;
        }
        case AST_UNARY_EXPR: {
            bool is_postfix = unary_expr_tag_is_postfix(ast->unary_expr.tag);
            if (!is_postfix)
                fputs(unary_expr_tag_to_string(ast->unary_expr.tag), file);
            print(file, indent, ast->unary_expr.arg, styles);
            if (is_postfix)
                fputs(unary_expr_tag_to_string(ast->unary_expr.tag), file);
            break;
        }
        case AST_CALL_EXPR:
            print(file, indent, ast->call_expr.callee, styles);
            print_many(file, indent, "(", ", ", ")", ast->call_expr.args, styles);
            break;
        case AST_CONSTRUCT_EXPR:
            print(file, indent, ast->construct_expr.type, styles);
            print_many(file, indent, "(", ", ", ")", ast->construct_expr.args, styles);
            break;
        case AST_PAREN_EXPR:
            fputs("(", file);
            print(file, indent, ast->paren_expr.inner_expr, styles);
            fputs(")", file);
            break;
        case AST_COMPOUND_EXPR:
            print_many(file, indent, "", ", ", "", ast->compound_expr.elems, styles);
            break;
        case AST_COMPOUND_INIT:
            print_many(file, indent, "{", ", ", "}", ast->compound_init.elems, styles);
            break;
        case AST_TERNARY_EXPR:
            print(file, indent, ast->ternary_expr.cond, styles);
            fputs(" ? ", file);
            print(file, indent, ast->ternary_expr.then_expr, styles);
            fputs(" : ", file);
            print(file, indent, ast->ternary_expr.else_expr, styles);
            break;
        case AST_INDEX_EXPR:
            print(file, indent, ast->index_expr.value, styles);
            fputs("[", file);
            print(file, indent, ast->index_expr.index, styles);
            fputs("]", file);
            break;
        case AST_PROJ_EXPR:
            print(file, indent, ast->proj_expr.value, styles);
            fprintf(file, ".%s", ast->proj_expr.elem);
            break;
        case AST_CAST_EXPR:
            print_paren(file, indent, ast->cast_expr.type, styles);
            print(file, indent, ast->cast_expr.value, styles);
            break;
        case AST_BLOCK:
            fputs("{", file);
            for (struct ast* stmt = ast->block.stmts; stmt; stmt = stmt->next) {
                print_new_line(file, indent + 1);
                print_stmt(file, indent + 1, stmt, styles);
            }
            if (ast->block.stmts)
                print_new_line(file, indent);
            fputs("}", file);
            break;
        case AST_WHILE_LOOP:
            fprintf(file, "%swhile%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            print_paren(file, indent, ast->while_loop.cond, styles);
            fputs(" ", file);
            print_stmt(file, indent, ast->while_loop.body, styles);
            break;
        case AST_FOR_LOOP:
            fprintf(file, "%sfor%s (", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            if (ast->for_loop.init)
                print_stmt(file, indent, ast->for_loop.init, styles);
            else
                fputs(";", file);
            fputs(" ", file);
            if (ast->for_loop.cond)
                print(file, indent, ast->for_loop.cond, styles);
            fputs("; ", file);
            if (ast->for_loop.inc)
                print(file, indent, ast->for_loop.inc, styles);
            fputs(") ", file);
            print_stmt(file, indent, ast->for_loop.body, styles);
            break;
        case AST_DO_WHILE_LOOP:
            fprintf(file, "%sdo%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            print_stmt(file, indent, ast->do_while_loop.body, styles);
            fputs(" ", file);
            fprintf(file, "%swhile%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            print_paren(file, indent, ast->do_while_loop.cond, styles);
            fputs(";", file);
            break;
        case AST_IF_STMT:
            fprintf(file, "%sif%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            print_paren(file, indent, ast->if_stmt.cond, styles);
            fputs(" ", file);
            print_stmt(file, indent, ast->if_stmt.then_stmt, styles);
            if (ast->if_stmt.else_stmt) {
                fprintf(file, " %selse%s ", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
                print_stmt(file, indent, ast->if_stmt.else_stmt, styles);
            }
            break;
        case AST_BREAK_STMT:
            fprintf(file, "%sbreak%s;", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            break;
        case AST_CONTINUE_STMT:
            fprintf(file, "%scontinue%s;", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            break;
        case AST_RETURN_STMT:
            fprintf(file, "%sreturn%s", styles[STYLE_KEYWORD], styles[STYLE_RESET]);
            if (ast->return_stmt.value) {
                fputs(" ", file);
                print(file, indent, ast->return_stmt.value, styles);
            }
            fputs(";", file);
            break;
        case AST_EMPTY_STMT:
            fputs(";", file);
            break;
        default:
            assert(false && "invalid AST node");
            break;
    }
}

void ast_print(FILE* file, const struct ast* ast, const struct ast_print_options* options) {
    const char* styles[STYLE_COUNT] = {
        [STYLE_RESET]   = options->disable_colors ? "" : TERM1(TERM_RESET),
        [STYLE_COMMENT] = options->disable_colors ? "" : TERM1(TERM_FG_GREEN),
        [STYLE_KEYWORD] = options->disable_colors ? "" : TERM2(TERM_FG_BLUE, TERM_BOLD),
        [STYLE_LITERAL] = options->disable_colors ? "" : TERM1(TERM_FG_CYAN),
        [STYLE_ERROR]   = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD),
    };
    for (const struct ast* decl = ast; decl; decl = decl->next) {
        print(file, options->indent, decl, styles);
        print_new_line(file, options->indent);
    }
}

void ast_dump(const struct ast* ast) {
    ast_print(stdout, ast, &(struct ast_print_options) {
        .disable_colors = !is_term(stdout)
    });
    fputs("\n", stdout);
    fflush(stdout);
}

size_t ast_list_size(const struct ast* ast) {
    size_t size = 0;
    while (ast)
        size++, ast = ast->next;
    return size;
}

size_t ast_field_count(const struct ast* ast) {
    assert(ast->tag == AST_STRUCT_DECL);
    size_t field_count = 0;
    for (struct ast* field = ast->struct_decl.fields; field; field = field->next) {
        assert(field->tag == AST_VAR_DECL);
        field_count += ast_list_size(field->var_decl.vars);
    }
    return field_count;
}

const char* ast_decl_name(const struct ast* ast) {
    switch (ast->tag) {
        case AST_STRUCT_DECL: return ast->struct_decl.name;
        case AST_FUNC_DECL:   return ast->func_decl.name;
        case AST_SHADER_DECL: return ast->shader_decl.name;
        default:              return "";
    }
}
