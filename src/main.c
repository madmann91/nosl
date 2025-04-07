#include "parse.h"
#include "check.h"
#include "type_table.h"
#include "file_cache.h"
#include "preprocessor.h"
#include "ast.h"
#include "builtins.h"

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
    bool warns_as_errors;
    uint32_t max_warns;
    uint32_t max_errors;
};

static enum cli_state usage(void*, char*) {
    printf(
        "usage: noslc [options] files...\n"
        "options:\n"
        "  -h  --help               Shows this message.\n"
        "      --no-color           Disables colors in the output.\n"
        "      --warns-as-errors    Turns warnings into errors.\n"
        "      --max-errors <n>     Sets the maximum number of error messages to display.\n"
        "      --max-warns <n>      Sets the maximum number of warning messages to display.\n"
        "      --print-ast          Prints the AST on the standard output.\n");
    return CLI_STATE_ERROR;
}

static bool compile_file(
    const char* file_name,
    struct type_table* type_table,
    struct file_cache* file_cache,
    const struct builtins* builtins,
    const struct options* options)
{
    struct log log = {
        .file = stderr,
        .disable_colors = options->disable_colors || !is_term(stderr),
        .warns_as_errors = options->warns_as_errors,
        .max_warns = options->max_warns,
        .max_errors = options->max_errors,
        .print_line = log_print_line
    };

    struct preprocessor* preprocessor = preprocessor_open(&log, file_name, file_cache, &(struct preprocessor_config) {
        .include_paths = NULL,
        .include_path_count = 0
    });

    if (!preprocessor) {
        log_error(&log, NULL, "cannot open '%s'\n", file_name);
        return false;
    }

    struct mem_pool mem_pool = mem_pool_create();
    struct ast* program = parse(&mem_pool, preprocessor, &log);
    if (program) {
        check(&mem_pool, type_table, builtins, program, &log);

        if (options->print_ast) {
            ast_print(stdout, program, &(struct ast_print_options) {
                .disable_colors = options->disable_colors || !is_term(stdout)
            });
        }
    }

    mem_pool_destroy(&mem_pool);
    preprocessor_close(preprocessor);

    return log.error_count == 0;
}

static bool parse_options(int argc, char** argv, struct options* options) {
    struct cli_option cli_options[] = {
        { .short_name = "-h", .long_name = "--help", .parse = usage },
        cli_flag(NULL, "--no-color",        &options->disable_colors),
        cli_flag(NULL, "--warns-as-errors", &options->warns_as_errors),
        cli_flag(NULL, "--print-ast",       &options->print_ast),
        cli_option_uint32(NULL, "--max-errors", &options->max_errors),
        cli_option_uint32(NULL, "--max-warns", &options->max_warns),
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
        .max_errors = UINT32_MAX,
        .max_warns = UINT32_MAX
    };
    if (!parse_options(argc, argv, &options))
        return 1;

    struct mem_pool mem_pool = mem_pool_create();
    struct type_table* type_table = type_table_create(&mem_pool);
    struct builtins* builtins = builtins_create(type_table);
    struct file_cache* file_cache = file_cache_create();

    bool status = true;
    size_t file_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        status &= compile_file(argv[i], type_table, file_cache, builtins, &options);
        file_count++;
    }

    file_cache_destroy(file_cache);
    builtins_destroy(builtins);
    type_table_destroy(type_table);
    mem_pool_destroy(&mem_pool);

    if (file_count == 0) {
        fprintf(stderr, "no input files\n");
        return 1;
    }
    return status ? 0 : 1;
}
