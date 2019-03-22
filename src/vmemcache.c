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
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "out.h"
#include "file.h"

#include "libvmemcache.h"
#include "vmemcache.h"
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
 * vmemcache_newU -- (internal) create a vmemcache
 */
#ifndef _WIN32
static inline
#endif
VMEMcache *
vmemcache_newU(const char *dir, size_t max_size, size_t extent_size,
		enum vmemcache_replacement_policy replacement_policy)
{
	LOG(3, "dir %s max_size %zu extent_size %zu replacement_policy %d",
		dir, max_size, extent_size, replacement_policy);

	if (max_size < VMEMCACHE_MIN_POOL) {
		ERR("size %zu smaller than %zu", max_size, VMEMCACHE_MIN_POOL);
		errno = EINVAL;
		return NULL;
	}

	if (extent_size < VMEMCACHE_MIN_EXTENT) {
		ERR("extent size %zu smaller than %zu bytes",
			extent_size, VMEMCACHE_MIN_EXTENT);
		errno = EINVAL;
		return NULL;
	}

	if (extent_size > max_size) {
		ERR(
			"extent size %zu larger than maximum file size: %zu bytes",
			extent_size, max_size);
		errno = EINVAL;
		return NULL;
	}

	VMEMcache *cache = Zalloc(sizeof(VMEMcache));
	if (cache == NULL) {
		ERR("!Zalloc");
		return NULL;
	}

	cache->size = max_size;

	cache->index = vmcache_index_new();
	if (cache->index == NULL) {
		LOG(1, "indexing structure initialization failed");
		goto error_destroy_heap;
	}

	cache->repl = repl_p_init(replacement_policy);
	if (cache->repl == NULL) {
		LOG(1, "replacement policy initialization failed");
		goto error_destroy_index;
	}

	cache->dirfd = open(dir, __O_PATH|O_CLOEXEC|O_DIRECTORY);
	if (cache->dirfd == -1) {
		LOG(1, "!open(dir)");
		goto error_destroy_index;
	}

	return cache;

error_destroy_index:
	vmcache_index_delete(cache->index, vmemcache_delete_entry_cb);
error_destroy_heap:
	Free(cache);
	return NULL;
}

static __thread int
deleting_dirfd;

/*
 * vmemcache_delete_entry_cb -- callback deleting a vmemcache entry
 *                              for vmemcache_delete()
 */
void
vmemcache_delete_entry_cb(struct cache_entry *entry)
{
	char filename[20];
	snprintf(filename, sizeof(filename), "vmc%llx", entry->value.file_no);
	unlinkat(deleting_dirfd, filename, 0);
	Free(entry);
}

/*
 * vmemcache_delete -- destroy a vmemcache
 */
void
vmemcache_delete(VMEMcache *cache)
{
	deleting_dirfd = cache->dirfd;
	repl_p_destroy(cache->repl);
	vmcache_index_delete(cache->index, vmemcache_delete_entry_cb);
	close(cache->dirfd);
	Free(cache);
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

	util_fetch_and_add64(&cache->size_DRAM, malloc_usable_size(entry));

	entry->value.file_no = util_fetch_and_add64(&cache->put_count, 1);

	entry->key.ksize = ksize;
	memcpy(entry->key.key, key, ksize);

	if (cache->index_only || cache->no_alloc)
		goto put_index;

	char filename[20];
	snprintf(filename, sizeof(filename), "vmc%llx", entry->value.file_no);

	int fd = openat(cache->dirfd, filename, O_CREAT|O_TRUNC|O_EXCL
		|O_CLOEXEC|O_WRONLY, 0666);
	if (fd == -1) {
		LOG(1, "!open(write)");
		goto error_exit;
	}

	int err;
	while ((err = posix_fallocate(fd, 0, (off_t)value_size))) {
		if (vmemcache_evict(cache, NULL, 0)) {
			LOG(1, "vmemcache_evict() failed");
			if (errno == ESRCH)
				errno = ENOSPC;
			goto error_exit;
		}
	}

	entry->value.vsize = value_size;

	if (write(fd, value, value_size) != (ssize_t)value_size) {
		ERR("!write");
		int err = errno;
		close(fd);
		errno = err;
		goto error_exit;
	}

	close(fd);

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
	Free(entry);

	return -1;
}

/*
 * vmemcache_populate_value -- (internal) copies content of heap entries
 *                              to the output value's buffer 'vbuf' starting
 *                              from the 'offset'
 */
static size_t
vmemcache_populate_value(int dirfd, void *vbuf, size_t vbufsize, size_t offset,
				struct cache_entry *entry, int no_memcpy)
{
	if (!vbuf)
		return 0;

	char filename[20];
	snprintf(filename, sizeof(filename), "vmc%llx", entry->value.file_no);

	int fd = openat(dirfd, filename, O_RDONLY|O_CLOEXEC|__O_NOATIME);
	if (fd == -1) {
		LOG(1, "!open(read)");
		return 0;
	}

	ssize_t copied = pread(fd, vbuf, vbufsize, (off_t)offset);
	if (copied < 0) {
		int err = errno;
		ERR("!pread");
		close(fd);
		errno = err;
		return 0;
	}

	close(fd);

	return (size_t)copied;
}

