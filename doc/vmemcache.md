---
layout: manual
Content-Style: 'text/css'
title: VMEMCACHE
collection: vmemcache
header: PMDK
...

[NAME](#name)<br />
[SYNOPSIS](#synopsis)<br />
[DESCRIPTION](#description)<br />

[comment]: <> (Copyright 2019, Intel Corporation)

[comment]: <> (Redistribution and use in source and binary forms, with or without)
[comment]: <> (modification, are permitted provided that the following conditions)
[comment]: <> (are met:)
[comment]: <> (    * Redistributions of source code must retain the above copyright)
[comment]: <> (      notice, this list of conditions and the following disclaimer.)
[comment]: <> (    * Redistributions in binary form must reproduce the above copyright)
[comment]: <> (      notice, this list of conditions and the following disclaimer in)
[comment]: <> (      the documentation and/or other materials provided with the)
[comment]: <> (      distribution.)
[comment]: <> (    * Neither the name of the copyright holder nor the names of its)
[comment]: <> (      contributors may be used to endorse or promote products derived)
[comment]: <> (      from this software without specific prior written permission.)

[comment]: <> (THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS)
[comment]: <> ("AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT)
[comment]: <> (LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR)
[comment]: <> (A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT)
[comment]: <> (OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,)
[comment]: <> (SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT)
[comment]: <> (LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,)
[comment]: <> (DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY)
[comment]: <> (THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT)
[comment]: <> ((INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE)
[comment]: <> (OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.)

# NAME #

**vmemcache** - buffer-based LRU cache

# SYNOPSIS #

```c
#include <libvmemcache.h>

VMEMcache *vmemcache_new(const char *path, size_t max_size, size_t segment_size,
	enum vmemcache_replacement_policy replacement_policy);
void vmemcache_delete(VMEMcache *cache);

void vmemcache_callback_on_evict(VMEMcache *cache,
	vmemcache_on_evict *evict, void *arg);
void vmemcache_callback_on_miss(VMEMcache *cache,
	vmemcache_on_miss *miss, void *arg);

ssize_t vmemcache_get(VMEMcache *cache,
	const void *key, size_t key_size,
	void *vbuf, size_t vbufsize, size_t offset, size_t *vsize);

int vmemcache_put(VMEMcache *cache,
	const void *key, size_t key_size,
	const void *value, size_t value_size);

int vmemcache_evict(VMEMcache *cache, const void *key, size_t ksize);

int vmemcache_get_stat(VMEMcache *cache,
	enum vmemcache_statistic stat,
	void *value, size_t value_size);

const char *vmemcache_errormsg(void);
```

# DESCRIPTION #

**libvmemcache** is a volatile key-value store optimized for operating on
NVDIMM based space, although it can work with any filesystem, be it stored
in memory (tmpfs) or, less performant, on some kind of a disk.


##### Creation #####

```
VMEMcache *vmemcache_new(const char *path, size_t max_size, size_t segment_size,
	enum vmemcache_replacement_policy replacement_policy);
```

The cache will be created in the given *path*, which may be:
 + a `/dev/dax` device
 + a directory on a regular filesystem (which may or may not be mounted with
   -o dax, either on persistent memory or any other backing)

Its size is given as *max_size*, although it is rounded **up** towards a
whole page size alignment (4KB on x86, 64KB on ppc, 4/16/64KB on arm64).

*segment_size* affects allowed fragmentation of the cache. Reducing it
improves cache space utilization, increasing it improves performance.

*replacement_policy* may be:
 + **VMEMCACHE_REPLACEMENT_NONE**: manual eviction only - puts into a full
   cache will fail
 + **VMEMCACHE_REPLACEMENT_LRU**: least recently accessed entry will be evicted
   to make space when needed


```
void vmemcache_delete(VMEMcache *cache);
```

Frees any structures associated with the cache.


##### Use #####

```
ssize_t vmemcache_get(VMEMcache *cache,
	const void *key, size_t key_size,
	void *vbuf, size_t vbufsize, size_t offset, size_t *vsize);
```

Searches for an entry with the given *key*; it doesn't have to be
zero-terminated or be text - any sequence of bytes of length *key_size*
is okay. If found, the entry's value is copied to *vbuf* that has space
for *vbufsize* bytes, optionally skipping *offset* bytes at the start.
No matter if the copy was truncated or not, its true size is stored into
*vsize*; *vsize* remains unmodified if the key was not found.

Return value is -1 on error, 0 if the key was not found, or number of bytes
successfully copied otherwise. Note that 0 may be legitimately returned
even if the key was found (reading past end, zero-sized value, etc) -
check *vsize* to tell that apart.


```
int vmemcache_put(VMEMcache *cache,
	const void *key, size_t key_size,
	const void *value, size_t value_size);
```

Inserts the given key:value pair into the cache. Returns 0 on success,
-1 on error. Inserting a key that already exists will fail with EEXIST.


```
int vmemcache_evict(VMEMcache *cache, const void *key, size_t ksize);
```

Removes the given key from the cache. If *key* is null and there is a
replacement policy set, the oldest entry will be removed. Returns 0 if
an entry has been evicted, -1 otherwise.


##### Callbacks #####

You can register a hook to be called during eviction or after a cache miss,
using **vmemcache_callback_on_evict()** or **vmemcache_callback_on_miss()**,
respectively. The extra *arg* will be passed to your function.

```
void vmemcache_on_evict(VMEMcache *cache,
	const void *key, size_t key_size, void *arg);
```

Called when an entry is being removed from the cache. The eviction can't
be prevented, but until the callback returns, the entry remains available
for queries. The thread that triggered the eviction is blocked in the
meantime.


```
int vmemcache_on_miss(VMEMcache *cache,
	const void *key, size_t key_size, void *arg);
```

Called when a *get* query fails, to provide an opportunity to insert the
missing key. If the callback returns zero, upon return the query will
be retried (just once - no more calls if it fails again). Note that it's
possible that the entry will be evicted between the insert and get.


##### Misc #####

```
int vmemcache_get_stat(VMEMcache *cache,
	enum vmemcache_statistic stat,
	void *value, size_t value_size);
```

Obtains a piece of statistics about the cache. The *stat* may be:
 + **VMEMCACHE_STAT_PUT**
	count of puts
 + **VMEMCACHE_STAT_GET**
	count of gets
 + **VMEMCACHE_STAT_HIT**
	count of gets that received data from the cache
	FIXME - broken
 + **VMEMCACHE_STAT_MISS**
	count of gets that were not present in the cache
	FIXME - broken
	PROPOSED: this number might be distinct from gets - hits, because of
	callbacks producing data?
 + **VMEMCACHE_STAT_EVICT**
	count of evictions
 + **VMEMCACHE_STAT_ENTRIES**
	*current* number of cache entries
 + **VMEMCACHE_STAT_DRAM_SIZE_USED**
	current amount of DRAM used for keys
	CLARIFY/RENAME: doesn't include index, repl nor allocator
 + **VMEMCACHE_STAT_POOL_SIZE_USED**
	current usage of data pool


```
const char *vmemcache_errormsg(void);
```

Retrieves a human-friendly description of the last error.


##### Errors #####

On an error, a machine-usable description is passed in `errno`. It may
be:
 + **EINVAL**
	nonsensical/invalid parameter
 + **ENOMEM**
	out of DRAM
 + **EEXIST**
	(put) entry for that key already exists
 + **ENOENT**
	(evict) couldn't find an evictable entry
 + **EBUSY**
	(evict) the entry is busy and cannot be evicted
 + **ENOSPC**
	(create, put) not enough space in the memory pool
