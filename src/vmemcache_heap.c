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

#define GUARD_SIZE ((uintptr_t)0x40) /* 64 bytes */

#define IS_ALLOCATED 1
#define IS_FREE 0

/* flag: this extent is allocated */
#define FLAG_ALLOCATED ((sizeof(void *) > 4) ? (1ULL << 63) : (1UL << 31))

/* mask of all flags */
#define MASK_FLAGS (~FLAG_ALLOCATED)

#define SIZE_FLAGS(size, is_allocated) \
	(is_allocated) ? ((size) | FLAG_ALLOCATED) : (size)

struct heap {
	os_mutex_t lock;
	size_t extent_size;
	ptr_ext_t *first_extent;

	/* statistics */
	stat_t size_used; /* current size of memory pool used for values */
	stat_t entries;   /* current number of heap entries */
};

struct header {
	ptr_ext_t *next;
	ptr_ext_t *prev;
	uint64_t size_flags;
};

struct footer {
	uint64_t size_flags;
};

/* heap entry ('struct extent' with header and footer ) */
struct heap_entry {
	struct header *ptr;
	size_t size;
};

#define HEADER_SIZE (sizeof(struct header))
#define FOOTER_SIZE (sizeof(struct footer))

/* size of a header and a footer */
#define HFER_SIZE (HEADER_SIZE + FOOTER_SIZE)

static void
vmcache_heap_merge(struct heap *heap, struct extent *ext,
			struct heap_entry *he);

/*
 * vmcache_new_heap_entry -- create a new heap entry
 */
static inline struct header *
vmcache_new_heap_entry(struct header *ptr, size_t alloc_size)
{
	return (struct header *)((uintptr_t)ptr + alloc_size);
}

/*
 * vmcache_create_footer_address -- create a footer address for the heap entry
 */
static inline struct footer *
vmcache_create_footer_address(struct heap_entry *he)
{
	return (struct footer *)((uintptr_t)he->ptr + he->size - FOOTER_SIZE);
}

/*
 * vmcache_create_ext_ptr -- create an extent pointer for the heap entry
 */
static inline ptr_ext_t *
vmcache_create_ext_ptr(struct heap_entry *he)
{
	return (ptr_ext_t *)((uintptr_t)he->ptr + HEADER_SIZE);
}

/*
 * vmcache_extent_get_header -- get the header of the extent
 */
static inline struct header *
vmcache_extent_get_header(ptr_ext_t *ptr)
{
	return (struct header *)((uintptr_t)ptr - HEADER_SIZE);
}

/*
 * vmcache_extent_get_footer -- get the footer of the extent
 */
static inline struct footer *
vmcache_extent_get_footer(ptr_ext_t *ptr)
{
	struct header *header = vmcache_extent_get_header(ptr);
	size_t size = header->size_flags & MASK_FLAGS;
	return (struct footer *)((uintptr_t)ptr + size);
}

/*
 * vmcache_extent_get_next -- get the pointer to the next extent
 */
ptr_ext_t *
vmcache_extent_get_next(ptr_ext_t *ptr)
{
	if (ptr == NULL)
		return NULL;

	return vmcache_extent_get_header(ptr)->next;
}

/*
 * vmcache_extent_get_size -- get size of the extent
 */
size_t
vmcache_extent_get_size(ptr_ext_t *ptr)
{
	if (ptr == NULL)
		return 0;

	return vmcache_extent_get_header(ptr)->size_flags & MASK_FLAGS;
}

/*
 * vmcache_get_prev_footer -- get the address of the footer
 *                            of the previous extent
 */
static inline struct footer *
vmcache_get_prev_footer(struct extent *ext)
{
	return (struct footer *)((uintptr_t)ext->ptr - HFER_SIZE);
}

/*
 * vmcache_get_next_ptr_ext -- get the pointer to the next extent
 */
static inline ptr_ext_t *
vmcache_get_next_ptr_ext(struct extent *ext)
{
	return (ptr_ext_t *)((uintptr_t)ext->ptr + ext->size + HFER_SIZE);
}

/*
 * vmcache_get_prev_ptr_ext -- get the pointer to the previous extent
 */
static inline ptr_ext_t *
vmcache_get_prev_ptr_ext(struct footer *footer, size_t size)
{
	return (ptr_ext_t *)((uintptr_t)footer - size);
}

/*
 * vmcache_insert_heap_entry -- insert the 'he' entry into the list of extents
 */
static inline int
vmcache_insert_heap_entry(struct heap *heap, struct heap_entry *he,
				ptr_ext_t **first_extent, int is_allocated)
{
	struct header *header = he->ptr;
	struct footer *footer = vmcache_create_footer_address(he);

	/* pointer and size of a new extent */
	ptr_ext_t *new_extent = vmcache_create_ext_ptr(he);
	size_t size_flags =
		SIZE_FLAGS((he->size - HFER_SIZE), is_allocated);

	/* save the header */
	header->next = *first_extent;
	header->prev = NULL;
	header->size_flags = size_flags;

	/* save the footer */
	footer->size_flags = size_flags;

	if (*first_extent) {
		struct header *first_header =
				vmcache_extent_get_header(*first_extent);
		ASSERTeq(first_header->prev, NULL);
		first_header->prev = new_extent;
	}

	*first_extent = new_extent;

#ifdef STATS_ENABLED
	if (!is_allocated)
		heap->entries++;
#endif

	return 0;
}

