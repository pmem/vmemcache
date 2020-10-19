// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

/*
 * vmemcache_index.h -- internal definitions for vmemcache indexing API
 */

#ifndef VMEMCACHE_INDEX_H
#define VMEMCACHE_INDEX_H 1

#include "libvmemcache.h"
#include "critnib.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cache_entry;

struct index *vmcache_index_new(void);
void vmcache_index_delete(struct index *index, delete_entry_t del_entry);
int vmcache_index_insert(struct index *index,
			struct cache_entry *entry);
int vmcache_index_get(struct index *index, const void *key, size_t ksize,
			struct cache_entry **entry, int bump_stat);
int vmcache_index_remove(VMEMcache *cache, struct cache_entry *entry);
size_t vmemcache_index_get_stat(struct index *index,
	enum vmemcache_statistic stat);

#ifdef __cplusplus
}
#endif

#endif
