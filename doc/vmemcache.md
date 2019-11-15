---
layout: manual
Content-Style: 'text/css'
title: _MP(VMEMCACHE.3)
collection: vmemcache
header: VMEMCACHE
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

VMEMcache *vmemcache_new();
void vmemcache_delete(VMEMcache *cache);
int vmemcache_set_eviction_policy(VMEMcache *cache,
        enum vmemcache_repl_p repl_p);
int vmemcache_set_size(VMEMcache *cache, size_t size);
int vmemcache_set_extent_size(VMEMcache *cache, size_t extent_size);
int vmemcache_add(VMEMcache *cache, const char *path);

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

int vmemcache_exists(VMEMcache *cache,
	const void *key, size_t key_size);

int vmemcache_evict(VMEMcache *cache, const void *key, size_t ksize);

int vmemcache_get_stat(VMEMcache *cache,
	enum vmemcache_statistic stat,
	void *value, size_t value_size);

const char *vmemcache_errormsg(void);
```

# DESCRIPTION #

**libvmemcache** is a volatile key-value store optimized for operating on
NVDIMM based space, although it can work with any filesystem,
stored in memory (tmpfs) or, less performant, on some kind of a disk.


##### Creation #####

`VMEMcache *vmemcache_new();`

:   Creates an empty unconfigured vmemcache instance.

`int vmemcache_set_size(VMEMcache *cache, size_t size);`

:   Sets the size of the cache; it will be rounded **up** towards a whole page
    size alignment (4KB on x86).

`int vmemcache_set_extent_size(VMEMcache *cache, size_t extent_size);`

:   Sets block size of the cache -- 256 bytes minimum, strongly recommended
    to be a multiple of 64 bytes.  If the cache is backed by a non
    byte-addressable medium, the extent size should be 4096 (or a multiple) or
    performance will greatly suffer.

`int vmemcache_set_eviction_policy(VMEMcache *cache, enum vmemcache_repl_p repl_p);`

:   Sets what should happen on a put into a full cache.

    + **VMEMCACHE_REPLACEMENT_NONE**: manual eviction only - puts into a full
      cache will fail
    + **VMEMCACHE_REPLACEMENT_LRU**: least recently accessed entry will be evicted
      to make space when needed

`int vmemcache_add(VMEMcache *cache, const char *path);`

:   Associate the cache with a backing medium in the given *path*, which may be:

    + a `/dev/dax` device
    + a directory on a regular filesystem (which may or may not be mounted with
      -o dax, either on persistent memory or any other backing storage)

`void vmemcache_delete(VMEMcache *cache);`

:   Frees any structures associated with the cache.


##### Use #####

`ssize_t vmemcache_get(VMEMcache *cache, const void *key, size_t key_size, void *vbuf, size_t vbufsize, size_t offset, size_t *vsize);`

:   Searches for an entry with the given *key*; it doesn't have to be
    zero-terminated or be text - any sequence of bytes of length *key_size*
    is okay. If found, the entry's value is copied to *vbuf* that has space
    for *vbufsize* bytes, optionally skipping *offset* bytes at the start.
    No matter if the copy was truncated or not, its true size is stored into
    *vsize*; *vsize* remains unmodified if the key was not found.

    Return value is number of bytes successfully copied, or -1 on error.
    In particular, if there's no entry for the given *key* in the cache,
    the errno will be ENOENT.


`int vmemcache_put(VMEMcache *cache, const void *key, size_t key_size, const void *value, size_t value_size);`

:   Inserts the given key:value pair into the cache. Returns 0 on success,
    -1 on error. Inserting a key that already exists will fail with EEXIST.

`int vmemcache_exists(VMEMcache *cache, const void *key, size_t key_size);`

:   Searches for an entry with the given *key*, and returns 1 if found,
    0 if not found, and -1 if search couldn't be performed.
    This function does not impact the replacement policy or statistics.

`int vmemcache_evict(VMEMcache *cache, const void *key, size_t ksize);`

:   Removes the given key from the cache. If *key* is null and there is a
    replacement policy set, the oldest entry will be removed. Returns 0 if
    an entry has been evicted, -1 otherwise.


##### Callbacks #####

You can register a hook to be called during eviction or after a cache miss,
using **vmemcache_callback_on_evict()** or **vmemcache_callback_on_miss()**,
respectively:

`void vmemcache_callback_on_evict(VMEMcache *cache, vmemcache_on_evict *evict, void *arg);`

`void vmemcache_callback_on_miss(VMEMcache *cache, vmemcache_on_miss *miss, void *arg);`

The extra *arg* will be passed to your function.

A hook to be called during eviction has to have the following signature:

`void vmemcache_on_evict(VMEMcache *cache, const void *key, size_t key_size, void *arg);`

:   Called when an entry is being removed from the cache. The eviction can't
    be prevented, but until the callback returns, the entry remains available
    for queries. The thread that triggered the eviction is blocked in the
    meantime.

A hook to be called after a cache miss has to have the following signature:

`void vmemcache_on_miss(VMEMcache *cache, const void *key, size_t key_size, void *arg);`

:   Called when a *get* query fails, to provide an opportunity to insert the
    missing key. If the callback calls *put* for that specific key, the *get*
    will return its value, even if it did not fit into the cache.


##### Misc #####

`int vmemcache_get_stat(VMEMcache *cache, enum vmemcache_statistic stat, void *value, size_t value_size);`

:   Obtains a piece of statistics about the cache. The *stat* may be:

    + **VMEMCACHE_STAT_PUT**
	-- count of puts
    + **VMEMCACHE_STAT_GET**
	-- count of gets
    + **VMEMCACHE_STAT_HIT**
	-- count of gets that were served from the cache
    + **VMEMCACHE_STAT_MISS**
	-- count of gets that were not present in the cache
    + **VMEMCACHE_STAT_EVICT**
	-- count of evictions
    + **VMEMCACHE_STAT_ENTRIES**
	-- *current* number of cache entries (key:value pairs)
    + **VMEMCACHE_STAT_DRAM_SIZE_USED**
	-- current amount of DRAM used
    + **VMEMCACHE_STAT_POOL_SIZE_USED**
	-- current usage of data pool
    + **VMEMCACHE_STAT_HEAP_ENTRIES**
	-- current number of discontiguous unused regions (ie, free space
	fragmentation)

Statistics are enabled by default. They can be disabled at the compile time
of the vmemcache library if the **STATS_ENABLED** CMake option is set to OFF.

`const char *vmemcache_errormsg(void);`

:   Retrieves a human-friendly description of the last error.


##### Errors #####

On an error, a machine-usable description is passed in `errno`. It may be:

+ **EINVAL** -- nonsensical/invalid parameter
+ **ENOMEM** -- out of DRAM
+ **EEXIST** -- (put) entry for that key already exists
+ **ENOENT** -- (evict, get) no entry for that key
+ **ESRCH** -- (evict) could not find an evictable entry
+ **EAGAIN** -- (evict) an entry was used and could not be evicted, please try again
+ **ENOSPC** -- (create, put) not enough space in the memory pool
