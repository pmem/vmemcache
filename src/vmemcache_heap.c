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
#include "sys_util.h"

#define IS_ALLOCATED 1
#define IS_FREE 0

/* flag: this extent is allocated */
#define FLAG_ALLOCATED ((uint64_t)1 << 63)

/* mask of all flags */
#define MASK_FLAGS (~FLAG_ALLOCATED)

/* do an arithmetic on the 'void' pointer */
#define VOID_PTR(ptr, byte_offset) ((void *)((uintptr_t)(ptr) + (byte_offset)))

#define SIZE_FLAGS(size, is_allocated) \
	(is_allocated) ? ((size) | FLAG_ALLOCATED) : (size)

struct heap {
	os_mutex_t lock;
	void *addr;
	size_t size;
	size_t extent_size;
	void *last_extent;

	/* statistics */
	stat_t size_used; /* current size of memory pool used for values */
	stat_t entries;   /* current number of heap entries */
};

/* heap entry ('struct extent' with header and footer ) */
struct heap_entry {
	void *ptr;
	size_t size;
};

struct header {
	void *prev;
	void *next;
	uint64_t size_flags;
};

struct footer {
	uint64_t size_flags;
};

#define HEADER_SIZE (sizeof(struct header))
#define FOOTER_SIZE (sizeof(struct footer))

/* size of a header and a footer */
#define HFER_SIZE (HEADER_SIZE + FOOTER_SIZE)

/* pointer to the header */
#define HEADER(ptr) ((struct header *)((uintptr_t)(ptr) - HEADER_SIZE))

/* pointer to the footer */
#define FOOTER(ptr, size) ((struct footer *)((uintptr_t)(ptr) + (size)))

/* size from the footer */
#define SIZE_FROM_FOOTER(ptr) (*(uint64_t *)((uintptr_t)ptr))

static void
vmcache_heap_merge(struct heap *heap, struct extent *ext,
			struct heap_entry *he);

/*
 * vmcache_extent_get_prev -- get the pointer to the previous extent
 */
void *
vmcache_extent_get_prev(void *ptr)
{
	ASSERTne(ptr, NULL);
	return HEADER(ptr)->prev;
}

/*
 * vmcache_extent_get_size -- get size of the extent
 */
size_t
vmcache_extent_get_size(void *ptr)
{
	ASSERTne(ptr, NULL);
	return HEADER(ptr)->size_flags & MASK_FLAGS;
}

/*
 * vmcache_insert_heap_entry -- insert the 'he' entry into the list of extents
 */
static inline int
vmcache_insert_heap_entry(struct heap *heap, struct heap_entry *he,
				void **last_extent, int is_allocated)
{
	struct header *header = he->ptr;
	struct footer *footer = VOID_PTR(he->ptr, he->size - FOOTER_SIZE);

	/* pointer and size of a new extent */
	void *new_extent = VOID_PTR(he->ptr, HEADER_SIZE);
	size_t size_flags =
		SIZE_FLAGS((he->size - HFER_SIZE), is_allocated);

	/* save the header */
	header->prev = *last_extent;
	header->next = NULL;
	header->size_flags = size_flags;

	/* save the footer */
	footer->size_flags = size_flags;

	if (*last_extent) {
		struct header *last_header = HEADER(*last_extent);
		ASSERTeq(last_header->next, NULL);
		last_header->next = new_extent;
	}

	*last_extent = new_extent;

	if (!is_allocated) {
		__sync_fetch_and_add(&heap->entries, 1);
	}

	return 0;
}

/*
 * vmcache_get_heap_entry -- get the free entry from the heap
 */
static inline int
vmcache_get_heap_entry(struct heap *heap, struct heap_entry *he)
{
	if (heap->last_extent == NULL)
		return -1;

	struct header *header = HEADER(heap->last_extent);
	ASSERTeq((header->size_flags & FLAG_ALLOCATED), 0); /* is free */
	ASSERTeq(header->next, NULL);

	struct footer *footer = FOOTER(heap->last_extent, header->size_flags);
	ASSERTeq(footer->size_flags, header->size_flags);

	he->ptr = header;
	he->size = header->size_flags + HFER_SIZE;

	if (header->prev) {
		struct header *prev_header = HEADER(header->prev);
		ASSERTne(prev_header->next, NULL);
		prev_header->next = NULL;
	}

	heap->last_extent = header->prev;

	__sync_fetch_and_sub(&heap->entries, 1);

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

	struct heap_entry whole_heap = {addr, size};
	vmcache_insert_heap_entry(heap,
				&whole_heap, &heap->last_extent, IS_FREE);

	return heap;
}

/*
 * vmcache_heap_destroy -- destroy vmemcache heap
 */
void
vmcache_heap_destroy(struct heap *heap)
{
	LOG(3, "heap %p", heap);

	util_mutex_destroy(&heap->lock);
	Free(heap);
}

/*
 * vmcache_free_first_extent -- (internal) free the first extent
 */
static int
vmcache_free_first_extent(struct heap *heap, struct extent *ext)
{
	ASSERTne(ext->ptr, NULL);

	struct header *first_header = HEADER(ext->ptr);
	ASSERTeq(first_header->prev, NULL);
	ASSERTne(first_header->next, NULL);

	void *second_extent = first_header->next;
	struct header *second_header = HEADER(second_extent);
	ASSERTeq(second_header->prev, ext->ptr);

	/* remove the first extent from the list */
	second_header->prev = NULL;

	/* free the first extent */
	struct heap_entry he;
	vmcache_heap_merge(heap, ext, &he);
	vmcache_insert_heap_entry(heap, &he, &heap->last_extent, IS_FREE);

	__sync_fetch_and_sub(&heap->size_used, ext->size);

	return 0;
}

