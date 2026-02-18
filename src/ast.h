#pragma once

#include "type.h"

#include <overture/log.h>
#include <overture/vec.h>

#include <stddef.h>
#include <stdint.h>

#define UNARY_EXPR_LIST(x) \
    x(PRE_INC,  "++") \
    x(PRE_DEC,  "--") \
    x(POST_INC, "++") \
    x(POST_DEC, "--") \
    x(BIT_NOT,  "~") \
    x(NEG,      "-") \
    x(NOT,      "!")

#define ASSIGN_EXPR_LIST(x) \
    x(ASSIGN,         EQ,        "=",   10) \
    x(ASSIGN_MUL,     MUL_EQ,    "*=",  10) \
    x(ASSIGN_DIV,     DIV_EQ,    "/=",  10) \
    x(ASSIGN_REM,     REM_EQ,    "%=",  10) \
    x(ASSIGN_ADD,     ADD_EQ,    "+=",  10) \
    x(ASSIGN_SUB,     SUB_EQ,    "-=",  10) \
    x(ASSIGN_LSHIFT,  LSHIFT_EQ, "<<=", 10) \
    x(ASSIGN_RSHIFT,  RSHIFT_EQ, ">>=", 10) \
    x(ASSIGN_BIT_AND, AND_EQ,    "&=",  10) \
    x(ASSIGN_BIT_XOR, XOR_EQ,    "^=",  10) \
    x(ASSIGN_BIT_OR,  OR_EQ,     "|=",  10)

#define ARITH_EXPR_LIST(x) \
    x(MUL,            MUL,       "*",    1) \
    x(DIV,            DIV,       "/",    1) \
    x(REM,            REM,       "%",    1) \
    x(ADD,            ADD,       "+",    2) \
    x(SUB,            SUB,       "-",    2)

#define SHIFT_EXPR_LIST(x) \
    x(LSHIFT,         LSHIFT,    "<<",   3) \
    x(RSHIFT,         RSHIFT,    ">>",   3)

#define CMP_EXPR_LIST(x) \
    x(CMP_LT,         CMP_LT,    "<",    4) \
    x(CMP_LE,         CMP_LE,    "<=",   4) \
    x(CMP_GT,         CMP_GT,    ">",    4) \
    x(CMP_GE,         CMP_GE,    ">=",   4) \
    x(CMP_NE,         CMP_NE,    "!=",   4) \
    x(CMP_EQ,         CMP_EQ,    "==",   4)

#define BIT_EXPR_LIST(x) \
    x(BIT_AND,        AND,       "&",    5) \
    x(BIT_XOR,        XOR,       "^",    6) \
    x(BIT_OR,         OR,        "|",    7)

#define LOGIC_EXPR_LIST(x) \
    x(LOGIC_AND,      LOGIC_AND, "&&",   8) \
    x(LOGIC_OR,       LOGIC_OR,  "||",   9)

#define BINARY_EXPR_LIST(x) \
    ASSIGN_EXPR_LIST(x) \
    ARITH_EXPR_LIST(x) \
    SHIFT_EXPR_LIST(x) \
    CMP_EXPR_LIST(x) \
    BIT_EXPR_LIST(x) \
    LOGIC_EXPR_LIST(x)

enum unary_expr_tag {
#define x(name, ...) UNARY_EXPR_##name,
    UNARY_EXPR_LIST(x)
#undef x
};

enum binary_expr_tag {
#define x(name, ...) BINARY_EXPR_##name,
    BINARY_EXPR_LIST(x)
#undef x
};

enum ast_tag {
    AST_ERROR,
    AST_METADATUM,
    AST_ATTR,

    // Types
    AST_PRIM_TYPE,
    AST_SHADER_TYPE,
    AST_NAMED_TYPE,

    // Literals
    AST_BOOL_LITERAL,
    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_STRING_LITERAL,
    AST_UNSIZED_DIM,

    // Declarations
    AST_SHADER_DECL,
    AST_STRUCT_DECL,
    AST_FUNC_DECL,
    AST_VAR_DECL,
    AST_VAR,
    AST_PARAM,

    // Expressions
    AST_IDENT_EXPR,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_CALL_EXPR,
    AST_CONSTRUCT_EXPR,
    AST_PAREN_EXPR,
    AST_COMPOUND_EXPR,
    AST_COMPOUND_INIT,
    AST_TERNARY_EXPR,
    AST_INDEX_EXPR,
    AST_PROJ_EXPR,
    AST_CAST_EXPR,

    // Statements
    AST_BLOCK,
    AST_WHILE_LOOP,
    AST_FOR_LOOP,
    AST_DO_WHILE_LOOP,
    AST_IF_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_RETURN_STMT,
    AST_EMPTY_STMT
};

struct type;

