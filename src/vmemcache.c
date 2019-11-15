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
 * vmemcache.c -- vmemcache implementation
 */

#include <sys/mman.h>
#include <errno.h>

#include "out.h"
#include "file.h"
#include "mmap.h"

#include "libvmemcache.h"
#include "vmemcache.h"
#include "vmemcache_heap.h"
#include "vmemcache_index.h"
#include "vmemcache_repl.h"
#include "valgrind_internal.h"

/*
 * Arguments to currently running get request, during a callback.
 */
static __thread struct {
	const char *key;
	size_t ksize;
	void *vbuf;
	size_t vbufsize;
	size_t offset;
	size_t *vsize;
} get_req = { 0 };

/*
 * vmemcache_new -- create a vmemcache
 */
VMEMcache *
vmemcache_new()
{
	LOG(3, NULL);

	VMEMcache *cache = Zalloc(sizeof(VMEMcache));
	if (cache == NULL) {
		ERR("!Zalloc");
		return NULL;
	}

	cache->repl_p = VMEMCACHE_REPLACEMENT_LRU;
	cache->extent_size = VMEMCACHE_MIN_EXTENT;

	return cache;
}

/*
 * vmemcache_set_eviction_policy
 */
int
vmemcache_set_eviction_policy(VMEMcache *cache,
	enum vmemcache_repl_p repl_p)
{
	LOG(3, "cache %p eviction policy %d", cache, repl_p);

	if (cache->ready) {
		ERR("cache already in use");
		errno =  EALREADY;
		return -1;
	}

	cache->repl_p = repl_p;
	return 0;
}

/*
 * vmemcache_set_size
 */
int
vmemcache_set_size(VMEMcache *cache, size_t size)
{
	LOG(3, "cache %p size %zu", cache, size);

	/* TODO: allow growing this way */
	if (cache->ready) {
		ERR("cache already in use");
		errno =  EALREADY;
		return -1;
	}

	if (size < VMEMCACHE_MIN_POOL) {
		ERR("size %zu smaller than %zu", size, VMEMCACHE_MIN_POOL);
		errno = EINVAL;
		return -1;
	}

	if (size >= 1ULL << ((sizeof(void *) > 4) ? 56 : 31)) {
		ERR("implausible large size %zu", size);
		errno = EINVAL;
		return -1;
	}

	cache->size = size;
	return 0;
}

/*
 * vmemcache_set_extent_size
 */
int
vmemcache_set_extent_size(VMEMcache *cache, size_t extent_size)
{
	LOG(3, "cache %p extent_size %zu", cache, extent_size);

	if (cache->ready) {
		ERR("cache already in use");
		errno =  EALREADY;
		return -1;
	}

	if (extent_size < VMEMCACHE_MIN_EXTENT) {
		ERR("extent size %zu smaller than %zu bytes",
			extent_size, VMEMCACHE_MIN_EXTENT);
		errno = EINVAL;
		return -1;
	}

	cache->extent_size = extent_size;
	return 0;
}

/*
 * vmemcache_addU -- (internal) open the backing file
 */
