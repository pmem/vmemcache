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
#include "vmemcache_repl.h"
#include "ravl.h"

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

	return NULL;
}

/*
 * vmemcache_delete -- destroy a vmemcache
 */
void
vmemcache_delete(VMEMcache *cache)
{
}

/*
 * vmemcache_put -- put an element into the vmemcache
 */
int
vmemcache_put(VMEMcache *cache, const char *key, size_t ksize,
				const char *value, size_t value_size)
{
	return 0;
}

/*
 * vmemcache_entry_acquire -- acquire pointer to the vmemcache entry
 */
struct cache_entry *
vmemcache_entry_acquire(struct cache_entry *entry)
{
	return entry;
}

/*
 * vmemcache_entry_release -- release or delete the vmemcache entry
 */
void
vmemcache_entry_release(VMEMcache *cache, struct cache_entry *entry)
{
}

/*
 * vmemcache_get - get an element from the vmemcache,
 *                 returns the number of bytes read
 */
ssize_t
vmemcache_get(VMEMcache *cache, const char *key, size_t ksize, void *vbuf,
		size_t vbufsize, size_t offset, size_t *vsize)
{
	return 0;
}

/*
 * vmemcache_evict -- evict an element from the vmemcache
 */
int
vmemcache_evict(VMEMcache *cache, const char *key, size_t ksize)
{
	return 0;
}

/*
 * vmemcache_callback_on_evict -- install the 'on evict' callback
 */
void
vmemcache_callback_on_evict(VMEMcache *cache, vmemcache_on_evict *evict,
				void *arg)
{
}

/*
 * vmemcache_callback_on_miss -- install the 'on miss' callback
 */
void
vmemcache_callback_on_miss(VMEMcache *cache, vmemcache_on_miss *miss,
				void *arg)
{
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
