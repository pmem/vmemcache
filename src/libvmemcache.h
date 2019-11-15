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
 * libvmemcache.h -- definitions of libvmemcache entry points
 *
 * This library provides near-zero waste volatile caching.
 */

#ifndef LIBVMEMCACHE_H
#define LIBVMEMCACHE_H 1

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32

#ifndef PMDK_UTF8_API

#define vmemcache_new vmemcache_newW
#define vmemcache_errormsg vmemcache_errormsgW

#else

#define vmemcache_new vmemcache_newU
#define vmemcache_errormsg vmemcache_errormsgU

#endif

#endif

/*
 * VMEMCACHE_MAJOR_VERSION and VMEMCACHE_MINOR_VERSION provide the current
 * version of the libvmemcache API as provided by this header file.
 */
#define VMEMCACHE_MAJOR_VERSION 0
#define VMEMCACHE_MINOR_VERSION 8

#define VMEMCACHE_MIN_POOL ((size_t)(1024 * 1024)) /* minimum pool size: 1MB */
#define VMEMCACHE_MIN_EXTENT ((size_t)256) /* minimum size of extent: 256B */

/*
 * opaque type, internal to libvmemcache
 */
typedef struct vmemcache VMEMcache;

enum vmemcache_repl_p {
	VMEMCACHE_REPLACEMENT_NONE,
	VMEMCACHE_REPLACEMENT_LRU,

	VMEMCACHE_REPLACEMENT_NUM
};

enum vmemcache_statistic {
	VMEMCACHE_STAT_PUT,		/* total number of puts */
	VMEMCACHE_STAT_GET,		/* total number of gets */
	VMEMCACHE_STAT_HIT,		/* total number of hits */
	VMEMCACHE_STAT_MISS,		/* total number of misses */
	VMEMCACHE_STAT_EVICT,		/* total number of evicts */
	VMEMCACHE_STAT_ENTRIES,		/* current number of cache entries */
	VMEMCACHE_STAT_DRAM_SIZE_USED,	/* current size of DRAM used for keys */
	VMEMCACHE_STAT_POOL_SIZE_USED,	/* current size of memory pool */
					/*    used for values */
	VMEMCACHE_STAT_HEAP_ENTRIES,	/* current number of allocator heap */
					/*    entries */
	VMEMCACHE_STATS_NUM		/* total number of statistics */
};

enum vmemcache_bench_cfg {
	/* these will corrupt the data, good only for benchmarking */
	VMEMCACHE_BENCH_INDEX_ONLY,	/* disable anything but indexing */
	VMEMCACHE_BENCH_NO_ALLOC,	/* index+repl but no alloc */
	VMEMCACHE_BENCH_NO_MEMCPY,	/* alloc but don't copy data */
	VMEMCACHE_BENCH_PREFAULT,	/* prefault the whole pool */
};

typedef void vmemcache_on_evict(VMEMcache *cache,
	const void *key, size_t key_size, void *arg);

typedef void vmemcache_on_miss(VMEMcache *cache,
	const void *key, size_t key_size, void *arg);

VMEMcache *
vmemcache_new(void);

int vmemcache_set_eviction_policy(VMEMcache *cache,
	enum vmemcache_repl_p repl_p);
int vmemcache_set_size(VMEMcache *cache, size_t size);
int vmemcache_set_extent_size(VMEMcache *cache, size_t extent_size);

#ifndef _WIN32
int vmemcache_add(VMEMcache *cache, const char *path);
#else
int vmemcache_addU(VMEMcache *cache, const char *path);
int vmemcache_addW(VMEMcache *cache, const wchar_t *path);
#endif

void vmemcache_delete(VMEMcache *cache);

void vmemcache_callback_on_evict(VMEMcache *cache,
	vmemcache_on_evict *evict, void *arg);
void vmemcache_callback_on_miss(VMEMcache *cache,
	vmemcache_on_miss *miss, void *arg);

ssize_t /* returns the number of bytes read */
vmemcache_get(VMEMcache *cache,
	const void *key, size_t key_size,
	void *vbuf, /* user-provided buffer */
	size_t vbufsize, /* size of vbuf */
	size_t offset, /* offset inside of value from which to begin copying */
	size_t *vsize /* real size of the object */);

int vmemcache_exists(VMEMcache *cache,
	const void *key, size_t key_size);

int vmemcache_put(VMEMcache *cache,
	const void *key, size_t key_size,
	const void *value, size_t value_size);

int vmemcache_evict(VMEMcache *cache, const void *key, size_t ksize);

int vmemcache_get_stat(VMEMcache *cache,
	enum vmemcache_statistic stat,
	void *value,
	size_t value_size);

#ifndef _WIN32
const char *vmemcache_errormsg(void);
#else
const char *vmemcache_errormsgU(void);
const wchar_t *vmemcache_errormsgW(void);
#endif

/* UNSTABLE INTEFACE -- DO NOT USE! */
void vmemcache_bench_set(VMEMcache *cache, enum vmemcache_bench_cfg cfg,
	size_t val);

#ifdef __cplusplus
}
#endif
#endif	/* libvmemcache.h */
