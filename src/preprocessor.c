#include "preprocessor.h"
#include "lexer.h"

#include <overture/hash.h>
#include <overture/set.h>
#include <overture/mem.h>
#include <overture/file.h>

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

struct context {
    struct token (*next_token)(struct context*);
    void (*free)(struct context*);
    struct context* prev;
};

struct base_context {
    struct context context;
    struct lexer lexer;
};

struct source_file {
    const char* file_name;
    const char* file_data;
    size_t file_size;
    const char* include_guard;
    bool has_include_guard;
};

static inline char* canonicalize_file_name(const char* file_name) {
#if _XOPEN_SOURCE >= 500 || _DEFAULT_SOURCE || _BSD_SOURCE
    return realpath(file_name, NULL);
#elif WIN32
    return _fullpath(NULL, file_name, 0);
#else
    return NULL;
#endif
}

static inline uint32_t hash_source_file(uint32_t h, struct source_file* const* source_file) {
    return hash_string(h, (*source_file)->file_name);
}

static inline bool is_same_source_file(struct source_file* const* source_file, struct source_file* const* other) {
    return !strcmp((*source_file)->file_name, (*other)->file_name);
}

SET_DEFINE(source_file_set, struct source_file*, hash_source_file, is_same_source_file, PRIVATE)

struct preprocessor {
    struct log* log;
    struct context* context;
    struct source_file_set source_files;
    struct preprocessor_config config;
};

static struct token base_context_next_token(struct context* context) {
    return lexer_advance(&((struct base_context*)context)->lexer);
}

static void base_context_free(struct context* context) {
    free(context);
}

static inline void push_context(struct preprocessor* preprocessor, struct context* context) {
    context->prev = preprocessor->context;
    preprocessor->context = context;
}

static inline void pop_context(struct preprocessor* preprocessor) {
    assert(preprocessor->context);
    struct context* prev_context = preprocessor->context->prev;
    preprocessor->context->free(preprocessor->context);
    preprocessor->context = prev_context;
}

struct preprocessor* preprocessor_create(struct log* log, const struct preprocessor_config* config) {
    struct preprocessor* preprocessor = xmalloc(sizeof(struct preprocessor));
    preprocessor->log = log;
    preprocessor->context = NULL;
    preprocessor->source_files = source_file_set_create();
    preprocessor->config = *config;
    return preprocessor;
}

void preprocessor_destroy(struct preprocessor* preprocessor) {
    while (preprocessor->context)
        pop_context(preprocessor);
    SET_FOREACH(struct source_file*, source_file, preprocessor->source_files) {
        free((char*)(*source_file)->file_name);
        free((char*)(*source_file)->file_data);
        free((char*)(*source_file)->include_guard);
        free(*source_file);
    }
    source_file_set_destroy(&preprocessor->source_files);
    free(preprocessor);
}

static inline struct source_file* find_source_file(struct preprocessor* preprocessor, const char* file_name) {
    struct source_file* source_file = &(struct source_file) { .file_name = file_name };
    struct source_file* const* existing_file = source_file_set_find(&preprocessor->source_files, &source_file);
    return existing_file ? *existing_file : NULL;
}

static inline struct source_file* get_or_insert_source_file(struct preprocessor* preprocessor, const char* file_name) {
    const char* canonical_name = canonicalize_file_name(file_name);
    struct source_file* existing_file = find_source_file(preprocessor, canonical_name ? canonical_name : file_name);
    if (existing_file) {
        free((char*)canonical_name);
        return existing_file;
    }

    if (!file_exists(file_name))
        return NULL;

    struct source_file* source_file = xcalloc(1, sizeof(struct source_file));
    source_file->file_name = canonical_name ? canonical_name : strdup(file_name);
    source_file->file_data = file_read(file_name, &source_file->file_size);
    [[maybe_unused]] bool was_inserted = source_file_set_insert(&preprocessor->source_files, &source_file);
    assert(was_inserted);
    return source_file;
}

bool preprocessor_include(struct preprocessor* preprocessor, const char* file_name) {
    struct source_file* source_file = get_or_insert_source_file(preprocessor, file_name);
    if (!source_file)
        return false;

    struct base_context* base_context = xmalloc(sizeof(struct base_context));
    base_context->context.next_token  = base_context_next_token;
    base_context->context.free        = base_context_free;
    base_context->context.prev        = preprocessor->context;

    base_context->lexer = lexer_create(
        source_file->file_name,
        source_file->file_data,
        source_file->file_size,
        preprocessor->log);

    push_context(preprocessor, &base_context->context);
    return true;
}

struct token preprocessor_advance(struct preprocessor* preprocessor) {
    struct token token;
    do {
        token = preprocessor->context->next_token(preprocessor->context);
    } while (token.tag == TOKEN_NL);
    return token;
}

struct str_view preprocessor_view(struct preprocessor* preprocessor, const struct token* token) {
    struct source_file* source_file = find_source_file(preprocessor, token->loc.file_name);
    if (!source_file)
        return (struct str_view) {};
    return token_view(source_file->file_data, token);
}
