/*
 * Copyright 2018-2019, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#ifdef STATS_ENABLED
#define STAT_ADD(ptr, add) util_fetch_and_add64(ptr, add)
#define STAT_SUB(ptr, add) util_fetch_and_sub64(ptr, add)
#else
#define STAT_ADD(ptr, add) do {} while (0)
#define STAT_SUB(ptr, add) do {} while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif
