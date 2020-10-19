// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2019, Intel Corporation */

#include <stdio.h>
#include <stdlib.h>
#include <libvmemcache.h>
#include <sys/mman.h>
#include <string.h>

#define ERR(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define SIZE_MB		(1024 * 1024ULL)
#define L1_CAPACITY	(1 * SIZE_MB)
#define L2_CAPACITY	(10 * SIZE_MB)
#define ZSIZE		(SIZE_MB / 2)

static void
evict_demote(VMEMcache *cache, const void *key, size_t key_size, void *arg)
{
	VMEMcache *colder = (VMEMcache *)arg;

	size_t vsize;
	/* First, obtain the value's size. */
	if (vmemcache_get(cache, key, key_size, NULL, 0, 0, &vsize))
		return; /* Somehow got deleted? -- can't happen. */
	void *buf = malloc(vsize);
	if (!buf)
		return;
	/* Then, fetch the value. */
	if (vmemcache_get(cache, key, key_size, buf, vsize, 0, NULL) ==
		(ssize_t)vsize) {
		/* Again, it's not supposed to be missing. */
		vmemcache_put(colder, key, key_size, buf, vsize);
	}
	free(buf);
}

static void
miss_promote(VMEMcache *cache, const void *key, size_t key_size, void *arg)
{
	VMEMcache *colder = (VMEMcache *)arg;

	size_t vsize;
	if (vmemcache_get(colder, key, key_size, NULL, 0, 0, &vsize)) {
		/*
		 * Second-level cache miss.
		 *
		 * You may want to handle it somehow here.
		 */
		return;
	}
	void *buf = malloc(vsize);
	if (!buf)
		return;
	if (vmemcache_get(colder, key, key_size, buf, vsize, 0, NULL) ==
		(ssize_t)vsize) {
		/*
		 * Note that there's no lock, thus our entry may disappear
		 * between these two get() calls.
		 */
		if (!vmemcache_put(cache, key, key_size, buf, vsize)) {
			/*
			 * Put can legitimately fail: value too big for
			 * upper-level cache, no space because all evictable
			 * keys are busy, etc.
			 *
			 * The promotion likely cascades into one or more
			 * demotions to migrate cold keys downwards, to make
			 * space.
			 */
			/*
			 * You may or may not want to evict from cold cache
			 * here.
			 */
			vmemcache_evict(colder, key, key_size);
		}
	}
	free(buf);
}

static void
get(VMEMcache *cache, const char *x, int expfail)
{
	ssize_t ret = vmemcache_get(cache, x, strlen(x) + 1, NULL, 0, 0, NULL);
	if ((!ret) == expfail) {
		ERR("get(“%s”) %s when it shouldn't\n", x, expfail ?
			"succeeded" : "failed");
	}
}

int
main(int argc, const char **argv)
{
	if (argc != 3)
		ERR("Usage: twolevel <L1dir> <L2dir>\n");

	VMEMcache *pmem = vmemcache_new();
	VMEMcache *dram = vmemcache_new();
	if (!pmem || !dram)
	    ERR("VMEMcache_new failed\n");

	vmemcache_set_size(pmem, L2_CAPACITY);
	vmemcache_set_size(dram, L1_CAPACITY);

	if (vmemcache_add(pmem, argv[1]))
		ERR("vmemcache_add(“%s”) failed: %m\n", argv[1]);
	if (vmemcache_add(dram, argv[2]))
		ERR("vmemcache_add(“%s”) failed: %m\n", argv[2]);

	vmemcache_callback_on_evict(dram, evict_demote, pmem);
	vmemcache_callback_on_miss(dram, miss_promote, pmem);

	void *lotta_zeroes = mmap(NULL, ZSIZE, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (!lotta_zeroes)
		ERR("can't mmap zeroes: %m\n");

#define PUT(x)  vmemcache_put(dram, x, strlen(x) + 1, lotta_zeroes, ZSIZE)
#define GET(x)  get(dram, x, 0)
#define GETF(x) get(dram, x, 1)

	PUT("first");
	PUT("second");
	PUT("third");
	GET("first");
	GET("first");
	GET("second");
	GET("third");
	GETF("nonexistent");

	const int cap = (L1_CAPACITY / ZSIZE - 1)
		+ (L2_CAPACITY / ZSIZE - 1)
		- 1;

	for (int i = 0; i < cap; i++) {
		char buf[12];
		snprintf(buf, sizeof(buf), "%d", i);
		PUT(buf);
	}
	/* "first" and "second" should have been dropped, "third" is still in */
	GETF("first");
	GETF("second");
	GET("third");

	return 0;
}
