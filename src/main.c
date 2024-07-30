#include "parse.h"
#include "ast.h"

#include <overture/cli.h>
#include <overture/file.h>
#include <overture/mem_pool.h>
#include <overture/log.h>
#include <overture/term.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

struct options {
    bool print_ast;
    bool disable_colors;
    uint32_t max_errors;
};

static enum cli_state usage(void*, char*) {
    printf(
        "usage: noslc [options] files...\n"
        "options:\n"
        "  -h  --help               Shows this message.\n"
        "      --no-color           Disables colors in the output.\n"
        "      --max-errors <n>     Sets the maximum number of error messages.\n"
        "      --print-ast          Prints the AST on the standard output.\n");
    return CLI_STATE_ERROR;
}

static bool compile_file(const char* file_name, const struct options* options) {
    size_t file_size = 0;
    char* file_data = file_read(file_name, &file_size);
    if (!file_data) {
        fprintf(stderr, "cannot open '%s'\n", file_name);
        return false;
    }

    struct log log = {
        .file = stderr,
        .disable_colors = options->disable_colors || !is_term(stderr),
        .max_errors = options->max_errors,
        .source_name = file_name,
        .source_data = (struct str_view) { .data = file_data, .length = file_size }
    };
    struct mem_pool mem_pool = mem_pool_create();

    bool status = true;

    struct ast* program = parse(&mem_pool, file_data, file_size, &log);
    if (log.error_count != 0)
        goto error;

    if (options->print_ast) {
        ast_print(stdout, program, &(struct ast_print_options) {
            .disable_colors = options->disable_colors || !is_term(stdout)
        });
    }

    goto done;

error:
    status = false;
done:
    mem_pool_destroy(&mem_pool);
    free(file_data);
    return status;
}

static bool parse_options(int argc, char** argv, struct options* options) {
    struct cli_option cli_options[] = {
        { .short_name = "-h", .long_name = "--help", .parse = usage },
        cli_flag(NULL, "--no-color",   &options->disable_colors),
        cli_flag(NULL, "--print-ast",  &options->print_ast),
        cli_option_uint32(NULL, "--max-errors", &options->max_errors),
    };
    if (!cli_parse_options(argc, argv, cli_options, sizeof(cli_options) / sizeof(cli_options[0])))
        return false;
    if (options->max_errors < 2)
        options->max_errors = 2;
    return true;
}

int main(int argc, char** argv) {
    struct options options = {
        .print_ast = false,
        .disable_colors = false,
        .max_errors = UINT32_MAX
    };
    if (!parse_options(argc, argv, &options))
        return 1;

    bool status = true;
    size_t file_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        status &= compile_file(argv[i], &options);
        file_count++;
    }
    if (file_count == 0) {
        fprintf(stderr, "no input files\n");
        return 1;
    }
    return status ? 0 : 1;
}
