// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

/*
 * vmemcache_heap.h -- internal definitions for vmemcache allocator module
 */

#ifndef VMEMCACHE_HEAP_H
#define VMEMCACHE_HEAP_H 1

#include <stddef.h>
#include <sys/types.h>

/* type of the statistics */
typedef unsigned long long stat_t;

#ifdef __cplusplus
extern "C" {
#endif

#define HEAP_ENTRY_IS_NULL(he) ((he.ptr) == NULL)

/* just for type safety - see 'ptr' field in 'struct extent' below */
struct ptr_ext;
typedef struct ptr_ext ptr_ext_t;

/* extent structure ('struct heap_entry' without header and footer ) */
struct extent {
	ptr_ext_t *ptr;
	size_t size;
};

struct heap;

struct heap *vmcache_heap_create(void *addr, size_t size, size_t extent_size);
void vmcache_heap_destroy(struct heap *heap);

ssize_t vmcache_alloc(struct heap *heap, size_t size,
			ptr_ext_t **first_extent,
			ptr_ext_t **small_extent);

void vmcache_free(struct heap *heap, ptr_ext_t *first_extent);

stat_t vmcache_get_heap_used_size(struct heap *heap);
stat_t vmcache_get_heap_entries_count(struct heap *heap);

ptr_ext_t *vmcache_extent_get_next(ptr_ext_t *ptr);
size_t vmcache_extent_get_size(ptr_ext_t *ptr);

/* unsafe variant - the headers of extents cannot be modified */
#define EXTENTS_FOREACH(ext, extents) \
	for ((ext).ptr = (extents), \
		(ext).size = vmcache_extent_get_size((ext).ptr); \
		(ext).ptr != NULL; \
		(ext).ptr = vmcache_extent_get_next((ext).ptr), \
		(ext).size = vmcache_extent_get_size((ext).ptr))

/* safe variant - the headers of extents can be modified (freed for example) */
#define EXTENTS_FOREACH_SAFE(ext, extents, __next) \
	for ((ext).ptr = (extents), \
		(ext).size = vmcache_extent_get_size((ext).ptr), \
		(__next) = vmcache_extent_get_next((ext).ptr); \
		(ext).ptr != NULL; \
		(ext).ptr = (__next), \
		(ext).size = vmcache_extent_get_size((ext).ptr), \
		(__next) = vmcache_extent_get_next((__next)))

#ifdef __cplusplus
}
#endif

#endif