/*
 * vmcache_alloc -- allocate memory (take it from the queue)
 *
 * It returns the number of allocated bytes if successful, otherwise -1.
 * The last extent of doubly-linked list of allocated extents is returned
 * in 'last_extent'.
 * 'first_extent' has to be zeroed in the beginning of a new allocation
 * (e.g. when *last_extent == NULL).
 */
ssize_t
vmcache_alloc(struct heap *heap, size_t size, void **last_extent,
		struct extent *first_extent)
{
	ASSERTne(last_extent, NULL);
	ASSERT((*last_extent == NULL) ?
		(first_extent->ptr == NULL && first_extent->size == 0) : 1);

	LOG(3, "heap %p size %zu last_extent %p first_extent->ptr %p",
			heap, size, *last_extent, first_extent->ptr);

	struct heap_entry he, f;
	size_t extent_size = heap->extent_size;
	size_t to_allocate = size;
	size_t allocated = 0;

	util_mutex_lock(&heap->lock);

	do {
		if (vmcache_get_heap_entry(heap, &he))
			break;

		size_t alloc_size = roundup(to_allocate + HFER_SIZE,
						extent_size);

		if (he.size >= alloc_size + extent_size) {
			f.ptr = VOID_PTR(he.ptr, alloc_size);
			f.size = he.size - alloc_size;

			vmcache_insert_heap_entry(heap,
					&f, &heap->last_extent, IS_FREE);

			he.size = alloc_size;
		}

		if (vmcache_insert_heap_entry(heap, &he, last_extent,
							IS_ALLOCATED)) {
			util_mutex_unlock(&heap->lock);
			return -1;
		}

		if (first_extent->ptr == NULL && to_allocate == size) {
			first_extent->ptr = *last_extent;
			first_extent->size = he.size - HFER_SIZE;
		}

		/* allocated size without headers */
		size_t allocated_size = he.size - HFER_SIZE;
		allocated += allocated_size;

		if (allocated_size > to_allocate &&
		    allocated_size - to_allocate >= extent_size - HFER_SIZE &&
		    first_extent->size == extent_size - HFER_SIZE) {
			vmcache_free_first_extent(heap, first_extent);
		}

		to_allocate -= MIN(allocated_size, to_allocate);

	} while (to_allocate > 0);

	__sync_fetch_and_add(&heap->size_used, allocated);

	util_mutex_unlock(&heap->lock);

	return (ssize_t)(size - to_allocate);
}

/*
 * vmcache_heap_remove -- (internal) remove an extent from the heap
 */
static void
vmcache_heap_remove(struct heap *heap, struct extent *ext)
{
	LOG(3, "heap %p ext %p", heap, ext);

	struct header *header = HEADER(ext->ptr);

	ASSERT(header->prev || header->next || (heap->last_extent == ext->ptr));

	if (header->prev) {
		struct header *header_prev = HEADER(header->prev);
		ASSERTeq(header_prev->next, ext->ptr);
		header_prev->next = header->next;
	}

	if (header->next) {
		struct header *header_next = HEADER(header->next);
		ASSERTeq(header_next->prev, ext->ptr);
		header_next->prev = header->prev;
	}

	if (heap->last_extent == ext->ptr)
		heap->last_extent = header->prev;

	__sync_fetch_and_sub(&heap->entries, 1);
}

/*
 * vmcache_heap_merge -- (internal) merge memory extents
 */
static void
vmcache_heap_merge(struct heap *heap, struct extent *ext,
			struct heap_entry *he)
{
	LOG(3, "heap %p ext %p", heap, ext);

	struct extent prev, next;

	he->ptr = VOID_PTR(ext->ptr, -HEADER_SIZE);
	he->size = ext->size + HFER_SIZE;

	/* merge with the previous one (lower address) */
	if ((uintptr_t)ext->ptr >= (uintptr_t)heap->addr + HFER_SIZE) {
		void *prev_footer = VOID_PTR(ext->ptr, -HFER_SIZE);
		prev.size = SIZE_FROM_FOOTER(prev_footer);
		if ((prev.size & FLAG_ALLOCATED) == 0) {
			prev.ptr = VOID_PTR(prev_footer, -prev.size);
			he->ptr = VOID_PTR(prev.ptr, -HEADER_SIZE);
			he->size += prev.size + HFER_SIZE;
			vmcache_heap_remove(heap, &prev);
		}
	}

	/* merge with the next one (higher address) */
	if ((uintptr_t)ext->ptr + ext->size + HFER_SIZE <
				(uintptr_t)heap->addr + heap->size) {
		next.ptr = VOID_PTR(ext->ptr, ext->size + HFER_SIZE);
		struct header *header_next = HEADER(next.ptr);
		next.size = header_next->size_flags;
		if ((next.size & FLAG_ALLOCATED) == 0) {
			he->size += next.size + HFER_SIZE;
			vmcache_heap_remove(heap, &next);
		}
	}
}

/*
 * vmcache_free -- free memory (give it back to the queue)
 */
void
vmcache_free(struct heap *heap, void *last_extent)
{
	LOG(3, "heap %p last_extent %p", heap, last_extent);

	util_mutex_lock(&heap->lock);

	size_t freed = 0;

	/*
	 * EXTENTS_FOREACH_SAFE variant is required here,
	 * because vmcache_insert_heap_entry() can modify
	 * the headers of the 'ext' extent.
	 */
	void *__prev;
	struct extent ext;
	EXTENTS_FOREACH_SAFE(ext, last_extent, __prev) {
		/* size without headers */
		freed += ext.size;

		struct heap_entry he;
		vmcache_heap_merge(heap, &ext, &he);
		vmcache_insert_heap_entry(heap,
					&he, &heap->last_extent, IS_FREE);
	}

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
	return heap->entries;
}
