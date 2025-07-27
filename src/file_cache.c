#include "file_cache.h"
#include "lexer.h"

#include <overture/file.h>
#include <overture/set.h>

#include <string.h>
#include <stdlib.h>
#include <assert.h>

static inline uint32_t hash_source_file(uint32_t h, struct source_file* const* source_file) {
    return hash_string(h, (*source_file)->file_name);
}

static inline bool is_same_source_file(struct source_file* const* source_file, struct source_file* const* other) {
    return !strcmp((*source_file)->file_name, (*other)->file_name);
}

SET_DEFINE(source_file_set, struct source_file*, hash_source_file, is_same_source_file, PRIVATE)

struct file_cache {
    struct source_file_set source_files;
};

static inline struct source_file* alloc_source_file(const char* file_name, char* file_data, size_t file_size) {
    struct source_file* source_file = xcalloc(1, sizeof(struct source_file));
    source_file->file_name = file_name;
    source_file->file_size = file_size;
    source_file->file_data = file_data;
    source_file->tokens = token_vec_create();
    struct lexer lexer = lexer_create(file_name, file_data, file_size);
    while (true) {
        struct token token = lexer_advance(&lexer);
        token_vec_push(&source_file->tokens, &token);
        if (token.tag == TOKEN_EOF)
            break;
    }
    return source_file;
}

static inline struct source_file* read_source_file(const char* file_name) {
    size_t file_size = 0;
    char* file_data = read_file(file_name, &file_size);
    if (!file_data)
        return NULL;

    file_name = strdup(file_name);
    return alloc_source_file(file_name, file_data, file_size);
}

static inline void free_source_file(struct source_file* source_file) {
    free((char*)source_file->file_name);
    free((char*)source_file->file_data);
    free((char*)source_file->include_guard);
    token_vec_destroy(&source_file->tokens);
    free(source_file);
}

struct file_cache* file_cache_create(void) {
    struct file_cache* cache = xmalloc(sizeof(struct file_cache));
    cache->source_files = source_file_set_create();
    return cache;
}

void file_cache_destroy(struct file_cache* cache) {
    SET_FOREACH(struct source_file*, source_file, cache->source_files) {
        free_source_file(*source_file);
    }
    source_file_set_destroy(&cache->source_files);
    free(cache);
}

struct source_file* file_cache_find(const struct file_cache* cache, const char* file_name) {
    struct source_file* source_file = &(struct source_file) { .file_name = file_name };
    struct source_file* const* existing_file = source_file_set_find(&cache->source_files, &source_file);
    return existing_file ? *existing_file : NULL;
}

struct source_file* file_cache_insert(struct file_cache* cache, const char* file_name) {
    struct source_file* existing_file = file_cache_find(cache, file_name);
    if (existing_file)
        return existing_file;

    struct source_file* source_file = read_source_file(file_name);
    if (!source_file)
        return NULL;

    [[maybe_unused]] bool was_inserted = source_file_set_insert(&cache->source_files, &source_file);
    assert(was_inserted);
    return source_file;
}

struct source_file* file_cache_insert_canonical(struct file_cache* file_cache, const char* file_name) {
    file_name = full_path(file_name);
    if (!file_name)
        return NULL;

    struct source_file* source_file = file_cache_insert(file_cache, file_name);
    free((char*)file_name);

    return source_file;
}
