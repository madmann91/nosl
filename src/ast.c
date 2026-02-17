#include "ast.h"

#include <overture/term.h>

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>

struct styles {
    const char* reset;
    const char* error;
    const char* keyword;
    const char* literal;
    const char* comment;
};

SMALL_VEC_IMPL(small_ast_vec, struct ast*, PUBLIC)
VEC_IMPL(ast_vec, struct ast*, PUBLIC)

static void print(FILE*, size_t, const struct ast*, const struct styles*);

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
#define x(name, str) case UNARY_EXPR_##name: return str;
        UNARY_EXPR_LIST(x)
#undef x
        default:
            assert(false && "invalid unary expression");
            return "";
    }
}

const char* binary_expr_tag_to_func_name(enum binary_expr_tag tag) {
    switch (binary_expr_tag_remove_assign(tag)) {
        case BINARY_EXPR_MUL:     return "__operator__mul__";
        case BINARY_EXPR_DIV:     return "__operator__div__";
        case BINARY_EXPR_REM:     return "__operator__mod__";
        case BINARY_EXPR_ADD:     return "__operator__add__";
        case BINARY_EXPR_SUB:     return "__operator__sub__";
        case BINARY_EXPR_LSHIFT:  return "__operator__shl__";
        case BINARY_EXPR_RSHIFT:  return "__operator__shr__";
        case BINARY_EXPR_CMP_LT:  return "__operator__lt__";
        case BINARY_EXPR_CMP_LE:  return "__operator__le__";
        case BINARY_EXPR_CMP_GT:  return "__operator__gt__";
        case BINARY_EXPR_CMP_GE:  return "__operator__ge__";
        case BINARY_EXPR_CMP_NE:  return "__operator__ne__";
        case BINARY_EXPR_CMP_EQ:  return "__operator__eq__";
        case BINARY_EXPR_BIT_AND: return "__operator__bitand__";
        case BINARY_EXPR_BIT_XOR: return "__operator__xor__";
        case BINARY_EXPR_BIT_OR:  return "__operator__bitor__";
        default:
            assert(false && "non-overloadable binary operator");
            return "";
    }
}

const char* unary_expr_tag_to_func_name(enum unary_expr_tag tag) {
    switch (tag) {
        case UNARY_EXPR_PRE_INC:  return "__operator__pre_inc__";
        case UNARY_EXPR_PRE_DEC:  return "__operator__pre_dec__";
        case UNARY_EXPR_POST_INC: return "__operator__post_inc__";
        case UNARY_EXPR_POST_DEC: return "__operator__post_dec__";
        case UNARY_EXPR_NEG:      return "__operator__neg__";
        case UNARY_EXPR_BIT_NOT:  return "__operator__compl__";
        case UNARY_EXPR_NOT:      return "__operator__not__";
        default:
            assert(false && "invalid unary expression");
            return "";
    }
}

enum binary_expr_tag binary_expr_tag_remove_assign(enum binary_expr_tag tag) {
    switch (tag) {
        case BINARY_EXPR_ASSIGN_MUL:     return BINARY_EXPR_MUL;
        case BINARY_EXPR_ASSIGN_DIV:     return BINARY_EXPR_DIV;
        case BINARY_EXPR_ASSIGN_REM:     return BINARY_EXPR_REM;
        case BINARY_EXPR_ASSIGN_ADD:     return BINARY_EXPR_ADD;
        case BINARY_EXPR_ASSIGN_SUB:     return BINARY_EXPR_SUB;
        case BINARY_EXPR_ASSIGN_LSHIFT:  return BINARY_EXPR_LSHIFT;
        case BINARY_EXPR_ASSIGN_RSHIFT:  return BINARY_EXPR_RSHIFT;
        case BINARY_EXPR_ASSIGN_BIT_AND: return BINARY_EXPR_BIT_AND;
        case BINARY_EXPR_ASSIGN_BIT_XOR: return BINARY_EXPR_BIT_XOR;
        case BINARY_EXPR_ASSIGN_BIT_OR:  return BINARY_EXPR_BIT_OR;
        default:                         return tag;
    }
}

