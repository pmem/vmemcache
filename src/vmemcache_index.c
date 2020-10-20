// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

/*
 * vmemcache_index.c -- abstraction layer for vmemcache indexing API
 */

#include <alloca.h>
#include <stdlib.h>
#include <errno.h>
#include <malloc.h>

#include "vmemcache.h"
#include "vmemcache_index.h"
#include "critnib.h"
#include "fast-hash.h"
#include "sys_util.h"

#ifdef STATS_ENABLED
#define STAT_ADD(ptr, add) util_fetch_and_add64(ptr, add)
#else
#define STAT_ADD(ptr, add) do {} while (0)
#endif

/* must be a power of 2 */
#define NSHARDS 256

struct index {
	struct critnib *bucket[NSHARDS];
	int sharding;
};

/*
 * shard_id -- (internal) hash the key and pick a shard bucket id
 */
static int
shard_id(size_t key_size, const char *key)
{
	return (int)hash(key_size, key) & (NSHARDS - 1);
}

/*
 * shard -- (internal) pick a shard bucket
 */
static struct critnib *
shard(struct index *index, size_t key_size, const char *key)
{
	if (index->sharding)
		return index->bucket[shard_id(key_size, key)];

	return index->bucket[0];
}

/*
 * vmcache_index_new -- initialize vmemcache indexing structure
 */
struct index *
vmcache_index_new(void)
{
	struct index *index = Malloc(sizeof(struct index));
	if (!index)
		return NULL;

	index->sharding = env_yesno10("VMEMCACHE_SHARDING", 1);

	for (int i = 0; i < NSHARDS; i++) {
		struct critnib *c = critnib_new();
		if (!c) {
			for (i--; i >= 0; i--) {
				util_rwlock_destroy(&index->bucket[i]->lock);
				critnib_delete(index->bucket[i], NULL);
			}
			Free(index);

			return NULL;
		}

		util_rwlock_init(&c->lock);
		index->bucket[i] = c;
	}

	return index;
}

/*
 * vmcache_index_delete -- destroy vmemcache indexing structure
 */
void
vmcache_index_delete(struct index *index, delete_entry_t del_entry)
{
	for (int i = 0; i < NSHARDS; i++) {
		util_rwlock_destroy(&index->bucket[i]->lock);
		critnib_delete(index->bucket[i], del_entry);
	}

	Free(index);
}

/*
 * vmcache_index_insert -- insert data into the vmemcache indexing structure
 */
int
vmcache_index_insert(struct index *index, struct cache_entry *entry)
{
	struct critnib *c = shard(index, entry->key.ksize, entry->key.key);

	util_rwlock_wrlock(&c->lock);

	int err = critnib_set(c, entry);
	if (err) {
		errno = err;
		util_rwlock_unlock(&c->lock);
		ERR("inserting to the index failed");
		return -1;
	}

#ifdef STATS_ENABLED
	c->leaf_count++;
	c->put_count++;
	c->DRAM_usage += malloc_usable_size(entry);
#endif

	/* this is the first and the only one reference now (in the index) */
	entry->value.refcount = 1;

	util_rwlock_unlock(&c->lock);

	return 0;
}

/*
 * vmcache_index_get -- get data from the vmemcache indexing structure
 */
int
vmcache_index_get(struct index *index, const void *key, size_t ksize,
			struct cache_entry **entry, int bump_stat)
{
#define SIZE_1K 1024
	struct critnib *c = shard(index, ksize, key);

	struct cache_entry *e;

	*entry = NULL;

	if (ksize > SIZE_1K) {
		e = Malloc(sizeof(struct cache_entry) + ksize);
		if (e == NULL) {
			ERR("!Zalloc");
			return -1;
		}
	} else {
		e = alloca(sizeof(struct cache_entry) + ksize);
	}

	e->key.ksize = ksize;
	memcpy(e->key.key, key, ksize);

	util_rwlock_rdlock(&c->lock);

	struct cache_entry *v = critnib_get(c, e);
	if (ksize > SIZE_1K)
		Free(e);
	if (v == NULL) {
		util_rwlock_unlock(&c->lock);

		if (bump_stat)
			STAT_ADD(&c->miss_count, 1);

		LOG(1,
			"vmcache_index_get: cannot find an element with the given key in the index");
		return 0;
	}

	if (bump_stat)
		STAT_ADD(&c->hit_count, 1);

	vmemcache_entry_acquire(v);
	*entry = v;

	util_rwlock_unlock(&c->lock);

	return 0;
}

/*
 * vmcache_index_remove -- remove data from the vmemcache indexing structure
 */
int
vmcache_index_remove(VMEMcache *cache, struct cache_entry *entry)
{
	struct critnib *c = shard(cache->index, entry->key.ksize,
		entry->key.key);

	util_rwlock_wrlock(&c->lock);

	struct cache_entry *v = critnib_remove(c, entry);
	if (v == NULL) {
		util_rwlock_unlock(&c->lock);
		ERR(
			"vmcache_index_remove: cannot find an element with the given key in the index");
		errno = EINVAL;
		return -1;
	}

#ifdef STATS_ENABLED
	c->leaf_count--;
	c->evict_count++;
	c->DRAM_usage -= malloc_usable_size(entry);
#endif

	vmemcache_entry_release(cache, entry);

	util_rwlock_unlock(&c->lock);

	return 0;
}

/*
 * vmemcache_index_get_stat -- query an index-held stat
 */
size_t
vmemcache_index_get_stat(struct index *index, enum vmemcache_statistic stat)
{
	size_t total = 0;

	switch (stat) {
	case VMEMCACHE_STAT_DRAM_SIZE_USED:
	{
		size_t nodes = 0;

		for (int i = 0; i < NSHARDS; i++) {
			nodes += index->bucket[i]->node_count;
			total += index->bucket[i]->DRAM_usage;
		}

		return total + nodes * sizeof(struct critnib_node);
	}

	case VMEMCACHE_STAT_PUT:
		for (int i = 0; i < NSHARDS; i++)
			total += index->bucket[i]->put_count;
		break;

	case VMEMCACHE_STAT_EVICT:
		for (int i = 0; i < NSHARDS; i++)
			total += index->bucket[i]->evict_count;
		break;

	case VMEMCACHE_STAT_HIT:
		for (int i = 0; i < NSHARDS; i++)
			total += index->bucket[i]->hit_count;
		break;

	case VMEMCACHE_STAT_MISS:
		for (int i = 0; i < NSHARDS; i++)
			total += index->bucket[i]->miss_count;
		break;

	case VMEMCACHE_STAT_ENTRIES:
		for (int i = 0; i < NSHARDS; i++)
			total += index->bucket[i]->leaf_count;
		break;

	default:
		FATAL("wrong stat type"); /* not callable from outside */
	}

	return total;
}
