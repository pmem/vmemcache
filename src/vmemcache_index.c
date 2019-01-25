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
 * vmemcache_index.c -- abstraction layer for vmemcache indexing API
 */

#include <alloca.h>
#include <stdlib.h>
#include <errno.h>

#include "vmemcache.h"
#include "vmemcache_index.h"
#include "critnib.h"
#include "sys_util.h"

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
	/* Fowler–Noll–Vo hash */
	uint64_t h = 0xcbf29ce484222325;
	for (size_t i = 0; i < key_size; i++)
		h = (h ^ (unsigned char)*key++) * 0x100000001b3;

	return h & (NSHARDS - 1);
}

/*
 * shard -- (internal) pick a shard bucket
 */
static struct critnib *
shard(VMEMcache *cache, size_t key_size, const char *key)
{
	if (cache->index->sharding)
		return cache->index->bucket[shard_id(key_size, key)];

	return cache->index->bucket[0];
}

/*
 * vmcache_index_new -- initialize vmemcache indexing structure
 */
int
vmcache_index_new(VMEMcache *cache, delete_entry_t del_entry)
{
	struct index *index = malloc(sizeof(struct index));
	if (!index)
		return ENOMEM;
	cache->index = index;
	index->sharding = !getenv("VMEMCACHE_NO_SHARDING");

	for (int i = 0; i < NSHARDS; i++) {
		struct critnib *c = critnib_new();
		if (!c) {
			for (i--; i >= 0; i--) {
				critnib_delete(index->bucket[i]);
				util_mutex_destroy(&index->bucket[i]->lock);
			}

			return ENOMEM;
		}

		util_mutex_init(&c->lock);
		index->bucket[i] = c;
	}

	return 0;
}

/*
 * vmcache_index_delete -- destroy vmemcache indexing structure
 */
void
vmcache_index_delete(VMEMcache *cache, delete_entry_t del_entry)
{
	struct index *index = cache->index;

	for (int i = 0; i < NSHARDS; i++) {
		util_mutex_destroy(&index->bucket[i]->lock);
		critnib_delete(index->bucket[i]);
	}

	free(cache->index);
	cache->index = NULL;
}

/*
 * vmcache_index_insert -- insert data into the vmemcache indexing structure
 */
int
vmcache_index_insert(VMEMcache *cache, struct cache_entry *entry)
{
	struct critnib *c = shard(cache, entry->key.ksize, entry->key.key);

	util_mutex_lock(&c->lock);

	if (critnib_set(c, entry)) {
		util_mutex_unlock(&c->lock);
		ERR("inserting to the index failed");
		return -1;
	}

	/* this is the first and the only one reference now (in the index) */
	entry->value.refcount = 1;

	util_mutex_unlock(&c->lock);

	return 0;
}

/*
 * vmcache_index_get -- get data from the vmemcache indexing structure
 */
int
vmcache_index_get(VMEMcache *cache, const void *key, size_t ksize,
			struct cache_entry **entry)
{
#define SIZE_1K 1024
	struct critnib *c = shard(cache, ksize, key);

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

	util_mutex_lock(&c->lock);

	struct cache_entry *v = critnib_get(c, e);
	if (ksize > SIZE_1K)
		Free(e);
	if (v == NULL) {
		util_mutex_unlock(&c->lock);
		LOG(1,
			"vmcache_index_get: cannot find an element with the given key in the index");
		return 0;
	}

	vmemcache_entry_acquire(v);
	*entry = v;

	util_mutex_unlock(&c->lock);

	return 0;
}

/*
 * vmcache_index_remove -- remove data from the vmemcache indexing structure
 */
int
vmcache_index_remove(VMEMcache *cache, struct cache_entry *entry)
{
	struct critnib *c = shard(cache, entry->key.ksize, entry->key.key);

	util_mutex_lock(&c->lock);

	struct cache_entry *v = critnib_remove(c, entry);
	if (v == NULL) {
		util_mutex_unlock(&c->lock);
		ERR(
			"vmcache_index_remove: cannot find an element with the given key in the index");
		errno = EINVAL;
		return -1;
	}

	vmemcache_entry_release(cache, entry);

	util_mutex_unlock(&c->lock);

	return 0;
}
