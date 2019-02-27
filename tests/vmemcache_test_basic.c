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
 * vmemcache_test_basic.c -- basic unit test for libvmemcache
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libvmemcache.h"
#include "test_helpers.h"

#define VMEMCACHE_FRAGMENT 16
#define LEN (VMEMCACHE_FRAGMENT)
#define KSIZE LEN /* key size */
#define VSIZE LEN /* value size */
#define DNUM 10 /* number of data */

#define SIZE_1K 1024

/* type of statistics */
typedef unsigned long long stat_t;

/* names of statistics */
static const char *stat_str[VMEMCACHE_STATS_NUM] = {
	"PUTs",
	"GETs",
	"HITs",
	"MISSes",
	"EVICTs",
	"CACHE_ENTRIES",
	"DRAM_SIZE_USED",
	"POOL_SIZE_USED"
};

/* context of callbacks */
struct ctx_cb {
	char vbuf[VSIZE];
	size_t vbufsize;
	size_t vsize;
	stat_t miss_count;
	stat_t evict_count;
};

/* key bigger than 1kB */
struct big_key {
	char buf[SIZE_1K];
	stat_t n_puts;
};

/*
 * verify_stats -- (internal) verify statistics
 */
static void
verify_stats(VMEMcache *cache, stat_t put, stat_t get, stat_t hit, stat_t miss,
		stat_t evict, stat_t entries, stat_t dram, stat_t pool)
{
	stat_t stat;
	int ret;

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_PUT,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != put)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_PUT], stat, put);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_GET,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != get)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_GET], stat, get);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_HIT,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != hit)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_HIT], stat, hit);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_MISS,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != miss)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_MISS], stat, miss);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_EVICT,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != evict)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_EVICT], stat, evict);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_ENTRIES,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != entries)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_ENTRIES], stat, entries);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_DRAM_SIZE_USED,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != dram)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_DRAM_SIZE_USED], stat, dram);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_POOL_SIZE_USED,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != pool)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_POOL_SIZE_USED], stat, pool);

	ret = vmemcache_get_stat(cache, VMEMCACHE_STATS_NUM,
					&stat, sizeof(stat));
	if (ret != -1)
		UT_FATAL(
			"vmemcache_get_stat() succeeded for incorrect statistic (-1)");
}

/*
 * verify_stat_entries -- (internal) verify the statistic
 *                                   'current number of cache entries'
 */
static void
verify_stat_entries(VMEMcache *cache, stat_t entries)
{
	stat_t stat;
	int ret;

	ret = vmemcache_get_stat(cache, VMEMCACHE_STAT_ENTRIES,
			&stat, sizeof(stat));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
	if (stat != entries)
		UT_FATAL(
			"vmemcache_get_stat: wrong statistic's (%s) value: %llu (should be %llu)",
			stat_str[VMEMCACHE_STAT_ENTRIES], stat, entries);
}

/*
 * test_new_delete -- (internal) test _new() and _delete()
 */
static void
test_new_delete(const char *dir, const char *file,
		enum vmemcache_replacement_policy replacement_policy)
{
	VMEMcache *cache;

