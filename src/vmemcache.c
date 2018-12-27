/*
 * Copyright 2018, Intel Corporation
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

/*
 * vmemcache_newU -- (internal) create a vmemcache
 */
#ifndef _WIN32
static inline
#endif
VMEMcache *
vmemcache_newU(const char *dir, size_t max_size, size_t fragment_size,
		enum vmemcache_replacement_policy replacement_policy)
{
	LOG(3, "dir %s max_size %zu fragment_size %zu replacement_policy %d",
		dir, max_size, fragment_size, replacement_policy);

	if (max_size < VMEMCACHE_MIN_POOL) {
		ERR("size %zu smaller than %zu", max_size, VMEMCACHE_MIN_POOL);
		errno = EINVAL;
		return NULL;
	}

	if (fragment_size < VMEMCACHE_MIN_FRAG) {
		ERR("fragment size %zu smaller than %zu bytes",
			fragment_size, VMEMCACHE_MIN_FRAG);
		errno = EINVAL;
		return NULL;
	}

	if (fragment_size > max_size) {
		ERR(
			"fragment size %zu larger than maximum file size: %zu bytes",
			fragment_size, max_size);
		errno = EINVAL;
		return NULL;
	}

	VMEMcache *cache = Zalloc(sizeof(VMEMcache));
	if (cache == NULL) {
		ERR("!Zalloc");
		return NULL;
	}

	enum file_type type = util_file_get_type(dir);
	if (type == OTHER_ERROR) {
		LOG(1, "checking file type failed");
		goto error_free_cache;
	}

	if (type == TYPE_DEVDAX) {
		const char *devdax = dir;
		ssize_t size = util_file_get_size(devdax);
		if (size < 0) {
			LOG(1, "cannot determine file length \"%s\"", devdax);
			goto error_free_cache;
		}

		cache->addr = util_file_map_whole(devdax);
		if (cache->addr == NULL) {
			LOG(1, "mapping of whole DAX device failed");
			goto error_free_cache;
		}

		cache->size = (size_t)size;
	} else {
		/* silently enforce multiple of mapping alignment */
		cache->size = roundup(max_size, Mmap_align);

		/*
		 * XXX: file should be mapped on-demand during allocation,
		 *      up to cache->size
		 */
		cache->addr = util_map_tmpfile(dir, cache->size, 4 * MEGABYTE);
		if (cache->addr == NULL) {
			LOG(1, "mapping of a temporary file failed");
			goto error_free_cache;
		}
	}

	cache->heap = vmcache_heap_create(cache->addr, cache->size,
						fragment_size);
	if (cache->heap == NULL) {
		LOG(1, "heap initialization failed");
		goto error_unmap;
	}

	cache->index = vmcache_index_new();
	if (cache->index == NULL) {
		LOG(1, "indexing structure initialization failed");
		goto error_destroy_heap;
	}

	if (repl_p_init(&cache->repl, replacement_policy)) {
		LOG(1, "replacement policy initialization failed");
		goto error_destroy_index;
	}

	return cache;

error_destroy_index:
	vmcache_index_delete(cache->index);
error_destroy_heap:
	vmcache_heap_destroy(cache->heap);
error_unmap:
	util_unmap(cache->addr, cache->size);
error_free_cache:
	Free(cache);
	return NULL;
}

/*
 * vmemcache_delete -- destroy a vmemcache
 */
void
vmemcache_delete(VMEMcache *cache)
{
	repl_p_destroy(&cache->repl);
	vmcache_index_delete(cache->index);
	vmcache_heap_destroy(cache->heap);
	util_unmap(cache->addr, cache->size);
	Free(cache);
}

/*
 * vmemcache_populate_fragments -- (internal) copies content of value
 *                                  to heap entries
 */
static void
vmemcache_populate_fragments(struct cache_entry *entry,
				const char *value, size_t value_size)
{
	struct heap_entry he;
	size_t size_left = value_size;

	VEC_FOREACH(he, &entry->value.fragments) {
		ASSERT(size_left > 0);
		size_t len = (he.size < size_left) ? he.size : size_left;
		memcpy(he.ptr, value, len);
		value += len;
		size_left -= len;
	}

	entry->value.vsize = value_size;
}

/*
 * vmemcache_put -- put an element into the vmemcache
 */
int
vmemcache_put(VMEMcache *cache, const char *key, size_t ksize,
				const char *value, size_t value_size)
{
	struct cache_entry *entry;
	struct heap_entry he;

	entry = Zalloc(sizeof(struct cache_entry) + ksize);
	if (entry == NULL) {
		ERR("!Zalloc");
		return -1;
	}

	entry->key.ksize = ksize;
	memcpy(entry->key.key, key, ksize);

	size_t left_to_allocate = value_size;

	while (left_to_allocate > 0) {
		he = vmcache_alloc(cache->heap, left_to_allocate);
		if (HEAP_ENTRY_IS_NULL(he)) {
			if (vmemcache_evict(cache, NULL, 0)) {
				LOG(1, "vmemcache_evict() failed");
				goto error_exit;
			}
			continue;
		}

		if (VEC_PUSH_BACK(&entry->value.fragments, he)) {
			LOG(1, "out of memory");
			goto error_exit;
		}

		if (left_to_allocate <= he.size)
			break;

		left_to_allocate -= he.size;
	}

	vmemcache_populate_fragments(entry, value, value_size);

	if (vmcache_index_insert(cache->index, entry)) {
		LOG(1, "inserting to the index failed");
		goto error_exit;
	}

	entry->value.p_entry =
		cache->repl.ops->repl_p_insert(cache->repl.head, entry,
						&entry->value.p_entry);

	return 0;

error_exit:
	VEC_FOREACH(he, &entry->value.fragments) {
		vmcache_free(cache->heap, he);
	}

	VEC_DELETE(&entry->value.fragments);
	Free(entry);

	return -1;
}

