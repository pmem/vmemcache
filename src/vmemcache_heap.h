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
#include "vec.h"

/* type of the statistics */
typedef unsigned long long stat_t;

#ifdef __cplusplus
extern "C" {
#endif

#define HEAP_ENTRY_IS_NULL(he) ((he.ptr) == NULL)

struct heap_entry {
	void *ptr;
	size_t size;
};

VEC(fragment_vec, struct heap_entry);

struct heap;

struct heap *vmcache_heap_create(void *addr, size_t size, size_t fragment_size);
void vmcache_heap_destroy(struct heap *heap);

ssize_t vmcache_alloc(struct heap *heap, size_t size, struct fragment_vec *vec);
void vmcache_free(struct heap *heap, struct fragment_vec *vec);

stat_t vmcache_get_heap_used_size(struct heap *heap);

#ifdef __cplusplus
}
#endif

#endif