	/* TEST #1 - minimum values of max_size and fragment_size */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache == NULL)
		UT_FATAL("vmemcache_new: %s", vmemcache_errormsg());

	vmemcache_delete(cache);

	/* TEST #2 - fragment_size = max_size = VMEMCACHE_MIN_POOL */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_POOL,
				replacement_policy);
	if (cache == NULL)
		UT_FATAL("vmemcache_new: %s", vmemcache_errormsg());

	vmemcache_delete(cache);

	/* TEST #3 - fragment_size == 0 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, 0,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL("vmemcache_new did not fail with fragment_size == 0");

	/* TEST #4 - fragment_size == -1 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, (size_t)-1,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL("vmemcache_new did not fail with fragment_size == -1");

	/* TEST #5 - fragment_size == VMEMCACHE_MIN_FRAG - 1 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG - 1,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with fragment_size == VMEMCACHE_MIN_FRAG - 1");

	/* TEST #6 - fragment_size == max_size + 1 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_POOL + 1,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with fragment_size == max_size + 1");

	/* TEST #7 - size == VMEMCACHE_MIN_POOL - 1 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL - 1, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with size == VMEMCACHE_MIN_POOL - 1");

	/* TEST #8 - size == 0 */
	cache = vmemcache_new(dir, 0, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with size == 0");

	/* TEST #9 - size == -1 */
	cache = vmemcache_new(dir, (size_t)-1, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with size == -1");

	/* TEST #10 - not a directory, but a file */
	cache = vmemcache_new(file, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with a file instead of a directory");

	/* TEST #11 - NULL directory path */
	cache = vmemcache_new(NULL, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with a NULL directory path");

	/* TEST #12 - nonexistent directory path */
	char nonexistent[PATH_MAX];
	strcpy(nonexistent, dir);
	strcat(nonexistent, "/nonexistent_dir");
	cache = vmemcache_new(nonexistent, VMEMCACHE_MIN_POOL,
				VMEMCACHE_MIN_FRAG, replacement_policy);
	if (cache != NULL)
		UT_FATAL(
			"vmemcache_new did not fail with a nonexistent directory path");
}

/*
 * test_put_get_evict -- (internal) test _put(), _get() and _evict()
 */
static void
test_put_get_evict(const char *dir,
			enum vmemcache_replacement_policy replacement_policy)
{
	VMEMcache *cache;

	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_FRAGMENT,
				replacement_policy);
	if (cache == NULL)
		UT_FATAL("vmemcache_new: %s", vmemcache_errormsg());

	const char *key = "KEY";
	size_t key_size = strlen(key) + 1;
	const char *value = "VALUE";
	size_t value_size = strlen(value) + 1;

	if (vmemcache_put(cache, key, key_size, value, value_size))
		UT_FATAL("vmemcache_put: %s", vmemcache_errormsg());

	verify_stat_entries(cache, 1);

	char vbuf[VMEMCACHE_FRAGMENT]; /* user-provided buffer */
	size_t vbufsize = VMEMCACHE_FRAGMENT; /* size of vbuf */
	size_t vsize = 0; /* real size of the object */
	ssize_t ret;

	/* get the only one element */
	ret = vmemcache_get(cache, key, key_size, vbuf, vbufsize, 0, &vsize);
	if (ret < 0)
		UT_FATAL("vmemcache_get: %s", vmemcache_errormsg());

	if ((size_t)ret != value_size)
		UT_FATAL(
			"vmemcache_get: wrong return value: %zi (should be %zu)",
			ret, value_size);

	if (vsize != value_size)
		UT_FATAL(
			"vmemcache_get: wrong size of value: %zi (should be %zu)",
			vsize, value_size);

	if (strncmp(vbuf, value, vsize))
		UT_FATAL("vmemcache_get: wrong value: %s (should be %s)",
			vbuf, value);

	/* evict the only one element */
	switch (replacement_policy) {
	case VMEMCACHE_REPLACEMENT_NONE:
		ret = vmemcache_evict(cache, key, key_size);
		break;
	case VMEMCACHE_REPLACEMENT_LRU:
		ret = vmemcache_evict(cache, NULL, 0);
		break;
	default:
		UT_FATAL("unknown policy: %u", replacement_policy);
		break;
	}

	if (ret == -1)
		UT_FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	/* getting the evicted element should return 0 (no such element) */
	ret = vmemcache_get(cache, key, key_size, vbuf, vbufsize, 0, &vsize);
	if (ret != 0)
		UT_FATAL("vmemcache_get did not return 0 (no such element)");

	vmemcache_delete(cache);
}

/*
 * on_evict_test_evict_cb -- (internal) 'on evict' callback for test_evict
 */
static void
on_evict_test_evict_cb(VMEMcache *cache, const void *key, size_t key_size,
		void *arg)
{
	struct ctx_cb *ctx = arg;
	ssize_t ret;

	ctx->evict_count++;

	ret = vmemcache_get(cache, key, key_size, ctx->vbuf, ctx->vbufsize, 0,
				&ctx->vsize);
	if (ret < 0)
		UT_FATAL("vmemcache_get");

	if ((size_t)ret != VSIZE)
		UT_FATAL(
			"vmemcache_get: wrong return value: %zi (should be %i)",
			ret, VSIZE);
}

/*
 * on_miss_test_evict_cb -- (internal) 'on miss' callback for test_evict
 */
static int
on_miss_test_evict_cb(VMEMcache *cache, const void *key, size_t key_size,
		void *arg)
{
	struct ctx_cb *ctx = arg;

	ctx->miss_count++;

	size_t size = (key_size <= ctx->vbufsize) ? key_size : ctx->vbufsize;

	memcpy(ctx->vbuf, key, size);
	ctx->vsize = size;

	return 1;
}

/*
 * test_evict -- (internal) test _evict()
 */
static void
test_evict(const char *dir,
		enum vmemcache_replacement_policy replacement_policy)
{
	VMEMcache *cache;
	char vbuf[VSIZE];
	size_t vsize = 0;
	ssize_t ret;

	struct ctx_cb ctx = {"", VSIZE, 0, 0, 0};

	struct kv {
		char key[KSIZE];
		char value[VSIZE];
	} data[DNUM];

	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_FRAGMENT,
				replacement_policy);
	if (cache == NULL)
		UT_FATAL("vmemcache_new: %s", vmemcache_errormsg());

	vmemcache_callback_on_evict(cache, on_evict_test_evict_cb, &ctx);
	vmemcache_callback_on_miss(cache, on_miss_test_evict_cb, &ctx);

	for (int i = 0; i < DNUM; i++) {
		data[i].key[0] = 'k';
		memset(&data[i].key[1], '0' + i, KSIZE - 2);
		data[i].key[KSIZE - 1] = 0;

		data[i].value[0] = 'v';
		memset(&data[i].value[1], '0' + i, VSIZE - 2);
		data[i].value[VSIZE - 1] = 0;

		if (vmemcache_put(cache, data[i].key, KSIZE,
					data[i].value, VSIZE))
			UT_FATAL("vmemcache_put: %s", vmemcache_errormsg());
	}

	verify_stat_entries(cache, DNUM);

	/* TEST #1 - evict the element with index #5 */
	ret = vmemcache_evict(cache, data[5].key, KSIZE);
	if (ret == -1)
		UT_FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	if (ctx.vsize != VSIZE)
		UT_FATAL(
			"vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted element is #5 */
	if (strncmp(ctx.vbuf, data[5].value, ctx.vsize))
		UT_FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[5].value);

	/* TEST #2 - evict the LRU element */
	ret = vmemcache_evict(cache, NULL, 0);
	if (ret == -1)
		UT_FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	if (ctx.vsize != VSIZE)
		UT_FATAL(
			"vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted LRU element is #0 */
	if (strncmp(ctx.vbuf, data[0].value, ctx.vsize))
		UT_FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[0].value);

	/* TEST #3 - get the element with index #1 (to change LRU one to #2) */
	ret = vmemcache_get(cache, data[1].key, KSIZE, vbuf, VSIZE,
			0, &vsize);
	if (ret < 0)
		UT_FATAL("vmemcache_get");

	if ((size_t)ret != VSIZE)
		UT_FATAL(
			"vmemcache_get: wrong return value: %zi (should be %i)",
			ret, VSIZE);

	if (vsize != VSIZE)
		UT_FATAL(
			"vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the got element is #1 */
	if (strncmp(vbuf, data[1].value, vsize))
		UT_FATAL("vmemcache_get: wrong value: %s (should be %s)",
			vbuf, data[1].value);

	/* TEST #4 - evict the LRU element (it should be #2 now) */
	ret = vmemcache_evict(cache, NULL, 0);
	if (ret == -1)
		UT_FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	if (ctx.vsize != VSIZE)
		UT_FATAL(
			"vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted LRU element is #2 */
	if (strncmp(ctx.vbuf, data[2].value, ctx.vsize))
		UT_FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[2].value);

	/* TEST #5 - get the evicted element with index #2 */
	ret = vmemcache_get(cache, data[2].key, KSIZE, vbuf, VSIZE,
			0, &vsize);
	if (ret == -1)
		UT_FATAL("vmemcache_get");

	if (ret != 0)
		UT_FATAL(
			"vmemcache_get: wrong return value: %zi (should be %i)",
			ret, 0);

	if (vsize != VSIZE)
		UT_FATAL(
			"vmemcache_get: wrong size of value: %zi (should be %i)",
			vsize, VSIZE);

	/* check if the 'on_miss' callback got key #2 */
	if (strncmp(ctx.vbuf, data[2].key, ctx.vsize))
		UT_FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[2].key);

	/* TEST #6 - null output arguments */
	vmemcache_get(cache, data[2].key, KSIZE, NULL, VSIZE, 0, NULL);

	/* free all the memory */
	while (vmemcache_evict(cache, NULL, 0) == 0)
		;

	/* check statistics */
	verify_stats(cache,
			DNUM, /* put */
			3 + ctx.evict_count, /* get */
			3 + ctx.evict_count - ctx.miss_count, /* hit */
			ctx.miss_count, ctx.evict_count, 0, 0, 0);

	vmemcache_delete(cache);
}

/*
 * on_evict_test_memory_leaks_cb -- (internal) 'on evict' callback for
 *                                   test_memory_leaks
 */
static void
on_evict_test_memory_leaks_cb(VMEMcache *cache,
				const void *key, size_t key_size, void *arg)
{
	stat_t *counter = arg;

	(*counter)++;
}

/*
 * test_memory_leaks -- (internal) test if there are any memory leaks
 */
static void
test_memory_leaks(const char *dir, int key_gt_1K,
			enum vmemcache_replacement_policy replacement_policy)
{
	VMEMcache *cache;
	char *buff;
	size_t size;
	int ret;

	srand((unsigned)time(NULL));

	stat_t n_puts = 0;
	stat_t n_evicts = 0;

	size_t min_size = VMEMCACHE_MIN_FRAG / 2;
	size_t max_size = VMEMCACHE_MIN_POOL / 16;

	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache == NULL)
		UT_FATAL("vmemcache_new: %s", vmemcache_errormsg());

	vmemcache_callback_on_evict(cache, on_evict_test_memory_leaks_cb,
					&n_evicts);

	while (n_evicts < 1000) {
		size = min_size + (size_t)rand() % (max_size - min_size + 1);
		buff = malloc(size);
		if (buff == NULL)
			UT_FATAL("out of memory");

		if (key_gt_1K) {
			struct big_key bk;
			memset(bk.buf, 42 /* arbitrary */, sizeof(bk.buf));
			bk.n_puts = n_puts;

			ret = vmemcache_put(cache, &bk, sizeof(bk), buff, size);
		} else {
			ret = vmemcache_put(cache, &n_puts, sizeof(n_puts),
						buff, size);
		}

		if (ret)
			UT_FATAL(
				"vmemcache_put(n_puts: %llu n_evicts: %llu): %s",
				n_puts, n_evicts, vmemcache_errormsg());
		n_puts++;

		free(buff);
	}

	verify_stat_entries(cache, n_puts - n_evicts);

	/* free all the memory */
	while (vmemcache_evict(cache, NULL, 0) == 0)
		;

	/* check statistics */
	verify_stats(cache, n_puts, 0, 0, 0, n_evicts, 0, 0, 0);

	vmemcache_delete(cache);

	if (n_evicts != n_puts)
		UT_FATAL("memory leak detected");
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s dir-name\n", argv[0]);
		exit(-1);
	}

	const char *dir = argv[1];

	test_new_delete(dir, argv[0], VMEMCACHE_REPLACEMENT_NONE);
	test_new_delete(dir, argv[0], VMEMCACHE_REPLACEMENT_LRU);

	test_put_get_evict(dir, VMEMCACHE_REPLACEMENT_NONE);
	test_put_get_evict(dir, VMEMCACHE_REPLACEMENT_LRU);

	test_evict(dir, VMEMCACHE_REPLACEMENT_LRU);

	/* '0' means: key size < 1kB */
	test_memory_leaks(dir, 0, VMEMCACHE_REPLACEMENT_LRU);

	/* '1' means: key size > 1kB */
	test_memory_leaks(dir, 1, VMEMCACHE_REPLACEMENT_LRU);

	return 0;
}