/*
 * vmemcache_populate_value -- (internal) copies content of heap entries
 *                              to the output value's buffer 'vbuf' starting
 *                              from the 'offset'
 */
static size_t
vmemcache_populate_value(char *vbuf, size_t vbufsize, size_t offset,
				struct cache_entry *entry)
{
	struct heap_entry he;
	size_t copied = 0;
	size_t left_to_copy = entry->value.vsize;

	VEC_FOREACH(he, &entry->value.fragments) {
		if (offset > he.size) {
			offset -= he.size;
			continue;
		}

		size_t off = 0;
		size_t len = he.size;

		if (offset > 0) {
			off += offset;
			len -= offset;
			offset = 0;
		}

		if (len > vbufsize)
			len = vbufsize;

		if (len > left_to_copy)
			len = left_to_copy;

		memcpy(vbuf, (char *)he.ptr + off, len);

		vbufsize -= len;
		vbuf += len;
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
struct cache_entry *
vmemcache_entry_acquire(struct cache_entry *entry)
{
	if (__sync_fetch_and_add(&entry->value.refcount, 1) == 0) {
		entry->value.refcount--;
		return NULL;
	}

	return entry;
}

/*
 * vmemcache_entry_release -- release or delete the vmemcache entry
 */
void
vmemcache_entry_release(VMEMcache *cache, struct cache_entry *entry)
{
	if (__sync_fetch_and_sub(&entry->value.refcount, 1) != 1)
		return;

	/* 'refcount' equals 0 now - it means that the entry should be freed */

	struct heap_entry he;
	VEC_FOREACH(he, &entry->value.fragments) {
		vmcache_free(cache->heap, he);
	}

	VEC_DELETE(&entry->value.fragments);
	Free(entry);
}

/*
 * vmemcache_get - get an element from the vmemcache,
 *                 returns the number of bytes read
 */
ssize_t
vmemcache_get(VMEMcache *cache, const char *key, size_t ksize, void *vbuf,
		size_t vbufsize, size_t offset, size_t *vsize)
{
	struct cache_entry *entry;

	int ret = vmcache_index_get(cache->index, key, ksize, &entry);
	if (ret < 0)
		return -1;

	if (entry == NULL) { /* cache miss */
		if (cache->on_miss == NULL ||
		    (*cache->on_miss)(cache, key, ksize, cache->arg_miss) == 0)
			return 0;

		ret = vmcache_index_get(cache->index, key, ksize, &entry);
		if (ret < 0)
			return -1;

		if (entry == NULL)
			return 0;
	}

	struct repl_p_entry *repl_entry = entry->value.p_entry;
	if (repl_entry != NULL)
		cache->repl.ops->repl_p_use(cache->repl.head, repl_entry);

	size_t read = vmemcache_populate_value(vbuf, vbufsize, offset, entry);
	*vsize = entry->value.vsize;

	vmemcache_entry_release(cache, entry);

	return (ssize_t)read;
}

/*
 * vmemcache_evict -- evict an element from the vmemcache
 */
int
vmemcache_evict(VMEMcache *cache, const char *key, size_t ksize)
{
	struct cache_entry *entry = NULL;
	int evicted_from_repl_p = 0;

	if (key == NULL) {
		entry = cache->repl.ops->repl_p_evict(cache->repl.head, NULL);
		if (entry == NULL) {
			ERR("no element to evict");
			errno = EINVAL;
			return -1;
		}

		evicted_from_repl_p = 1;
		key = entry->key.key;
		ksize = entry->key.ksize;

	} else {
		int ret = vmcache_index_get(cache->index, key, ksize, &entry);
		if (ret < 0)
			return -1;

		if (entry == NULL) {
			ERR(
				"vmemcache_evict: cannot find an element with the given key");
			errno = EINVAL;
			return -1;
		}
	}

	if (!__sync_bool_compare_and_swap(&entry->value.evicting, 0, 1)) {
		ERR(
			"vmemcache_evict: the element with the given key is being evicted just now!");
		errno = EINVAL;
		goto exit_release;
	}

	if (cache->on_evict != NULL)
		(*cache->on_evict)(cache, key, ksize, cache->arg_evict);

	if (!evicted_from_repl_p) {
		struct repl_p_entry *repl_entry = entry->value.p_entry;
		if (repl_entry != NULL) {
			cache->repl.ops->repl_p_evict(cache->repl.head,
							repl_entry);
			/* release the reference from the replacement policy */
			vmemcache_entry_release(cache, entry);
		}
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
	cache->on_miss = miss;
	cache->arg_miss = arg;
}

#ifndef _WIN32
/*
 * vmemcache_new -- create a vmemcache
 */
VMEMcache *
vmemcache_new(const char *path, size_t max_size, size_t fragment_size,
		enum vmemcache_replacement_policy replacement_policy)
{
	return vmemcache_newU(path, max_size, fragment_size,
				replacement_policy);
}
#else
/*
 * vmemcache_newW -- create a vmemcache
 */
VMEMcache *
vmemcache_newW(const wchar_t *path, size_t max_size, size_t fragment_size,
		enum vmemcache_replacement_policy replacement_policy)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL)
		return NULL;

	VMEMcache *ret = vmemcache_newU(upath, path, max_size, fragment_size,
					replacement_policy);

	util_free_UTF8(upath);
	return ret;
}
#endif
