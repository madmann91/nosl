#include "parse.h"
#include "check.h"
#include "type_table.h"
#include "preprocessor.h"
#include "lexer.h"
#include "ast.h"

#include <overture/cli.h>
#include <overture/file.h>
#include <overture/mem_pool.h>
#include <overture/log.h>
#include <overture/term.h>
#include <overture/vec.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

VEC_DEFINE(raw_str_vec, char*, PRIVATE)

struct options {
    bool print_ast;
    bool disable_colors;
    bool disable_builtins;
    bool warns_as_errors;
    struct raw_str_vec include_dirs;
    uint32_t max_warns;
    uint32_t max_errors;
};

static struct options options_create() {
    return (struct options) {
        .print_ast = false,
        .disable_colors = false,
        .disable_builtins = false,
        .max_errors = UINT32_MAX,
        .max_warns = UINT32_MAX,
        .include_dirs = raw_str_vec_create()
    };
}

static void options_destroy(struct options* options) {
    raw_str_vec_destroy(&options->include_dirs);
    memset(options, 0, sizeof(struct options));
}

static enum cli_state usage(void*, char*) {
    printf(
        "usage: noslc [options] files...\n"
        "options:\n"
        "  -h  --help                      Shows this message.\n"
        "      --no-color                  Disables colors in the output.\n"
        "      --warns-as-errors           Turns warnings into errors.\n"
        "      --max-errors <n>            Sets the maximum number of error messages to display.\n"
        "      --max-warns <n>             Sets the maximum number of warning messages to display.\n"
        "      --no-builtins               Do not automatically include built-in functions and operators.\n"
        "      --print-ast                 Prints the AST on the standard output.\n"
        "  -I  --include-dir <directory>   Adds the given directory to the list of include directories.\n");
    return CLI_STATE_ERROR;
}

static inline enum cli_state cli_append_string(void* data, char* arg) {
    raw_str_vec_push((struct raw_str_vec*)data, &arg);
    return CLI_STATE_ACCEPTED;
}

static struct cli_option cli_option_multi_strings(
    const char* short_name,
    const char* long_name,
    struct raw_str_vec* strings)
{
    return (struct cli_option) {
        .short_name = short_name,
        .long_name = long_name,
        .data = (void*)strings,
        .parse = cli_append_string,
        .has_value = true
    };
}

static bool compile_file(
    const char* file_name,
    struct ast* builtins,
    struct type_table* type_table,
    const struct options* options)
{
    struct log log = {
        .file = stderr,
        .disable_colors = options->disable_colors || !is_term(stderr),
        .warns_as_errors = options->warns_as_errors,
        .max_warns = options->max_warns,
        .max_errors = options->max_errors
    };

    struct preprocessor* preprocessor = preprocessor_open(&log, file_name, (const char* const*)options->include_dirs.elems);
    if (!preprocessor) {
        log_error(&log, NULL, "cannot open '%s'\n", file_name);
        return false;
    }

    struct mem_pool mem_pool = mem_pool_create();
    struct ast* program = parse_with_preprocessor(&mem_pool, preprocessor, &log);

    // If builtins are available, prepend them to the program.
    struct ast* last_builtin = NULL;
    if (builtins) {
        last_builtin = ast_list_last(builtins);
        last_builtin->next = program;
        program = builtins;
    }

    if (program) {
        check(&mem_pool, type_table, program, &log);

        if (options->print_ast) {
            ast_print(stdout, program, &(struct ast_print_options) {
                .disable_colors = options->disable_colors || !is_term(stdout)
            });
        }
    }

    // Remove builtins from the program.
    if (last_builtin)
        last_builtin->next = NULL;

    mem_pool_destroy(&mem_pool);
    preprocessor_close(preprocessor);

    return log.error_count == 0;
}

static bool parse_options(int argc, char** argv, struct options* options) {
    struct cli_option cli_options[] = {
        { .short_name = "-h", .long_name = "--help", .parse = usage },
        cli_flag(NULL, "--no-color",        &options->disable_colors),
        cli_flag(NULL, "--no-builtins",     &options->disable_builtins),
        cli_flag(NULL, "--warns-as-errors", &options->warns_as_errors),
        cli_flag(NULL, "--print-ast",       &options->print_ast),
        cli_option_uint32(NULL, "--max-errors", &options->max_errors),
        cli_option_uint32(NULL, "--max-warns", &options->max_warns),
        cli_option_multi_strings("-I", "--include-dir", &options->include_dirs),
    };
    if (!cli_parse_options(argc, argv, cli_options, sizeof(cli_options) / sizeof(cli_options[0])))
        return false;
    if (options->max_errors < 2)
        options->max_errors = 2;
    raw_str_vec_push(&options->include_dirs, (char*[]) { NULL });
    return true;
}

static struct ast* parse_builtins(
    [[maybe_unused]] struct mem_pool* mem_pool,
    [[maybe_unused]] struct type_table* type_table)
{
#ifdef ENABLE_BUILTINS
    struct log log = {
        .file = NULL,
        .disable_colors = true,
        .max_warns = 1,
        .max_errors = 1
    };

    static const char builtins_data[] = {
#embed "builtins.osl.preprocessed"
        , 0
    };

    struct lexer lexer = lexer_create(STR_VIEW("builtins.osl"), STR_VIEW(builtins_data));
    struct ast* builtins = parse_with_lexer(mem_pool, &lexer, &log);
    check(mem_pool, type_table, builtins, &log);
    assert(log.error_count == 0 && log.warn_count == 0);
    return builtins;
#else
    return NULL;
#endif
}

int main(int argc, char** argv) {
    struct options options = options_create();
    if (!parse_options(argc, argv, &options)) {
        options_destroy(&options);
        return 1;
    }

    struct mem_pool mem_pool = mem_pool_create();
    struct type_table* type_table = type_table_create(&mem_pool);

    struct ast* builtins = NULL;
    if (!options.disable_builtins)
        builtins = parse_builtins(&mem_pool, type_table);

    bool status = true;
    size_t file_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        status &= compile_file(argv[i], builtins, type_table, &options);
        file_count++;
    }

    type_table_destroy(type_table);
    mem_pool_destroy(&mem_pool);
    options_destroy(&options);

    if (file_count == 0) {
        fprintf(stderr, "no input files\n");
        return 1;
    }
    return status ? 0 : 1;
}