#ifndef _WIN32
static inline
#endif
int
vmemcache_addU(VMEMcache *cache, const char *dir)
{
	LOG(3, "cache %p dir %s", cache, dir);

	if (cache->ready) {
		ERR("the cache is already initialized");
		errno = EBUSY;
		return -1;
	}

	size_t size = cache->size;

	if (size && cache->extent_size > size) {
		ERR(
			"extent size %zu larger than cache size: %zu bytes",
			cache->extent_size, size);
		errno = EINVAL;
		return -1;
	}

	if (size && size < VMEMCACHE_MIN_POOL) {
		ERR("cache size %zu smaller than %zu", size,
			VMEMCACHE_MIN_POOL);
		errno = EINVAL;
		return -1;
	}

	enum file_type type = util_file_get_type(dir);
	if (type == OTHER_ERROR) {
		LOG(1, "checking file type failed");
		return -1;
	}

	if (type == TYPE_DEVDAX) {
		const char *devdax = dir;
		ssize_t dax_size = util_file_get_size(devdax);
		if (dax_size < 0) {
			LOG(1, "cannot determine file length \"%s\"", devdax);
			return -1;
		}

		if (size != 0 && size > (size_t)dax_size) {
			ERR(
				"error: maximum cache size (%zu) is bigger than the size of the DAX device (%zi)",
				size, dax_size);
			errno = EINVAL;
			return -1;
		}

		if (size == 0) {
			cache->size = (size_t)dax_size;
		} else {
			cache->size = roundup(size, Mmap_align);
			if (cache->size > (size_t)dax_size)
				cache->size = (size_t)dax_size;
		}

		cache->addr = util_file_map_whole(devdax);
		if (cache->addr == NULL) {
			LOG(1, "mapping of whole DAX device failed");
			return -1;
		}

	} else {
		/* silently enforce multiple of mapping alignment */
		cache->size = roundup(cache->size, Mmap_align);

		/* if not set, start with the default */
		if (!cache->size)
			cache->size = VMEMCACHE_MIN_POOL;

		/*
		 * XXX: file should be mapped on-demand during allocation,
		 *      up to cache->size
		 */
		cache->addr = util_map_tmpfile(dir, cache->size, 4 * MEGABYTE);
		if (cache->addr == NULL) {
			LOG(1, "mapping of a temporary file failed");
			return -1;
		}
	}

	cache->heap = vmcache_heap_create(cache->addr, cache->size,
						cache->extent_size);
	if (cache->heap == NULL) {
		LOG(1, "heap initialization failed");
		goto error_unmap;
	}

	cache->index = vmcache_index_new();
	if (cache->index == NULL) {
		LOG(1, "indexing structure initialization failed");
		goto error_destroy_heap;
	}

	cache->repl = repl_p_init(cache->repl_p);
	if (cache->repl == NULL) {
		LOG(1, "replacement policy initialization failed");
		goto error_destroy_index;
	}

	cache->ready = 1;

	return 0;

error_destroy_index:
	vmcache_index_delete(cache->index, vmemcache_delete_entry_cb);
	cache->index = NULL;
error_destroy_heap:
	vmcache_heap_destroy(cache->heap);
	cache->heap = NULL;
error_unmap:
	util_unmap(cache->addr, cache->size);
	cache->addr = NULL;
	return -1;
}

/*
 * vmemcache_delete_entry_cb -- callback deleting a vmemcache entry
 *                              for vmemcache_delete()
 */
void
vmemcache_delete_entry_cb(struct cache_entry *entry)
{
	Free(entry);
}

/*
 * vmemcache_delete -- destroy a vmemcache
 */
void
vmemcache_delete(VMEMcache *cache)
{
	LOG(3, "cache %p", cache);

	if (cache->ready) {
		repl_p_destroy(cache->repl);
		vmcache_index_delete(cache->index, vmemcache_delete_entry_cb);
		vmcache_heap_destroy(cache->heap);
		util_unmap(cache->addr, cache->size);
	}
	Free(cache);
}

/*
 * vmemcache_populate_extents -- (internal) copies content of value
 *                                  to heap entries
 */
static void
vmemcache_populate_extents(struct cache_entry *entry,
				const void *value, size_t value_size)
{
	struct extent ext;
	size_t size_left = value_size;

	EXTENTS_FOREACH(ext, entry->value.extents) {
		ASSERT(size_left > 0);
		size_t len = (ext.size < size_left) ? ext.size : size_left;
		memcpy(ext.ptr, value, len);
		value = (char *)value + len;
		size_left -= len;
	}

	entry->value.vsize = value_size;
}

static void
vmemcache_put_satisfy_get(const void *key, size_t ksize,
		const void *value, size_t value_size)
{
	if (get_req.ksize != ksize || memcmp(get_req.key, key, ksize))
		return; /* not our key */

	get_req.key = NULL; /* mark request as satisfied */

	if (get_req.offset >= value_size) {
		get_req.vbufsize = 0;
	} else {
		if (get_req.vbufsize > value_size - get_req.offset)
			get_req.vbufsize = value_size - get_req.offset;
		if (get_req.vbuf)
			memcpy(get_req.vbuf, value, get_req.vbufsize);
	}

	if (get_req.vsize)
		*get_req.vsize = value_size;
}

/*
 * vmemcache_put -- put an element into the vmemcache
 */
