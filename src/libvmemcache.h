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
 * libvmemcache.h -- definitions of libvmemcache entry points
 *
 * This library provides near-zero waste volatile caching.
 *
 * WARNING: this library is in a 'Work-In-Progress' state,
 *          API is not stable and it may change at any time.
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
 * Applications can verify that the version available at run-time
 * is compatible with the version used at compile-time by passing
 * these defines to vmemcache_check_version().
 */
#define VMEMCACHE_MAJOR_VERSION 0
#define VMEMCACHE_MINOR_VERSION 1

#define VMEMCACHE_MIN_POOL ((size_t)(1024 * 1024)) /* minimum pool size: 1MB */
#define VMEMCACHE_MIN_FRAG ((size_t)8) /* minimum fragment size: 8B */

/*
 * opaque type, internal to libvmemcache
 */
typedef struct vmemcache VMEMcache;

enum vmemcache_replacement_policy {
	VMEMCACHE_REPLACEMENT_NONE,
	VMEMCACHE_REPLACEMENT_LRU,

	VMEMCACHE_REPLACEMENT_NUM
};

typedef void vmemcache_on_evict(VMEMcache *cache,
	const char *key, size_t key_size, void *arg);
typedef int vmemcache_on_miss(VMEMcache *cache,
	const char *key, size_t key_size, void *arg);

#ifndef _WIN32
VMEMcache *vmemcache_new(const char *path, size_t max_size, size_t segment_size,
		enum vmemcache_replacement_policy replacement_policy);
#else
VMEMcache *vmemcache_newU(const char *path, size_t max_size,
		size_t segment_size,
		enum vmemcache_replacement_policy replacement_policy);
VMEMcache *vmemcache_newW(const wchar_t *path, size_t max_size,
		size_t segment_size,
		enum vmemcache_replacement_policy replacement_policy);
#endif

void vmemcache_delete(VMEMcache *c);

void vmemcache_callback_on_evict(VMEMcache *cache,
	vmemcache_on_evict *evict, void *arg);
void vmemcache_callback_on_miss(VMEMcache *cache,
	vmemcache_on_miss *miss, void *arg);

ssize_t /* returns the number of bytes read */
vmemcache_get(VMEMcache *cache,
	const char *key, size_t key_size,
	void *vbuf, /* user-provided buffer */
	size_t vbufsize, /* size of vbuf */
	size_t offset, /* offset inside of value from which to begin copying */
	size_t *vsize /* real size of the object */);

int vmemcache_put(VMEMcache *cache,
	const char *key, size_t key_size,
	const char *value, size_t value_size);

int vmemcache_evict(VMEMcache *cache, const char *key, size_t ksize);

#ifndef _WIN32
const char *vmemcache_errormsg(void);
#else
const char *vmemcache_errormsgU(void);
const wchar_t *vmemcache_errormsgW(void);
#endif

#ifdef __cplusplus
}
#endif
#endif	/* libvmemcache.h */