/*
 * vmemcache_entry_acquire -- acquire pointer to the vmemcache entry
 */
void
vmemcache_entry_acquire(struct cache_entry *entry)
{
	uint64_t ret = util_fetch_and_add64(&entry->value.refcount, 1);

	ASSERTne(ret, 0);
}

/*
 * vmemcache_entry_release -- release or delete the vmemcache entry
 */
void
vmemcache_entry_release(VMEMcache *cache, struct cache_entry *entry)
{
	if (util_fetch_and_sub64(&entry->value.refcount, 1) != 1) {
		VALGRIND_ANNOTATE_HAPPENS_BEFORE(&entry->value.refcount);
		return;
	}

	/* 'refcount' equals 0 now - it means that the entry should be freed */

	VALGRIND_ANNOTATE_HAPPENS_AFTER(&entry->value.refcount);
	VALGRIND_ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(&entry->value.refcount);

	char filename[20];
	snprintf(filename, sizeof(filename), "vmc%llx", entry->value.file_no);
	unlinkat(cache->dirfd, filename, 0);

	util_fetch_and_sub64(&cache->size_DRAM, malloc_usable_size(entry));

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
	struct cache_entry *entry;
	size_t read = 0;

	util_fetch_and_add64(&cache->get_count, 1);

	int ret = vmcache_index_get(cache->index, key, ksize, &entry);
	if (ret < 0)
		return -1;

	if (entry == NULL) { /* cache miss */
		util_fetch_and_add64(&cache->miss_count, 1);

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

	read = vmemcache_populate_value(cache->dirfd, vbuf, vbufsize, offset,
		entry, cache->no_memcpy);
	if (vsize)
		*vsize = entry->value.vsize;

get_index:
	vmemcache_entry_release(cache, entry);

	return (ssize_t)read;
}

/*
 * vmemcache_evict -- evict an element from the vmemcache
 */
int
vmemcache_evict(VMEMcache *cache, const void *key, size_t ksize)
{
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
		int ret = vmcache_index_get(cache->index, key, ksize, &entry);
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

	util_fetch_and_add64(&cache->evict_count, 1);

	if (cache->on_evict != NULL)
		(*cache->on_evict)(cache, key, ksize, cache->arg_evict);

	if (!evicted_from_repl_p) {
		if (cache->repl->ops->repl_p_evict(cache->repl->head,
						&entry->value.p_entry)) {
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

/*
 * vmemcache_get_stat -- get the statistic
 */
int
vmemcache_get_stat(VMEMcache *cache, enum vmemcache_statistic stat,
			void *value, size_t value_size)
{
	if (value_size != sizeof(stat_t)) {
		ERR("wrong size of the value: %zu (should be: %zu)",
			value_size, sizeof(stat_t));
		errno = EINVAL;
		return -1;
	}

	stat_t *val = value;

	switch (stat) {
	case VMEMCACHE_STAT_PUT:
		*val = cache->put_count;
		break;
	case VMEMCACHE_STAT_GET:
		*val = cache->get_count;
		break;
	case VMEMCACHE_STAT_HIT:
		*val = cache->get_count - cache->miss_count;
		break;
	case VMEMCACHE_STAT_MISS:
		*val = cache->miss_count;
		break;
	case VMEMCACHE_STAT_EVICT:
		*val = cache->evict_count;
		break;
	case VMEMCACHE_STAT_ENTRIES:
		*val = cache->put_count - cache->evict_count;
		break;
	case VMEMCACHE_STAT_DRAM_SIZE_USED:
		*val = cache->size_DRAM;
		break;
	case VMEMCACHE_STAT_POOL_SIZE_USED:
		break;
	case VMEMCACHE_STAT_HEAP_ENTRIES:
		break;
	default:
		ERR("unknown value of statistic: %u", stat);
		errno = EINVAL;
		return -1;
	}

	return 0;
}

/*
 * vmemcache_bench_set -- alter a benchmark parameter
 */
void
vmemcache_bench_set(VMEMcache *cache, enum vmemcache_bench_cfg cfg,
				size_t val)
{
	switch (cfg) {
	case VMEMCACHE_BENCH_INDEX_ONLY:
		cache->index_only = !!val;
		break;
	case VMEMCACHE_BENCH_NO_MEMCPY:
		cache->no_memcpy = !!val;
		break;
	case VMEMCACHE_BENCH_PREFAULT:
		break;
	default:
		ERR("invalid config parameter: %u", cfg);
	}
}

#ifndef _WIN32
/*
 * vmemcache_new -- create a vmemcache
 */
VMEMcache *
vmemcache_new(const char *path, size_t max_size, size_t extent_size,
		enum vmemcache_replacement_policy replacement_policy)
{
	return vmemcache_newU(path, max_size, extent_size,
				replacement_policy);
}
#else
/*
 * vmemcache_newW -- create a vmemcache
 */
VMEMcache *
vmemcache_newW(const wchar_t *path, size_t max_size, size_t extent_size,
		enum vmemcache_replacement_policy replacement_policy)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL)
		return NULL;

	VMEMcache *ret = vmemcache_newU(upath, path, max_size, extent_size,
					replacement_policy);

	util_free_UTF8(upath);
	return ret;
}
#endif