/*
 * vmcache_pop_heap_entry -- pop the free entry from the heap
 */
static inline int
vmcache_pop_heap_entry(struct heap *heap, struct heap_entry *he)
{
	if (heap->first_extent == NULL)
		return -1;

	struct header *header = vmcache_extent_get_header(heap->first_extent);
	struct footer *footer = vmcache_extent_get_footer(heap->first_extent);
	ASSERTeq(header->prev, NULL);
	ASSERTeq((header->size_flags & FLAG_ALLOCATED), 0); /* is free */
	ASSERTeq(header->size_flags, footer->size_flags);

	he->ptr = header;
	he->size = header->size_flags + HFER_SIZE;

	if (header->next) {
		struct header *next_header =
				vmcache_extent_get_header(header->next);
		ASSERTne(next_header->prev, NULL);
		next_header->prev = NULL;
	}

	heap->first_extent = header->next;

#ifdef STATS_ENABLED
	heap->entries--;
#endif

	return 0;
}

/*
 * vmcache_heap_add_mapping -- add new memory mapping to vmemcache heap
 */
static void
vmcache_heap_add_mapping(struct heap *heap, void *addr, size_t size)
{
	LOG(3, "heap %p addr %p size %zu", heap, addr, size);

	void *new_addr;
	size_t new_size;

	/* reserve 64 bytes for a guard header */
	new_addr = (void *)ALIGN_UP((uintptr_t)addr + GUARD_SIZE, GUARD_SIZE);
	if (new_addr > addr)
		size -= ((uintptr_t)new_addr - (uintptr_t)addr);

	/* reserve 64 bytes for a guard footer */
	new_size = ALIGN_DOWN(size - GUARD_SIZE, GUARD_SIZE);

	util_mutex_lock(&heap->lock);

	/* add new memory chunk to the heap */
	struct heap_entry new_mem = {new_addr, new_size};
	vmcache_insert_heap_entry(heap, &new_mem, &heap->first_extent, IS_FREE);

	/* read the added extent */
	struct extent ext;
	ext.ptr = heap->first_extent;
	ext.size = vmcache_extent_get_size(ext.ptr);

	/* mark the guard header as allocated */
	struct footer *prev_footer = vmcache_get_prev_footer(&ext);
	uint64_t *size_flags = &prev_footer->size_flags;
	*size_flags |= FLAG_ALLOCATED;

	/* mark the guard footer as allocated */
	ptr_ext_t *next_ptr = vmcache_get_next_ptr_ext(&ext);
	struct header *header_next = vmcache_extent_get_header(next_ptr);
	size_flags = &header_next->size_flags;
	*size_flags |= FLAG_ALLOCATED;

	util_mutex_unlock(&heap->lock);
}

/*
 * vmcache_heap_create -- create vmemcache heap
 */