int
vmemcache_put(VMEMcache *cache, const void *key, size_t ksize,
				const void *value, size_t value_size)
{
	LOG(3, "cache %p key %p ksize %zu value %p value_size %zu",
		cache, key, ksize, value, value_size);

	if (get_req.key)
		vmemcache_put_satisfy_get(key, ksize, value, value_size);

	if (value_size > cache->size) {
		ERR("value larger than entire cache");
		errno = ENOSPC;
		return -1;
	}

	struct cache_entry *entry;

	entry = Zalloc(sizeof(struct cache_entry) + ksize);
	if (entry == NULL) {
		ERR("!Zalloc");
		return -1;
	}

	entry->key.ksize = ksize;
	memcpy(entry->key.key, key, ksize);

	if (cache->index_only || cache->no_alloc)
		goto put_index;

	ptr_ext_t *small_extent = NULL; /* required by vmcache_alloc() */
	size_t left_to_allocate = value_size;

	while (left_to_allocate != 0) {
		ssize_t allocated = vmcache_alloc(cache->heap, left_to_allocate,
							&entry->value.extents,
							&small_extent);
		if (allocated < 0)
			goto error_exit;

		if (allocated == 0 && vmemcache_evict(cache, NULL, 0)) {
			LOG(1, "vmemcache_evict() failed");
			if (errno == ESRCH)
				errno = ENOSPC;
			goto error_exit;
		}

		left_to_allocate -= MIN((size_t)allocated, left_to_allocate);
	}

	if (cache->no_memcpy)
		entry->value.vsize = value_size;
	else
		vmemcache_populate_extents(entry, value, value_size);

put_index:
	if (vmcache_index_insert(cache->index, entry)) {
		LOG(1, "inserting to the index failed");
		goto error_exit;
	}

	if (!cache->index_only) {
		cache->repl->ops->repl_p_insert(cache->repl->head, entry,
					&entry->value.p_entry);
	}

	return 0;

error_exit:
	vmcache_free(cache->heap, entry->value.extents);

	Free(entry);

	return -1;
}

/*
 * vmemcache_populate_value -- (internal) copies content of heap entries
 *                              to the output value's buffer 'vbuf' starting
 *                              from the 'offset'
 */
static size_t
vmemcache_populate_value(void *vbuf, size_t vbufsize, size_t offset,
				struct cache_entry *entry, int no_memcpy)
{
	if (!vbuf || offset >= entry->value.vsize)
		return 0;

	size_t left_to_copy = entry->value.vsize - offset;
	struct extent ext;
	size_t copied = 0;

	EXTENTS_FOREACH(ext, entry->value.extents) {
		char *ptr = (char *)ext.ptr;
		size_t len = ext.size;

		if (offset) {
			if (offset > ext.size) {
				offset -= ext.size;
				continue;
			}

			ptr += offset;
			len -= offset;
			offset = 0;
		}

		size_t max_len = MIN(left_to_copy, vbufsize);
		if (len > max_len)
			len = max_len;

		if (!no_memcpy)
			memcpy(vbuf, ptr, len);

		vbufsize -= len;
		vbuf = (char *)vbuf + len;
		copied += len;
		left_to_copy -= len;

		if (vbufsize == 0 || left_to_copy == 0)
			return copied;
	}

	return copied;
}

/*
 * vmemcache_entry_acquire -- acquire pointer to the vmemcache entry
 */
void
vmemcache_entry_acquire(struct cache_entry *entry)
{
	uint64_t ret = util_fetch_and_add32(&entry->value.refcount, 1);

	ASSERTne(ret, 0);
}

/*
 * vmemcache_entry_release -- release or delete the vmemcache entry
 */
void
vmemcache_entry_release(VMEMcache *cache, struct cache_entry *entry)
{
	if (util_fetch_and_sub32(&entry->value.refcount, 1) != 1) {
		VALGRIND_ANNOTATE_HAPPENS_BEFORE(&entry->value.refcount);
		return;
	}

	/* 'refcount' equals 0 now - it means that the entry should be freed */

	VALGRIND_ANNOTATE_HAPPENS_AFTER(&entry->value.refcount);
	VALGRIND_ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(&entry->value.refcount);

	vmcache_free(cache->heap, entry->value.extents);

	Free(entry);
}

/*
 * vmemcache_get - get an element from the vmemcache,
 *                 returns the number of bytes read
 */
