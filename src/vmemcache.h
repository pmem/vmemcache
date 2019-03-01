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
 * vmemcache.h -- internal definitions for vmemcache
 */

#ifndef VMEMCACHE_H
#define VMEMCACHE_H 1

#include <stdint.h>
#include <stddef.h>

#include "libvmemcache.h"
#include "vmemcache_heap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMEMCACHE_PREFIX "libvmemcache"
#define VMEMCACHE_LEVEL_VAR "VMEMCACHE_LEVEL"
#define VMEMCACHE_FILE_VAR "VMEMCACHE_FILE"

struct index;
struct repl_p;

struct vmemcache {
	void *addr;			/* mapping address */
	size_t size;			/* mapping size */
	struct heap *heap;		/* heap address */
	struct index *index;		/* indexing structure */
	struct repl_p *repl;		/* replacement policy abstraction */
	vmemcache_on_evict *on_evict;	/* callback on evict */
	void *arg_evict;		/* argument for callback on evict */
	vmemcache_on_miss *on_miss;	/* callback on miss */
	void *arg_miss;			/* argument for callback on miss */
	unsigned index_only:1;		/* bench: disable repl+alloc */
	unsigned no_alloc:1;		/* bench: disable allocations */
	unsigned no_memcpy:1;		/* bench: don't copy actual data */

	/* statistics */
	stat_t put_count;		/* total number of puts */
	stat_t get_count;		/* total number of gets */
	stat_t miss_count;		/* total number of misses */
	stat_t evict_count;		/* total number of evicts */
	stat_t size_DRAM;		/* current size of DRAM used for keys */
};

struct cache_entry {
	struct value {
		uint64_t refcount;
		int evicting;
		struct repl_p_entry *p_entry;
		size_t vsize;
		struct fragment_vec fragments;
	} value;

	struct key {
		size_t ksize;
		char key[];
	} key;
};

/* type of callback deleting a cache entry */
typedef void (*delete_entry_t)(struct cache_entry *entry);

/* callback deleting a cache entry (of the above type 'delete_entry_t') */
void vmemcache_delete_entry_cb(struct cache_entry *entry);

void vmemcache_entry_acquire(struct cache_entry *entry);
void vmemcache_entry_release(VMEMcache *cache, struct cache_entry *entry);

#ifdef __cplusplus
}
#endif

#endif
