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

#include "vmemcache.h"
#include "vmemcache_index.h"
#include "critnib.h"

/*
 * vmcache_index_new -- initialize vmemcache indexing structure
 */
vmemcache_index_t *
vmcache_index_new(void)
{
	struct critnib *c = critnib_new();
	if (c)
		util_mutex_init(&c->lock);
	return c;
}

/*
 * vmcache_index_delete -- destroy vmemcache indexing structure
 */
void
vmcache_index_delete(vmemcache_index_t *index)
{
	util_mutex_destroy(&index->lock);
	critnib_delete(index);
}

/*
 * vmcache_index_insert -- insert data into the vmemcache indexing structure
 */
int
vmcache_index_insert(vmemcache_index_t *index, struct cache_entry *entry)
{
	util_mutex_lock(&index->lock);

	if (critnib_set(index, entry)) {
		util_mutex_unlock(&index->lock);
		ERR("inserting to the index failed");
		return -1;
	}

	/* this is the first and the only one reference now (in the index) */
	entry->value.refcount = 1;

	util_mutex_unlock(&index->lock);

	return 0;
}

/*
 * vmcache_index_get -- get data from the vmemcache indexing structure
 */
int
vmcache_index_get(vmemcache_index_t *index, const char *key, size_t ksize,
			struct cache_entry **entry)
{
#define SIZE_1K 1024

	struct cache_entry *e;

	struct static_buffer {
		struct cache_entry entry;
		char key[SIZE_1K];
	} sb;

	*entry = NULL;

	if (ksize > SIZE_1K) {
		e = Malloc(sizeof(struct cache_entry) + ksize);
		if (e == NULL) {
			ERR("!Zalloc");
			return -1;
		}
	} else {
		e = (struct cache_entry *)&sb;
	}

	e->key.ksize = ksize;
	memcpy(e->key.key, key, ksize);

	util_mutex_lock(&index->lock);

	struct cache_entry *v = critnib_get(index, e);
	if (ksize > SIZE_1K)
		Free(e);
	if (v == NULL) {
		util_mutex_unlock(&index->lock);
		LOG(1,
			"vmcache_index_get: cannot find an element with the given key in the index");
		return 0;
	}

	vmemcache_entry_acquire(v);
	*entry = v;

	util_mutex_unlock(&index->lock);

	return 0;
}

/*
 * vmcache_index_remove -- remove data from the vmemcache indexing structure
 */
int
vmcache_index_remove(VMEMcache *cache, struct cache_entry *entry)
{
	util_mutex_lock(&cache->index->lock);

	struct cache_entry *v = critnib_remove(cache->index, entry);
	if (v == NULL) {
		util_mutex_unlock(&cache->index->lock);
		ERR(
			"vmcache_index_remove: cannot find an element with the given key in the index");
		errno = EINVAL;
		return -1;
	}

	vmemcache_entry_release(cache, entry);

	util_mutex_unlock(&cache->index->lock);

	return 0;
}