ssize_t
vmemcache_get(VMEMcache *cache, const void *key, size_t ksize, void *vbuf,
		size_t vbufsize, size_t offset, size_t *vsize)
{
	LOG(3,
		"cache %p key %p ksize %zu vbuf %p vbufsize %zu offset %zu vsize %p",
		cache, key, ksize, vbuf, vbufsize, offset, vsize);

	struct cache_entry *entry;
	size_t read = 0;

	int ret = vmcache_index_get(cache->index, key, ksize, &entry, 1);
	if (ret < 0)
		return -1;

	if (entry == NULL) { /* cache miss */
		if (cache->on_miss) {
			get_req.key = key;
			get_req.ksize = ksize;
			get_req.vbuf = vbuf;
			get_req.vbufsize = vbufsize;
			get_req.offset = offset;
			get_req.vsize = vsize;

			(*cache->on_miss)(cache, key, ksize, cache->arg_miss);

			if (!get_req.key)
				return (ssize_t)get_req.vbufsize;
			get_req.key = NULL;
		}

		errno = ENOENT;
		/*
		 * Needed for errormsg but wastes 13% of time.  FIXME.
		 * ERR("cache entry not found");
		 */
		return -1;
	}

	if (cache->index_only)
		goto get_index;

	cache->repl->ops->repl_p_use(cache->repl->head, &entry->value.p_entry);

	if (cache->no_alloc)
		goto get_index;

	read = vmemcache_populate_value(vbuf, vbufsize, offset, entry,
		cache->no_memcpy);
	if (vsize)
		*vsize = entry->value.vsize;

get_index:
	vmemcache_entry_release(cache, entry);

	return (ssize_t)read;
}

/*
 * vmemcache_exists -- checks, without side-effects, if a key exists
 */
int
vmemcache_exists(VMEMcache *cache, const void *key, size_t key_size)
{
	LOG(3, "cache %p key %p key_size %zu", cache, key, key_size);

	struct cache_entry *entry;

	int ret = vmcache_index_get(cache->index, key, key_size, &entry, 0);
	if (ret < 0)
		return -1;

	if (entry == NULL)
		return 0;

	vmemcache_entry_release(cache, entry);

	return 1;
}


/*
 * vmemcache_evict -- evict an element from the vmemcache
 */
int
vmemcache_evict(VMEMcache *cache, const void *key, size_t ksize)
{
	LOG(3, "cache %p key %p ksize %zu", cache, key, ksize);

	struct cache_entry *entry = NULL;
	int evicted_from_repl_p = 0;

	if (key == NULL) {
		do {
			entry = cache->repl->ops->repl_p_evict(
							cache->repl->head,
							NULL);
			if (entry == NULL) {
				ERR("no element to evict");
				return -1;
			}

			evicted_from_repl_p = 1;
			key = entry->key.key;
			ksize = entry->key.ksize;

		} while (!__sync_bool_compare_and_swap(&entry->value.evicting,
							0, 1));
	} else {
		int ret = vmcache_index_get(cache->index, key, ksize, &entry,
			0);
		if (ret < 0)
			return -1;

		if (entry == NULL) {
			ERR(
				"vmemcache_evict: cannot find an element with the given key");
			errno = ENOENT;
			return -1;
		}

		if (!__sync_bool_compare_and_swap(&entry->value.evicting,
							0, 1)) {
			/*
			 * Element with the given key is being evicted just now.
			 * Release the reference from vmcache_index_get().
			 */
			vmemcache_entry_release(cache, entry);
			return 0;
		}
	}

	if (cache->on_evict != NULL)
		(*cache->on_evict)(cache, key, ksize, cache->arg_evict);

	if (!evicted_from_repl_p) {
		if (cache->repl->ops->repl_p_evict(cache->repl->head,
					&entry->value.p_entry) == NULL) {
			/*
			 * The given entry is busy
			 * and cannot be evicted right now.
			 * Release the reference from vmcache_index_get().
			 */
			vmemcache_entry_release(cache, entry);

			/* reset 'evicting' flag */
			__sync_bool_compare_and_swap(&entry->value.evicting,
									1, 0);
			return -1;
		}
		/* release the reference from the replacement policy */
		vmemcache_entry_release(cache, entry);
	}

	/* release the element */
	vmemcache_entry_release(cache, entry);

	if (vmcache_index_remove(cache, entry)) {
		LOG(1, "removing from the index failed");
		goto exit_release;
	}

	return 0;

exit_release:
	/* release the element */
	vmemcache_entry_release(cache, entry);
	return -1;
}