struct ast {
    enum ast_tag tag;
    const struct type* type;
    struct file_loc loc;
    struct ast* next;
    struct ast* attrs;
    union {
        bool bool_literal;
        int_literal int_literal;
        float_literal float_literal;
        const char* string_literal;
        struct {
            const char* name;
            struct ast* args;
        } attr;
        struct {
            bool is_closure;
            enum prim_type_tag tag;
        } prim_type;
        struct {
            enum shader_type_tag tag;
        } shader_type;
        struct {
            const char* name;
            struct ast* symbol;
        } ident_expr, named_type;
        struct {
            const char* name;
            struct ast* fields;
            const struct type* constructor_type;
        } struct_decl;
        struct {
            struct ast* ret_type;
            const char* name;
            struct ast* params;
            struct ast* body;
        } func_decl;
        struct {
            struct ast* type;
            const char* name;
            struct ast* params;
            struct ast* body;
            struct ast* metadata;
        } shader_decl;
        struct {
            bool is_output : 1;
            bool is_ellipsis : 1;
            struct ast* type;
            const char* name;
            struct ast* dim;
            struct ast* init;
            struct ast* metadata;
        } param;
        struct {
            struct ast* type;
            const char* name;
            struct ast* init;
        } metadatum;
        struct {
            const char* name;
            struct ast* dim;
            struct ast* init;
            bool is_global;
        } var;
        struct {
            struct ast* type;
            struct ast* vars;
        } var_decl;
        struct {
            enum binary_expr_tag tag;
            struct ast* args;
            struct ast* symbol;
        } binary_expr;
        struct {
            enum unary_expr_tag tag;
            struct ast* arg;
            struct ast* symbol;
        } unary_expr;
        struct {
            struct ast* callee;
            struct ast* args;
        } call_expr;
        struct {
            struct ast* type;
            struct ast* args;
        } construct_expr;
        struct {
            struct ast* elems;
        } compound_expr;
        struct {
            struct ast* elems;
            struct ast* symbol;
        } compound_init;
        struct {
            struct ast* inner_expr;
        } paren_expr;
        struct {
            struct ast* type;
            struct ast* value;
        } cast_expr;
        struct {
            struct ast* value;
            struct ast* index;
        } index_expr;
        struct {
            struct ast* value;
            const char* elem;
            size_t index;
        } proj_expr;
        struct {
            struct ast* cond;
            struct ast* then_expr;
            struct ast* else_expr;
        } ternary_expr;
        struct {
            struct ast* stmts;
        } block;
        struct {
            struct ast* cond;
            struct ast* body;
        } while_loop, do_while_loop;
        struct {
            struct ast* cond;
            struct ast* init;
            struct ast* inc;
            struct ast* body;
        } for_loop;
        struct {
            struct ast* cond;
            struct ast* then_stmt;
            struct ast* else_stmt;
        } if_stmt;
        struct {
            struct ast* value;
            struct ast* shader_or_func;
        } return_stmt;
        struct {
            struct ast* loop;
        } continue_stmt, break_stmt;
    };
};

struct ast_print_options {
    bool disable_colors;
    bool only_first;
    size_t indent;
};

VEC_DECL(ast_vec, struct ast*, PUBLIC)
SMALL_VEC_DECL(small_ast_vec, struct ast*, PUBLIC)

[[nodiscard]] bool unary_expr_tag_is_postfix(enum unary_expr_tag);
[[nodiscard]] int binary_expr_tag_precedence(enum binary_expr_tag);
[[nodiscard]] const char* binary_expr_tag_to_string(enum binary_expr_tag);
[[nodiscard]] const char* unary_expr_tag_to_string(enum unary_expr_tag);
[[nodiscard]] const char* binary_expr_tag_to_func_name(enum binary_expr_tag);
[[nodiscard]] const char* unary_expr_tag_to_func_name(enum unary_expr_tag);
[[nodiscard]] enum binary_expr_tag binary_expr_tag_remove_assign(enum binary_expr_tag);
[[nodiscard]] bool binary_expr_tag_is_assign(enum binary_expr_tag);
[[nodiscard]] bool binary_expr_tag_is_logic(enum binary_expr_tag);
[[nodiscard]] bool unary_expr_tag_is_inc_or_dec(enum unary_expr_tag);

void ast_print(FILE*, const struct ast*, const struct ast_print_options*);
void ast_dump(const struct ast*);

[[nodiscard]] bool ast_is_mutable(const struct ast*);
[[nodiscard]] bool ast_is_global_var(const struct ast*);
[[nodiscard]] size_t ast_list_size(const struct ast*);
[[nodiscard]] struct ast* ast_list_last(struct ast*);
[[nodiscard]] size_t ast_field_count(const struct ast*);
[[nodiscard]] const char* ast_decl_name(const struct ast*);
[[nodiscard]] struct ast* ast_skip_parens(struct ast*);
[[nodiscard]] struct ast* ast_find_attr(struct ast*, const char* name);