bool binary_expr_tag_is_assign(enum binary_expr_tag tag) {
    switch (tag) {
#define x(name, ...) case BINARY_EXPR_##name:
        ASSIGN_EXPR_LIST(x)
#undef x
            return true;
        default:
            return false;
    }
}

bool binary_expr_tag_is_logic(enum binary_expr_tag tag) {
    return tag == BINARY_EXPR_LOGIC_AND || tag == BINARY_EXPR_LOGIC_OR;
}

bool unary_expr_tag_is_inc_or_dec(enum unary_expr_tag tag) {
    switch (tag) {
        case UNARY_EXPR_POST_INC:
        case UNARY_EXPR_POST_DEC:
        case UNARY_EXPR_PRE_INC:
        case UNARY_EXPR_PRE_DEC:
            return true;
        default:
            return false;
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
    const struct styles* styles)
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
    const struct styles* styles)
{
    fputs("(", file);
    print(file, indent, ast, styles);
    fputs(")", file);
}

static void print_dim(
    FILE* file,
    size_t indent,
    const struct ast* ast,
    const struct styles* styles)
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
    const struct styles* styles)
{
    print(file, indent, ast, styles);
    if (needs_semicolon(ast))
        fputs(";", file);
}

static void print(
    FILE* file,
    size_t indent,
    const struct ast* ast,
    const struct styles* styles)
{
    if (ast->attrs) {
        fprintf(file, "%s__attribute__%s", styles->keyword, styles->reset);
        print_many(file, indent, "((", ", ", ")) ", ast->attrs, styles);
    }

    switch (ast->tag) {
        case AST_ERROR:
            fprintf(file, "%s<error>%s", styles->error, styles->reset);
            break;
        case AST_ATTR:
            fputs(ast->attr.name, file);
            if (ast->attr.args)
                print_many(file, indent, "(", ", ", ")", ast->attr.args, styles);
            break;
        case AST_METADATUM:
            print(file, indent, ast->metadatum.type, styles);
            fprintf(file, " %s = ", ast->metadatum.name);
            print(file, indent, ast->metadatum.init, styles);
            break;
        case AST_PRIM_TYPE:
            if (ast->prim_type.is_closure)
                fprintf(file, "%sclosure%s ", styles->keyword, styles->reset);
            fprintf(file, "%s%s%s", styles->keyword, prim_type_tag_to_string(ast->prim_type.tag), styles->reset);
            break;
        case AST_SHADER_TYPE:
            fprintf(file, "%s%s%s", styles->keyword, shader_type_tag_to_string(ast->shader_type.tag), styles->reset);
            break;
        case AST_NAMED_TYPE:
            fprintf(file, "%s", ast->named_type.name);
            break;
        case AST_UNSIZED_DIM:
            break;
        case AST_BOOL_LITERAL:
            fprintf(file, "%s%s%s", styles->keyword, ast->bool_literal ? "true" : "false", styles->reset);
            break;
        case AST_INT_LITERAL:
            fprintf(file, "%s%"PRIuMAX"%s", styles->literal, ast->int_literal, styles->reset);
            break;
        case AST_FLOAT_LITERAL:
            fprintf(file, "%s%f%s", styles->literal, ast->float_literal, styles->reset);
            break;
        case AST_STRING_LITERAL:
            fprintf(file, "%s\"%s\"%s", styles->literal, ast->string_literal, styles->reset);
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
            print_many(file, indent, "(", ", ", ")", ast->func_decl.params, styles);
            if (ast->func_decl.body) {
                fputc(' ', file);
                print(file, indent, ast->func_decl.body, styles);
            } else {
                fputc(';', file);
            }
            break;
        case AST_STRUCT_DECL:
            fprintf(file, "%sstruct%s %s {", styles->keyword, styles->reset, ast->struct_decl.name);
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
            if (ast->param.is_ellipsis) {
                fputs("...", file);
                break;
            }
            if (ast->param.is_output)
                fprintf(file, "%soutput%s ", styles->keyword, styles->reset);
            print(file, indent, ast->param.type, styles);
            if (ast->param.name)
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
            print(file, indent, ast->binary_expr.args, styles);
            fprintf(file, " %s ", binary_expr_tag_to_string(ast->binary_expr.tag));
            print(file, indent, ast->binary_expr.args->next, styles);
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
            // Only print the type if this is an explicit cast
            if (ast->cast_expr.type)
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
            fprintf(file, "%swhile%s ", styles->keyword, styles->reset);
            print_paren(file, indent, ast->while_loop.cond, styles);
            fputs(" ", file);
            print_stmt(file, indent, ast->while_loop.body, styles);
            break;
        case AST_FOR_LOOP:
            fprintf(file, "%sfor%s (", styles->keyword, styles->reset);
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
            fprintf(file, "%sdo%s ", styles->keyword, styles->reset);
            print_stmt(file, indent, ast->do_while_loop.body, styles);
            fputs(" ", file);
            fprintf(file, "%swhile%s ", styles->keyword, styles->reset);
            print_paren(file, indent, ast->do_while_loop.cond, styles);
            fputs(";", file);
            break;
        case AST_IF_STMT:
            fprintf(file, "%sif%s ", styles->keyword, styles->reset);
            print_paren(file, indent, ast->if_stmt.cond, styles);
            fputs(" ", file);
            print_stmt(file, indent, ast->if_stmt.then_stmt, styles);
            if (ast->if_stmt.else_stmt) {
                fprintf(file, " %selse%s ", styles->keyword, styles->reset);
                print_stmt(file, indent, ast->if_stmt.else_stmt, styles);
            }
            break;
        case AST_BREAK_STMT:
            fprintf(file, "%sbreak%s;", styles->keyword, styles->reset);
            break;
        case AST_CONTINUE_STMT:
            fprintf(file, "%scontinue%s;", styles->keyword, styles->reset);
            break;
        case AST_RETURN_STMT:
            fprintf(file, "%sreturn%s", styles->keyword, styles->reset);
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
    struct styles styles = {
        .reset   = options->disable_colors ? "" : TERM1(TERM_RESET),
        .comment = options->disable_colors ? "" : TERM1(TERM_FG_GREEN),
        .keyword = options->disable_colors ? "" : TERM2(TERM_FG_BLUE, TERM_BOLD),
        .literal = options->disable_colors ? "" : TERM1(TERM_FG_CYAN),
        .error   = options->disable_colors ? "" : TERM2(TERM_FG_RED, TERM_BOLD),
    };
    for (const struct ast* decl = ast; decl; decl = decl->next) {
        print(file, options->indent, decl, &styles);
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

bool ast_is_mutable(const struct ast* ast) {
    switch (ast->tag) {
        case AST_PARAM:
            return ast->param.is_output;
        case AST_VAR:
            return true;
        case AST_IDENT_EXPR:
            return ast->ident_expr.symbol && ast_is_mutable(ast->ident_expr.symbol);
        case AST_INDEX_EXPR:
            return ast_is_mutable(ast->index_expr.value);
        case AST_PROJ_EXPR:
            return ast_is_mutable(ast->proj_expr.value);
        default:
            return false;
    }
}

bool ast_is_global_var(const struct ast* ast) {
    return ast->tag == AST_VAR && ast->var.is_global;
}

size_t ast_list_size(const struct ast* ast) {
    size_t size = 0;
    while (ast)
        size++, ast = ast->next;
    return size;
}

struct ast* ast_list_last(struct ast* ast) {
    while (ast->next)
        ast = ast->next;
    return ast;
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

struct ast* ast_skip_parens(struct ast* ast) {
    while (ast->tag == AST_PAREN_EXPR)
        ast = ast->paren_expr.inner_expr;
    return ast;
}

struct ast* ast_find_attr(struct ast* ast, const char* name) {
    for (struct ast* attr = ast->attrs; attr; attr = attr->next) {
        if (!strcmp(name, attr->attr.name))
            return attr;
    }
    return NULL;
}