/*
 * vmemcache_callback_on_evict -- install the 'on evict' callback
 */
void
vmemcache_callback_on_evict(VMEMcache *cache, vmemcache_on_evict *evict,
				void *arg)
{
	LOG(3, "cache %p evict %p arg %p", cache, evict, arg);

	cache->on_evict = evict;
	cache->arg_evict = arg;
}

/*
 * vmemcache_callback_on_miss -- install the 'on miss' callback
 */
void
vmemcache_callback_on_miss(VMEMcache *cache, vmemcache_on_miss *miss,
				void *arg)
{
	LOG(3, "cache %p evict %p arg %p", cache, miss, arg);

	cache->on_miss = miss;
	cache->arg_miss = arg;
}

/*
 * vmemcache_get_stat -- get the statistic
 */
int
vmemcache_get_stat(VMEMcache *cache, enum vmemcache_statistic stat,
			void *value, size_t value_size)
{
	LOG(3, "cache %p stat %d value %p value_size %zu",
		cache, stat, value, value_size);

	if (value_size != sizeof(stat_t)) {
		ERR("wrong size of the value: %zu (should be: %zu)",
			value_size, sizeof(stat_t));
		errno = EINVAL;
		return -1;
	}

	stat_t *val = value;

	switch (stat) {
	case VMEMCACHE_STAT_PUT:
	case VMEMCACHE_STAT_HIT:
	case VMEMCACHE_STAT_MISS:
	case VMEMCACHE_STAT_EVICT:
	case VMEMCACHE_STAT_ENTRIES:
		*val = vmemcache_index_get_stat(cache->index, stat);
		break;
	case VMEMCACHE_STAT_GET:
		*val = vmemcache_index_get_stat(cache->index,
			VMEMCACHE_STAT_HIT) +
			vmemcache_index_get_stat(cache->index,
			VMEMCACHE_STAT_MISS);
		break;
	case VMEMCACHE_STAT_DRAM_SIZE_USED:
		*val = vmemcache_index_get_stat(cache->index,
			VMEMCACHE_STAT_DRAM_SIZE_USED)
			+ cache->repl->ops->dram_per_entry
			* vmemcache_index_get_stat(cache->index,
				VMEMCACHE_STAT_ENTRIES);
		break;
	case VMEMCACHE_STAT_POOL_SIZE_USED:
		*val = vmcache_get_heap_used_size(cache->heap);
		break;
	case VMEMCACHE_STAT_HEAP_ENTRIES:
		*val = vmcache_get_heap_entries_count(cache->heap);
		break;
	default:
		ERR("unknown value of statistic: %u", stat);
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static void
prefault(VMEMcache *cache)
{
	char *p = cache->addr;
	char *limit = (char *)cache->addr + cache->size;

	while (p < limit) {
		*(volatile char *)p = *p;

		p += 4096; /* once per page is enough */
	}
}

/*
 * vmemcache_bench_set -- alter a benchmark parameter
 */
void
vmemcache_bench_set(VMEMcache *cache, enum vmemcache_bench_cfg cfg, size_t val)
{
	LOG(3, "cache %p cfg %d val %zu", cache, cfg, val);

	switch (cfg) {
	case VMEMCACHE_BENCH_INDEX_ONLY:
		cache->index_only = !!val;
		break;
	case VMEMCACHE_BENCH_NO_MEMCPY:
		cache->no_memcpy = !!val;
		break;
	case VMEMCACHE_BENCH_PREFAULT:
		prefault(cache);
		break;
	default:
		ERR("invalid config parameter: %u", cfg);
	}
}

#ifndef _WIN32
/*
 * vmemcache_add -- add a backing file to vmemcache
 */
int
vmemcache_add(VMEMcache *cache, const char *path)
{
	return vmemcache_addU(cache, path);
}
#else
/*
 * vmemcache_addW -- add a backing file to vmemcache, wchar version
 */
int
vmemcache_addW(VMEMcache *cache, const wchar_t *path)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL)
		return -1;

	int ret = vmemcache_addU(cache, upath);

	util_free_UTF8(upath);
	return ret;
}
#endif
