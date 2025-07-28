#pragma once

#include <overture/str.h>
#include <overture/log.h>
#include <overture/vec.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PRIM_TYPE_LIST(x) \
    x(BOOL,   "bool",   BOOL,   1) \
    x(FLOAT,  "float",  FLOAT,  1) \
    x(INT,    "int",    INT,    1) \
    x(COLOR,  "color",  FLOAT,  3) \
    x(POINT,  "point",  FLOAT,  3) \
    x(VECTOR, "vector", FLOAT,  3) \
    x(NORMAL, "normal", FLOAT,  3) \
    x(MATRIX, "matrix", FLOAT, 16) \
    x(STRING, "string", STRING, 1) \
    x(VOID,   "void",   VOID,   0)

#define SHADER_TYPE_LIST(x) \
    x(DISPLACEMENT, "displacement",) \
    x(SHADER, "shader",) \
    x(SURFACE, "surface",) \
    x(VOLUME, "volume",)

#define KEYWORD_LIST(x) \
    PRIM_TYPE_LIST(x) \
    SHADER_TYPE_LIST(x) \
    x(TRUE, "true",) \
    x(FALSE, "false",) \
    x(OUTPUT, "output",) \
    x(CLOSURE, "closure",) \
    x(STRUCT, "struct",) \
    x(IF, "if",) \
    x(ELSE, "else",) \
    x(FOR, "for",) \
    x(DO, "do",) \
    x(WHILE, "while",) \
    x(RETURN, "return",) \
    x(BREAK, "break",) \
    x(CONTINUE, "continue",) \
    x(ATTRIBUTE, "__attribute__",)

#define SYMBOL_LIST(x) \
    x(HASH, "#",) \
    x(CONCAT, "##",) \
    x(SEMICOLON, ";",) \
    x(COMMA, ",",) \
    x(DOT, ".",) \
    x(LPAREN, "(",) \
    x(RPAREN, ")",) \
    x(LBRACKET, "[",) \
    x(RBRACKET, "]",) \
    x(LMETA, "[[",) \
    x(RMETA, "]]",) \
    x(LBRACE, "{",) \
    x(RBRACE, "}",) \
    x(EQ, "=",) \
    x(CMP_EQ, "==",) \
    x(CMP_NE, "!=",) \
    x(CMP_GT, ">",) \
    x(CMP_GE, ">=",) \
    x(CMP_LT, "<",) \
    x(CMP_LE, "<=",) \
    x(INC, "++",) \
    x(DEC, "--",) \
    x(ADD, "+",) \
    x(SUB, "-",) \
    x(MUL, "*",) \
    x(DIV, "/",) \
    x(REM, "%",) \
    x(AND, "&",) \
    x(OR, "|",) \
    x(XOR, "^",) \
    x(NOT, "!",) \
    x(QUESTION, "?",) \
    x(COLON, ":",) \
    x(TILDE, "~",) \
    x(LOGIC_AND, "&&",) \
    x(LOGIC_OR, "||",) \
    x(LSHIFT, "<<",) \
    x(RSHIFT, ">>",) \
    x(ADD_EQ, "+=",) \
    x(SUB_EQ, "-=",) \
    x(MUL_EQ, "*=",) \
    x(DIV_EQ, "/=",) \
    x(REM_EQ, "%=",) \
    x(AND_EQ, "&=",) \
    x(OR_EQ, "|=",) \
    x(XOR_EQ, "^=",) \
    x(LSHIFT_EQ, "<<=",) \
    x(RSHIFT_EQ, ">>=",) \
    x(ELLIPSIS, "...",) \
    x(BACKSLASH, "\\")

#define TOKEN_LIST(x) \
    SYMBOL_LIST(x) \
    KEYWORD_LIST(x) \
    x(NL, "<new line>",) \
    x(EOF, "<end-of-file>",) \
    x(IDENT, "<identifier>",) \
    x(ERROR, "<invalid token>",) \
    x(INT_LITERAL, "<integer literal>",) \
    x(FLOAT_LITERAL, "<floating-point literal>",) \
    x(STRING_LITERAL, "<string literal>",) \
    x(MACRO_PARAM, "<macro parameter>",)

enum token_tag {
#define x(tag, ...) TOKEN_##tag,
    TOKEN_LIST(x)
#undef x
};

typedef uintmax_t int_literal;
typedef double float_literal;

enum token_error {
    TOKEN_ERROR_INVALID,
    TOKEN_ERROR_UNTERMINATED_COMMENT,
    TOKEN_ERROR_UNTERMINATED_STRING
};

struct token {
    enum token_tag tag;
    struct file_loc loc;
    union {
        int_literal int_literal;
        float_literal float_literal;
        enum token_error error;
        size_t macro_param_index;
    };
};

VEC_DECL(token_vec, struct token, PUBLIC)
SMALL_VEC_DECL(small_token_vec, struct token, PUBLIC)

[[nodiscard]] struct str_view token_view(const struct token*, const char* file_data);
[[nodiscard]] struct str_view token_string_literal_view(const struct token*, const char* file_data);

[[nodiscard]] const char* token_tag_to_string(enum token_tag);
[[nodiscard]] bool token_tag_is_symbol(enum token_tag);
[[nodiscard]] bool token_tag_is_keyword(enum token_tag);