struct heap *
vmcache_heap_create(void *addr, size_t size, size_t extent_size)
{
	LOG(3, "addr %p size %zu extent_size %zu", addr, size, extent_size);

	struct heap *heap;

	heap = Zalloc(sizeof(struct heap));
	if (heap == NULL) {
		ERR("!Zalloc");
		return NULL;
	}

	util_mutex_init(&heap->lock);

	heap->extent_size = extent_size;

	vmcache_heap_add_mapping(heap, addr, size);

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
 * vmcache_free_extent -- (internal) free the smallest extent
 */
static int
vmcache_free_extent(struct heap *heap, ptr_ext_t *small_extent)
{
	ASSERTne(small_extent, NULL);

	struct header *header = vmcache_extent_get_header(small_extent);

	/* remove the extent from the list */
	if (header->prev) {
		struct header *prev_header =
				vmcache_extent_get_header(header->prev);
		ASSERTeq(prev_header->next, small_extent);
		prev_header->next = header->next;
	}

	if (header->next) {
		struct header *next_header =
				vmcache_extent_get_header(header->next);
		ASSERTeq(next_header->prev, small_extent);
		next_header->prev = header->prev;
	}

	struct extent ext;
	ext.ptr = small_extent;
	ext.size = heap->extent_size - HFER_SIZE;

	/* free the extent */
	struct heap_entry he;
	vmcache_heap_merge(heap, &ext, &he);
	vmcache_insert_heap_entry(heap, &he, &heap->first_extent, IS_FREE);

#ifdef STATS_ENABLED
	heap->size_used -= ext.size;
#endif

	return 0;
}

/*
 * vmcache_alloc -- allocate memory (take it from the queue)
 *
 * It returns the number of allocated bytes if successful, otherwise -1.
 * The last extent of doubly-linked list of allocated extents is returned
 * in 'first_extent'.
 * 'small_extent' has to be zeroed in the beginning of a new allocation
 * (e.g. when *first_extent == NULL).
 */
ssize_t
vmcache_alloc(struct heap *heap, size_t size, ptr_ext_t **first_extent,
		ptr_ext_t **small_extent)
{
	ASSERTne(first_extent, NULL);
	ASSERTne(small_extent, NULL);
	ASSERT((*first_extent == NULL) ? (*small_extent == NULL) : 1);

	LOG(3, "heap %p size %zu first_extent %p *small_extent %p",
			heap, size, *first_extent, *small_extent);

	struct heap_entry he, new;
	size_t extent_size = heap->extent_size;
	size_t to_allocate = size;
	size_t allocated = 0;

	util_mutex_lock(&heap->lock);

	do {
		if (vmcache_pop_heap_entry(heap, &he))
			break;

		size_t alloc_size = roundup(to_allocate + HFER_SIZE,
						extent_size);

		if (he.size >= alloc_size + extent_size) {
			new.ptr = vmcache_new_heap_entry(he.ptr, alloc_size);
			new.size = he.size - alloc_size;
			he.size = alloc_size;

			vmcache_insert_heap_entry(heap, &new,
					&heap->first_extent, IS_FREE);
		}

		if (vmcache_insert_heap_entry(heap, &he, first_extent,
							IS_ALLOCATED)) {
			util_mutex_unlock(&heap->lock);
			return -1;
		}

		if (*small_extent == NULL && he.size == extent_size)
			*small_extent = *first_extent;

		/* allocated size without headers */
		size_t allocated_size = he.size - HFER_SIZE;
		allocated += allocated_size;

		if (allocated_size > to_allocate &&
		    allocated_size - to_allocate >= extent_size - HFER_SIZE &&
		    *small_extent != NULL) {
			vmcache_free_extent(heap, *small_extent);
		}

		to_allocate -= MIN(allocated_size, to_allocate);

	} while (to_allocate > 0);

#ifdef STATS_ENABLED
	heap->size_used += allocated;
#endif

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

	struct header *header = vmcache_extent_get_header(ext->ptr);

	ASSERT(header->next || header->prev ||
			(heap->first_extent == ext->ptr));

	if (header->next) {
		struct header *header_next =
				vmcache_extent_get_header(header->next);
		ASSERTeq(header_next->prev, ext->ptr);
		header_next->prev = header->prev;
	}

	if (header->prev) {
		struct header *header_prev =
				vmcache_extent_get_header(header->prev);
		ASSERTeq(header_prev->next, ext->ptr);
		header_prev->next = header->next;
	}

	if (heap->first_extent == ext->ptr)
		heap->first_extent = header->next;

#ifdef STATS_ENABLED
	heap->entries--;
#endif
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

	he->ptr = vmcache_extent_get_header(ext->ptr);
	he->size = ext->size + HFER_SIZE;

	/* merge with the previous one (lower address) */
	struct footer *prev_footer = vmcache_get_prev_footer(ext);
	prev.size = prev_footer->size_flags;
	if ((prev.size & FLAG_ALLOCATED) == 0) {
		prev.ptr = vmcache_get_prev_ptr_ext(prev_footer, prev.size);
		he->ptr = vmcache_extent_get_header(prev.ptr);
		he->size += prev.size + HFER_SIZE;
		vmcache_heap_remove(heap, &prev);
	}

	/* merge with the next one (higher address) */
	next.ptr = vmcache_get_next_ptr_ext(ext);
	struct header *header_next = vmcache_extent_get_header(next.ptr);
	next.size = header_next->size_flags;
	if ((next.size & FLAG_ALLOCATED) == 0) {
		he->size += next.size + HFER_SIZE;
		vmcache_heap_remove(heap, &next);
	}
}

/*
 * vmcache_free -- free memory (give it back to the queue)
 */
void
vmcache_free(struct heap *heap, ptr_ext_t *first_extent)
{
	LOG(3, "heap %p first_extent %p", heap, first_extent);

	util_mutex_lock(&heap->lock);

	size_t freed = 0;

	/*
	 * EXTENTS_FOREACH_SAFE variant is required here,
	 * because vmcache_insert_heap_entry() can modify
	 * the headers of the 'ext' extent.
	 */
	ptr_ext_t *__next;
	struct extent ext;
	EXTENTS_FOREACH_SAFE(ext, first_extent, __next) {
		/* size without headers */
		freed += ext.size;

		struct heap_entry he;
		vmcache_heap_merge(heap, &ext, &he);
		vmcache_insert_heap_entry(heap,
					&he, &heap->first_extent, IS_FREE);
	}

#ifdef STATS_ENABLED
	heap->size_used -= freed;
#endif

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
