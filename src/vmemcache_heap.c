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
 * vmemcache_heap.c -- implementation of simple vmemcache linear allocator
 */

#include "vmemcache_heap.h"
#include "vec.h"
#include "sys_util.h"
#include <malloc.h>

#define IS_ALLOCATED 1
#define IS_FREE 0

/* size of a header */
#define HEADER_SIZE (sizeof(size_t))

/* size of a footer */
#define FOOTER_SIZE (sizeof(size_t))

/* size of a header and a footer */
#define HFER_SIZE (HEADER_SIZE + FOOTER_SIZE)

/* flag: this extent is allocated */
#define FLAG_ALLOCATED ((size_t)1 << (8 * HEADER_SIZE - 1))

/* value of the header */
#define HEADER(ptr) (*(size_t *)((uintptr_t)(ptr)))

/* value of the footer */
#define FOOTER(ptr, size) (*(size_t *)((uintptr_t)(ptr) + (size) - FOOTER_SIZE))

/* value of the previous footer */
#define PREV_FOOTER(ptr) (*(size_t *)((uintptr_t)(ptr) - FOOTER_SIZE))

/* do an arithmetic on the 'void' pointer */
#define VOID_PTR(ptr, byte_offset) ((void *)((uintptr_t)(ptr) + (byte_offset)))

#define HEADER_VALUE(size, is_allocated) \
	((is_allocated) ? ((size) | FLAG_ALLOCATED) : (size))

struct heap {
	os_mutex_t lock;
	void *addr;
	size_t size;
	size_t extent_size;
	VEC(, struct heap_entry) entries;

	/* statistic - current size of memory pool used for values */
	stat_t size_used;
};

/*
 * vmcache_heap_write_headers -- write headers of the heap extent
 */
static inline void
vmcache_heap_write_headers(struct heap_entry *he, int is_allocated)
{
	size_t header_value = HEADER_VALUE(he->size, is_allocated);

	/* save the header */
	HEADER(he->ptr) = header_value;

	/* save the footer */
	FOOTER(he->ptr, he->size) = header_value;
}

/*
 * vmcache_heap_verify_headers -- verify headers of the heap extent
 */
static inline void
vmcache_heap_verify_headers(struct heap_entry *he, int is_allocated)
{
	size_t header_value = HEADER_VALUE(he->size, is_allocated);

	/* verify the header */
	ASSERTeq(HEADER(he->ptr), header_value);

	/* verify the footer */
	ASSERTeq(FOOTER(he->ptr, he->size), header_value);
}

/*
 * vmcache_heap_insert -- mark the heap entry as free and insert it to the heap
 */
static inline int
vmcache_heap_insert(struct heap *heap, struct heap_entry *he)
{
	vmcache_heap_write_headers(he, IS_FREE);

	if (VEC_PUSH_BACK(&heap->entries, *he)) {
		ERR("!cannot grow the heap's vector");
		return -1;
	}

	return 0;
}

/*
 * vmcache_heap_get -- get the free heap entry from the heap
 */
static inline int
vmcache_heap_get(struct heap *heap, struct heap_entry *he)
{
	if (VEC_POP_BACK(&heap->entries, he) != 0)
		return -1;

	vmcache_heap_verify_headers(he, IS_FREE);

	return 0;
}

/*
 * vmcache_heap_vec_insert -- mark the heap entry as allocated
 *                            and insert it to the vector of extents
 */
static inline int
vmcache_heap_vec_insert(struct extent_vec *vec, struct heap_entry *he)
{
	vmcache_heap_write_headers(he, IS_ALLOCATED);

	/* update pointer and size */
	he->ptr = VOID_PTR(he->ptr, HEADER_SIZE);
	he->size -= HFER_SIZE;

	if (VEC_PUSH_BACK(vec, *he) != 0) {
		ERR("!cannot grow extent vector");
		return -1;
	}

	return 0;
}

/*
 * vmcache_heap_create -- create vmemcache heap
 */
struct heap *
vmcache_heap_create(void *addr, size_t size, size_t extent_size)
{
	LOG(3, "addr %p size %zu", addr, size);

	struct heap *heap;

	heap = Zalloc(sizeof(struct heap));
	if (heap == NULL) {
		ERR("!Zalloc");
		return NULL;
	}

	util_mutex_init(&heap->lock);

	heap->addr = addr;
	heap->size = size;
	heap->extent_size = extent_size;
	VEC_INIT(&heap->entries);

	/* insert the whole_heap extent */
	struct heap_entry whole_heap = {addr, size};
	vmcache_heap_insert(heap, &whole_heap);

	return heap;
}

/*
 * vmcache_heap_destroy -- destroy vmemcache heap
 */
