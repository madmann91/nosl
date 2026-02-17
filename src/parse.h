#pragma once

#include <stddef.h>

struct mem_pool;
struct lexer;
struct preprocessor;
struct log;
struct ast;

struct ast* parse_with_lexer(struct mem_pool*, struct lexer*, struct log*);
struct ast* parse_with_preprocessor(struct mem_pool*, struct preprocessor*, struct log*);
