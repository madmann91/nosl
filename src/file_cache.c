#include "file_cache.h"

#include <overture/set.h>
#include <overture/file.h>
#include <overture/mem_pool.h>
#include <overture/str_pool.h>

static uint32_t hash_cached_file(uint32_t h, struct cached_file* const* cached_file) {
    return hash_uint64(h, (uintptr_t)(*cached_file)->file_name);
}

static bool cached_file_is_equal(struct cached_file* const* cached_file, struct cached_file* const* other_cached_file) {
    return (*cached_file)->file_name == (*other_cached_file)->file_name;
}

SET_DEFINE(cached_file_set, struct cached_file*, hash_cached_file, cached_file_is_equal, PRIVATE)
VEC_DEFINE(line_vec, struct str_view, PRIVATE)

static struct str convert_tabs_to_spaces(const char* file_data, size_t file_size) {
    struct str str = str_create();
    for (size_t i = 0; i < file_size; ++i) {
        if (file_data[i] == '\t') {
            str_append(&str, STR_VIEW("    "));
            continue;
        }
        str_push(&str, file_data[i]);
    }
    str_terminate(&str);
    return str;
}

static struct line_vec extract_lines(const char* file_data, size_t file_size) {
    struct line_vec line_vec = line_vec_create();
    line_vec_push(&line_vec, &(struct str_view) { .data = file_data });
    for (size_t i = 0; i < file_size; ++i) {
        if (file_data[i] == '\n')
            line_vec_push(&line_vec, &(struct str_view) { .data = &file_data[i + 1] });
        else
            line_vec_last(&line_vec)->length++;
    }
    return line_vec;
}

static struct cached_file* alloc_cached_file(const char* file_name, const char* file_data, size_t file_size) {
    struct str cached_data = convert_tabs_to_spaces(file_data, file_size);
    struct line_vec lines = extract_lines(cached_data.data, cached_data.length);

    struct cached_file* cached_file = xcalloc(1, sizeof(struct cached_file));
    cached_file->file_name = file_name;
    cached_file->file_data = str_release(&cached_data);
    cached_file->line_count = lines.elem_count;
    cached_file->lines = line_vec_release(&lines);
    return cached_file;
}

static void free_cached_file(struct cached_file* cached_file) {
    free((char*)cached_file->file_data.data);
    free(cached_file->lines);
    free(cached_file);
}

struct file_cache {
    struct cached_file_set cached_files;
    struct mem_pool mem_pool;
    struct str_pool* str_pool;
};

struct file_cache* file_cache_create(void) {
    struct file_cache* file_cache = xcalloc(1, sizeof(struct file_cache));
    file_cache->cached_files = cached_file_set_create();
    file_cache->mem_pool = mem_pool_create();
    file_cache->str_pool = str_pool_create(&file_cache->mem_pool);
    return file_cache;
}

void file_cache_destroy(struct file_cache* file_cache) {
    SET_FOREACH(struct cached_file*, cached_file, file_cache->cached_files) {
        if (*cached_file)
            free_cached_file(*cached_file);
    }

    cached_file_set_destroy(&file_cache->cached_files);
    mem_pool_destroy(&file_cache->mem_pool);
    str_pool_destroy(file_cache->str_pool);
    free(file_cache);
}

void file_cache_reset(struct file_cache* file_cache) {
    SET_FOREACH(struct cached_file*, cached_file, file_cache->cached_files) {
        if (*cached_file) {
            (*cached_file)->has_pragma_once = false;
        }
    }
}

static const char* canonicalize_file_name(struct file_cache* file_cache, const char* file_name) {
    char* canonical_path = full_path(file_name);
    const char* unique_path = str_pool_insert(file_cache->str_pool, canonical_path ? canonical_path : file_name);
    free(canonical_path);
    return unique_path;
}

static struct cached_file* file_cache_find_internal(struct file_cache* file_cache, const char* file_name) {
    struct cached_file* cached_file = &(struct cached_file) { .file_name = file_name };
    struct cached_file* const* existing_file = cached_file_set_find(&file_cache->cached_files, &cached_file);
    return existing_file ? *existing_file : NULL;
}

struct cached_file* file_cache_find(struct file_cache* file_cache, const char* file_name) {
    const char* canonical_file_name = canonicalize_file_name(file_cache, file_name);
    return file_cache_find_internal(file_cache, canonical_file_name);
}

struct cached_file* file_cache_read(struct file_cache* file_cache, const char* file_name) {
    const char* canonical_file_name = canonicalize_file_name(file_cache, file_name);

    struct cached_file* cached_file = file_cache_find_internal(file_cache, canonical_file_name);
    if (cached_file)
        return cached_file;

    size_t file_size = 0;
    char* file_data = read_file(canonical_file_name, &file_size);
    cached_file = file_data ? alloc_cached_file(canonical_file_name, file_data, file_size) : NULL;
    free(file_data);

    [[maybe_unused]] bool was_inserted = cached_file_set_insert(&file_cache->cached_files, &cached_file);
    assert(was_inserted);
    return cached_file;
}
