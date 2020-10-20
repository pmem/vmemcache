// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

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
	size_t extent_size;		/* heap granularity */
	struct heap *heap;		/* heap address */
	struct index *index;		/* indexing structure */
	enum vmemcache_repl_p repl_p;	/* replacement policy */
	struct repl_p *repl;		/* replacement policy abstraction */
	vmemcache_on_evict *on_evict;	/* callback on evict */
	void *arg_evict;		/* argument for callback on evict */
	vmemcache_on_miss *on_miss;	/* callback on miss */
	void *arg_miss;			/* argument for callback on miss */
	unsigned ready:1;		/* is the cache ready for use? */
	unsigned index_only:1;		/* bench: disable repl+alloc */
	unsigned no_alloc:1;		/* bench: disable allocations */
	unsigned no_memcpy:1;		/* bench: don't copy actual data */
};

struct cache_entry {
	struct value {
		uint32_t refcount;
		int evicting;
		struct repl_p_entry *p_entry;
		size_t vsize;
		ptr_ext_t *extents;
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