void
vmcache_heap_destroy(struct heap *heap)
{
	LOG(3, "heap %p", heap);

	VEC_DELETE(&heap->entries);
	util_mutex_destroy(&heap->lock);
	Free(heap);
}

/*
 * vmcache_alloc -- allocate memory (take it from the queue)
 *
 * This function returns the number of allocated bytes if successful,
 * otherwise -1 is returned.
 */
ssize_t
vmcache_alloc(struct heap *heap, size_t size, struct extent_vec *vec)
{
	LOG(3, "heap %p size %zu", heap, size);

	struct heap_entry he = {NULL, 0};
	size_t to_allocate = size;
	size_t allocated = 0;

	util_mutex_lock(&heap->lock);

	do {
		if (vmcache_heap_get(heap, &he))
			break;

		size_t alloc_size = roundup(to_allocate + HFER_SIZE,
						heap->extent_size);

		if (he.size >= alloc_size + heap->extent_size) {
			struct heap_entry f;
			f.ptr = VOID_PTR(he.ptr, alloc_size);
			f.size = he.size - alloc_size;

			vmcache_heap_insert(heap, &f);

			he.size = alloc_size;
		}

		if (vmcache_heap_vec_insert(vec, &he)) {
			util_mutex_unlock(&heap->lock);
			return -1;
		}

		/* size without headers */
		allocated += he.size;

		if (to_allocate <= he.size) {
			to_allocate = 0;
			break;
		}

		to_allocate -= he.size;

	} while (to_allocate > 0);

	__sync_fetch_and_add(&heap->size_used, allocated);

	util_mutex_unlock(&heap->lock);

	return (ssize_t)(size - to_allocate);
}

/*
 * vmcache_heap_merge -- (internal) merge memory extents
 */
static void
vmcache_heap_merge(struct heap *heap, struct heap_entry *he)
{
	LOG(3, "heap %p he %p", heap, he);

	struct heap_entry prev, next, *el;

	/* merge with the previous one (lower address) */
	if ((uintptr_t)he->ptr >= (uintptr_t)heap->addr + FOOTER_SIZE) {
		/* read size from the previous footer */
		prev.size = PREV_FOOTER(he->ptr);
		if ((prev.size & FLAG_ALLOCATED) == 0) {
			prev.ptr = VOID_PTR(he->ptr, -prev.size);

			VEC_FOREACH_BY_PTR(el, &heap->entries) {
				if (el->ptr == prev.ptr) {
					he->ptr = prev.ptr;
					he->size += prev.size;
					VEC_ERASE_BY_PTR(&heap->entries, el);
					break;
				}
			}
		}
	}

	/* merge with the next one (higher address) */
	if ((uintptr_t)he->ptr + he->size <
				(uintptr_t)heap->addr + heap->size) {
		next.ptr = VOID_PTR(he->ptr, he->size);
		next.size = HEADER(next.ptr);
		if ((next.size & FLAG_ALLOCATED) == 0) {
			VEC_FOREACH_BY_PTR(el, &heap->entries) {
				if (el->ptr == next.ptr) {
					he->size += next.size;
					VEC_ERASE_BY_PTR(&heap->entries, el);
					break;
				}
			}
		}
	}
}

/*
 * vmcache_free -- free memory (give it back to the queue)
 */
void
vmcache_free(struct heap *heap, struct extent_vec *vec)
{
	LOG(3, "heap %p vec %p", heap, vec);

	util_mutex_lock(&heap->lock);

	size_t freed = 0;

	struct heap_entry he = {NULL, 0};
	VEC_FOREACH(he, vec) {
		/* size without headers */
		freed += he.size;

		/* update pointer and size */
		he.ptr = VOID_PTR(he.ptr, -HEADER_SIZE);
		he.size += HFER_SIZE;

		vmcache_heap_verify_headers(&he, IS_ALLOCATED);
		vmcache_heap_merge(heap, &he);
		vmcache_heap_insert(heap, &he);
	}

	VEC_CLEAR(vec);

	__sync_fetch_and_sub(&heap->size_used, freed);

	util_mutex_unlock(&heap->lock);
}

/*
 * vmcache_get_heap_used_size -- get the 'size_used' statistic
 */
stat_t
vmcache_get_heap_used_size(struct heap *heap)
{
	return heap->size_used;
}

/*
 * vmcache_get_heap_entries_count -- get the 'heap_entries_count' statistic
 */
stat_t
vmcache_get_heap_entries_count(struct heap *heap)
{
	return (stat_t)VEC_SIZE(&heap->entries);
}

/*
 * vmcache_heap_internal_memory_usage -- estimate DRAM usage
 */
size_t
vmcache_heap_internal_memory_usage(struct heap *heap)
{
	util_mutex_lock(&heap->lock);

	size_t dram = malloc_usable_size(heap->entries.buffer);

	util_mutex_unlock(&heap->lock);

	return dram;
}
