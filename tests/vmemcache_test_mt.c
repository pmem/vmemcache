// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

/*
 * vmemcache_test_mt.c -- multi-threaded test for libvmemcache
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "libvmemcache.h"
#include "test_helpers.h"
#include "os_thread.h"

#define EVICT_BY_LRU 0
#define EVICT_BY_KEY 1

#define BUF_SIZE 256

/* type of statistics */
typedef unsigned long long stat_t;

struct buffers {
	size_t size;
	char *buff;
};

struct context {
	unsigned thread_number;
	unsigned n_threads;
	VMEMcache *cache;
	struct buffers *buffs;
	unsigned nbuffs;
	unsigned ops_count;
	void *(*worker)(void *);
};

#ifdef STATS_ENABLED
/*
 * get_stat -- (internal) get one statistic
 */
static void
get_stat(VMEMcache *cache, stat_t *stat_val, enum vmemcache_statistic i_stat)
{
	int ret = vmemcache_get_stat(cache, i_stat,
					stat_val, sizeof(*stat_val));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
}
#endif /* STATS_ENABLED */

/*
 * free_cache -- (internal) free the cache
 */
static void
free_cache(VMEMcache *cache)
{
	/* evict all entries from the cache */
	while (vmemcache_evict(cache, NULL, 0) == 0)
		;

#ifdef STATS_ENABLED
	/* verify that all memory is freed */
	stat_t entries, heap_entries, dram, pool_ued;
	get_stat(cache, &entries, VMEMCACHE_STAT_ENTRIES);
	get_stat(cache, &heap_entries, VMEMCACHE_STAT_HEAP_ENTRIES);
	get_stat(cache, &dram, VMEMCACHE_STAT_DRAM_SIZE_USED);
	get_stat(cache, &pool_ued, VMEMCACHE_STAT_POOL_SIZE_USED);

	if (entries != 0)
		UT_FATAL("%llu entries were not freed", entries);
	if (dram != 0)
		UT_FATAL("%llu bytes of DRAM memory were not freed", dram);
	if (pool_ued != 0)
		UT_FATAL("%llu bytes of pool memory were not freed", pool_ued);
	if (heap_entries != 1)
		UT_FATAL("%llu heap entries were not merged", heap_entries - 1);
#endif /* STATS_ENABLED */
}

/*
 * run_threads -- (internal) create and join threads
 */
static void
run_threads(unsigned n_threads, os_thread_t *threads, struct context *ctx)
{
	for (unsigned i = 0; i < n_threads; ++i)
		os_thread_create(&threads[i], NULL, ctx[i].worker,
					&ctx[i]);

	for (unsigned i = 0; i < n_threads; ++i)
		os_thread_join(&threads[i], NULL);
}

/*
 * worker_thread_put -- (internal) worker testing vmemcache_put()
 */
static void *
worker_thread_put(void *arg)
{
	struct context *ctx = arg;
	unsigned long long i;
	unsigned long long shift = ctx->thread_number * ctx->ops_count;

	for (i = shift; i < (shift + ctx->ops_count); i++) {
		if (vmemcache_put(ctx->cache, &i, sizeof(i),
				ctx->buffs[i % ctx->nbuffs].buff,
				ctx->buffs[i % ctx->nbuffs].size))
			UT_FATAL("ERROR: vmemcache_put: %s",
					vmemcache_errormsg());
	}

	return NULL;
}

/*
 * worker_thread_get -- (internal) worker testing vmemcache_get()
 */
static void *
worker_thread_get(void *arg)
{
	struct context *ctx = arg;
	unsigned long long i;

	char vbuf[BUF_SIZE];		/* user-provided buffer */
	size_t vbufsize = BUF_SIZE;	/* size of vbuf */
	size_t vsize = 0;		/* real size of the object */

	/* starting from 1, because the entry #0 has been evicted */
	for (i = 1; i < ctx->ops_count; i++) {
		if (vmemcache_get(ctx->cache, &i, sizeof(i),
					vbuf, vbufsize, 0, &vsize) == -1)
			UT_FATAL("ERROR: vmemcache_get: %s",
					vmemcache_errormsg());
	}

	return NULL;
}

/*
 * worker_thread_put_in_gets -- (internal) worker testing vmemcache_put()
 */
static void *
worker_thread_put_in_gets(void *arg)
{
	struct context *ctx = arg;
	unsigned long long i;
	unsigned long long start = ctx->ops_count + (ctx->thread_number & 0x1);

	/*
	 * There is '3' here - in order to have the same number (ctx->ops_count)
	 * of operations per each thread.
	 */
	unsigned long long end = 3 * ctx->ops_count;

	for (i = start; i < end; i += 2) {
		if (vmemcache_put(ctx->cache, &i, sizeof(i),
				ctx->buffs[i % ctx->nbuffs].buff,
				ctx->buffs[i % ctx->nbuffs].size))
			UT_FATAL("ERROR: vmemcache_put: %s",
					vmemcache_errormsg());
	}

	return NULL;
}

/*
 * run_test_put -- (internal) run test for vmemcache_put()
 */
static void
run_test_put(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		unsigned ops_per_thread, struct context *ctx)
{
	free_cache(cache);

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].worker = worker_thread_put;
		ctx[i].ops_count = ops_per_thread;
	}

	printf("%s: STARTED\n", __func__);

	run_threads(n_threads, threads, ctx);

	printf("%s: PASSED\n", __func__);
}

/*
 * on_evict_cb -- (internal) 'on evict' callback for run_test_get
 */
static void
on_evict_cb(VMEMcache *cache, const void *key, size_t key_size, void *arg)
{
	int *cache_is_full = arg;

	*cache_is_full = 1;
}

/*
 * init_test_get -- (internal) initialize test for vmemcache_get()
 */
static void
init_test_get(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		unsigned ops_per_thread, struct context *ctx)
{
	free_cache(cache);

	int cache_is_full = 0;
	vmemcache_callback_on_evict(cache, on_evict_cb, &cache_is_full);

	printf("%s: filling the pool...", __func__);
	fflush(stdout);

	unsigned n = 0; /* number of elements put into the cache */
	while (!cache_is_full && n < ops_per_thread) {
		unsigned long long n_key = n;
		if (vmemcache_put(cache, &n_key, sizeof(n_key),
					ctx->buffs[n % ctx->nbuffs].buff,
					ctx->buffs[n % ctx->nbuffs].size))
			UT_FATAL("ERROR: vmemcache_put: %s",
					vmemcache_errormsg());
		n++;
	}

	printf(" done (inserted %u elements)\n", n);

	vmemcache_callback_on_evict(cache, NULL, NULL);

	if (ops_per_thread > n) {
		/* we cannot get more than we have put */
		ops_per_thread = n;
		printf("%s: decreasing ops_count to: %u\n",
			__func__, n_threads * ops_per_thread);
	}

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].worker = worker_thread_get;
		ctx[i].ops_count = ops_per_thread;
	}
}

/*
 * run_test_get -- (internal) run test for vmemcache_get()
 */
static void
run_test_get(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		unsigned ops_per_thread, struct context *ctx)
{
	init_test_get(cache, n_threads, threads, ops_per_thread, ctx);

	printf("%s: STARTED\n", __func__);

	run_threads(n_threads, threads, ctx);

	printf("%s: PASSED\n", __func__);
}

static void
on_miss_cb(VMEMcache *cache, const void *key, size_t key_size, void *arg);

/*
 * run_test_get_put -- (internal) run test for vmemcache_get()
 *                      and vmemcache_put()
 */
static void
run_test_get_put(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		unsigned ops_per_thread, struct context *ctx)
{
	init_test_get(cache, n_threads, threads, ops_per_thread, ctx);

	if (n_threads < 10) {
		ctx[n_threads >> 1].worker = worker_thread_put_in_gets;
	} else {
		/* 20% of threads (in the middle of their array) are puts */
		unsigned n_puts = (2 * n_threads) / 10; /* 20% of threads */
		unsigned start = (n_threads / 2) - (n_puts / 2);
		for (unsigned i = start; i < start + n_puts; i++)
			ctx[i].worker = worker_thread_put_in_gets;
	}

	vmemcache_callback_on_miss(cache, on_miss_cb, ctx);

	printf("%s: STARTED\n", __func__);

	run_threads(n_threads, threads, ctx);

	printf("%s: PASSED\n", __func__);
}

/*
 * on_miss_cb -- (internal) 'on miss' callback for run_test_get_on_miss
 *                          and run_test_get_put
 */
static void
on_miss_cb(VMEMcache *cache, const void *key, size_t key_size, void *arg)
{
	struct context *ctx = arg;

	typedef unsigned long long key_t;
	assert(key_size == sizeof(key_t));

	key_t n = *(key_t *)key;

	int ret = vmemcache_put(ctx->cache, key, key_size,
				ctx->buffs[n % ctx->nbuffs].buff,
				ctx->buffs[n % ctx->nbuffs].size);
	if (ret && errno != EEXIST)
		UT_FATAL("ERROR: vmemcache_put: %s", vmemcache_errormsg());
}

/*
 * worker_thread_get_unique_keys -- (internal) worker testing vmemcache_get()
 *                                   with unique keys
 */
static void *
worker_thread_get_unique_keys(void *arg)
{
	struct context *ctx = arg;
	unsigned long long key;

	char vbuf[BUF_SIZE];		/* user-provided buffer */
	size_t vbufsize = BUF_SIZE;	/* size of vbuf */
	size_t vsize = 0;		/* real size of the object */

	for (unsigned i = 0; i < ctx->ops_count; i++) {
		key = ((unsigned long long)ctx->thread_number << 48) | i;
		if (vmemcache_get(ctx->cache, &key, sizeof(key),
					vbuf, vbufsize, 0, &vsize) == -1)
			UT_FATAL("ERROR: vmemcache_get: %s",
					vmemcache_errormsg());
	}

	return NULL;
}

/*
 * run_test_get_on_miss -- (internal) run test for vmemcache_get() with
 *                          vmemcache_put() called in the 'on miss' callback
 */
static void
run_test_get_on_miss(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		unsigned ops_per_thread, struct context *ctx)
{
	free_cache(cache);

	vmemcache_callback_on_miss(cache, on_miss_cb, ctx);

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].worker = worker_thread_get_unique_keys;
		ctx[i].ops_count = ops_per_thread;
	}

	printf("%s: STARTED\n", __func__);

	run_threads(n_threads, threads, ctx);

	vmemcache_callback_on_miss(cache, NULL, NULL);

#ifdef STATS_ENABLED
	stat_t puts, gets, misses;
	get_stat(cache, &puts, VMEMCACHE_STAT_PUT);
	get_stat(cache, &gets, VMEMCACHE_STAT_GET);
	get_stat(cache, &misses, VMEMCACHE_STAT_MISS);

	stat_t nops = n_threads * ops_per_thread;

	if (puts != nops)
		UT_FATAL("wrong number of puts: %llu (should be: %llu",
				puts, nops);

	if (gets != nops)
		UT_FATAL("wrong number of gets: %llu (should be: %llu",
				gets, nops);

	if (misses != nops)
		UT_FATAL("wrong number of misses: %llu (should be: %llu",
				misses, nops);
#endif /* STATS_ENABLED */

	printf("%s: PASSED\n", __func__);
}

static uint32_t keep_running;

/*
 * worker_thread_test_evict_get -- (internal) worker testing vmemcache_get()
 */
static void *
worker_thread_test_evict_get(void *arg)
{
	struct context *ctx = arg;
	unsigned long long n = ctx->thread_number;

	char vbuf;

	while (__atomic_load_n(&keep_running, __ATOMIC_SEQ_CST) &&
		vmemcache_get(ctx->cache, &n, sizeof(n),
				&vbuf, sizeof(vbuf), 0, NULL) == sizeof(vbuf))
		;

	return NULL;
}

/*
 * worker_thread_test_evict_by_LRU -- (internal) worker evicting by LRU
 */
static void *
worker_thread_test_evict_by_LRU(void *arg)
{
	struct context *ctx = arg;

	/* at least one entry has to be evicted successfully */
	if (vmemcache_evict(ctx->cache, NULL, 0))
		UT_FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	/* try to evict all other entries */
	while (vmemcache_evict(ctx->cache, NULL, 0) == 0)
		;

	__atomic_store_n(&keep_running, 0, __ATOMIC_SEQ_CST);

	return NULL;
}

/*
 * worker_thread_test_evict_by_key -- (internal) worker evicting by key
 */
static void *
worker_thread_test_evict_by_key(void *arg)
{
	struct context *ctx = arg;
	unsigned n_threads = ctx->n_threads;

	/*
	 * Try to evict all entries by key first.
	 * It is very likely that even all of these vmemcache_evict() calls
	 * will fail, because the conditions are extremely difficult (all cache
	 * entries are being constantly read (used) by separate threads),
	 * but this is acceptable, because this test is dedicated
	 * to test the failure path of vmemcache_evict()
	 * and the success criteria of this test are checks done in free_cache()
	 * at the end of the test.
	 */
	for (unsigned long long  n = 0; n < n_threads; ++n)
		vmemcache_evict(ctx->cache, &n, sizeof(n));

	/* try to evict by LRU all entries that were not evicted above */
	while (vmemcache_evict(ctx->cache, NULL, 0) == 0)
		;

	__atomic_store_n(&keep_running, 0, __ATOMIC_SEQ_CST);

	return NULL;
}

/*
 * run_test_evict -- (internal) run test for vmemcache_evict()
 *
 * This test is dedicated to test the failure path of vmemcache_evict().
 * It simulates extremely difficult conditions for an eviction:
 * all cache entries are being constantly read (used) by separate threads
 * (only one thread tries to evict entries by key or by LRU),
 * so it is very likely that most of vmemcache_evict() calls in this test
 * will fail.
 * The main success criteria of this test are checks done in free_cache()
 * at the end of the test.
 */
static void
run_test_evict(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		unsigned ops_per_thread, struct context *ctx, int by_key)
{
	free_cache(cache);

	for (unsigned long long n = 0; n < n_threads; ++n) {
		if (vmemcache_put(ctx->cache, &n, sizeof(n), &n, sizeof(n)))
			UT_FATAL("ERROR: vmemcache_put: %s",
					vmemcache_errormsg());
	}

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].worker = worker_thread_test_evict_get;
		ctx[i].ops_count = ops_per_thread;
	}

	/* overwrite the last routine */
	if (by_key)
		ctx[n_threads - 1].worker = worker_thread_test_evict_by_key;
	else
		ctx[n_threads - 1].worker = worker_thread_test_evict_by_LRU;

	printf("%s%s: STARTED\n", __func__, by_key ? "_by_key" : "_by_LRU");

	__atomic_store_n(&keep_running, 1, __ATOMIC_SEQ_CST);
	run_threads(n_threads, threads, ctx);

	/* success of this function is the main success criteria of this test */
	free_cache(cache);

	printf("%s%s: PASSED\n", __func__, by_key ? "_by_key" : "_by_LRU");
}

int
main(int argc, char *argv[])
{
	unsigned seed = 0;
	int skip = 0;
	int ret = -1;

	if (argc < 2 || argc > 6) {
		fprintf(stderr,
			"usage: %s dir-name [threads] [ops_count] [seed] ['skip']\n"
			"\t seed == 0   - set seed from time()\n"
			"\t 'skip'      - skip tests that last very long under Valgrind\n",
			argv[0]);
		exit(-1);
	}

	const char *dir = argv[1];

	/* default values of parameters */
	unsigned n_threads = 10;
	unsigned ops_count = 10000;
	unsigned nbuffs = 10;
	size_t min_size = 8;
	size_t max_size = 64;

	if (argc >= 3 &&
	    (str_to_unsigned(argv[2], &n_threads) || n_threads < 1))
		UT_FATAL("incorrect value of n_threads: %s", argv[2]);

	if (argc >= 4 &&
	    (str_to_unsigned(argv[3], &ops_count) || ops_count < 1))
		UT_FATAL("incorrect value of ops_count: %s", argv[3]);

	if (argc >= 5 &&
	    (str_to_unsigned(argv[4], &seed)))
			UT_FATAL("incorrect value of seed: %s", argv[4]);

	if (argc == 6) {
		if (strcmp(argv[5], "skip"))
			UT_FATAL("incorrect value of the 'skip' option: %s",
				argv[5]);
		skip = 1;
	}

	if (seed == 0)
		seed = (unsigned)time(NULL);

	printf("Multi-threaded test parameters:\n");
	printf("   directory           : %s\n", dir);
	printf("   n_threads           : %u\n", n_threads);
	printf("   ops_count           : %u\n", ops_count);
	printf("   nbuffs              : %u\n", nbuffs);
	printf("   min_size            : %zu\n", min_size);
	printf("   max_size            : %zu\n", max_size);
	printf("   seed                : %u\n\n", seed);

	srand(seed);

	VMEMcache *cache = vmemcache_new();
	vmemcache_set_size(cache, VMEMCACHE_MIN_POOL); /* limit the size */

	if (vmemcache_add(cache, dir))
		UT_FATAL("vmemcache_new: %s (%s)", vmemcache_errormsg(), dir);

	struct buffers *buffs = calloc(nbuffs, sizeof(*buffs));
	if (buffs == NULL)
		UT_FATAL("out of memory");

	for (unsigned i = 0; i < nbuffs; ++i) {
		/* generate N random sizes (between A – B bytes) */
		buffs[i].size = min_size +
				(size_t)rand() % (max_size - min_size + 1);

		/* allocate a buffer and fill it for every generated size */
		buffs[i].buff = malloc(buffs[i].size);
		if (buffs[i].buff == NULL)
			UT_FATAL("out of memory");

		memset(buffs[i].buff, 0xCC, buffs[i].size);
	}

	os_thread_t *threads = calloc(n_threads, sizeof(*threads));
	if (threads == NULL)
		UT_FATAL("out of memory");

	struct context *ctx = calloc(n_threads, sizeof(*ctx));
	if (ctx == NULL)
		UT_FATAL("out of memory");

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].n_threads = n_threads;
		ctx[i].thread_number = i;
		ctx[i].cache = cache;
		ctx[i].buffs = buffs;
		ctx[i].nbuffs = nbuffs;
	}

	unsigned ops_per_thread = ops_count / n_threads;

	/* run all tests */
	run_test_get_on_miss(cache, n_threads, threads, ops_per_thread, ctx);
	run_test_put(cache, n_threads, threads, ops_per_thread, ctx);
	run_test_get(cache, n_threads, threads, ops_per_thread, ctx);
	run_test_get_put(cache, n_threads, threads, ops_per_thread, ctx);

	if (!skip) {
		run_test_evict(cache, n_threads, threads,
					ops_per_thread, ctx, EVICT_BY_LRU);
		run_test_evict(cache, n_threads, threads,
					ops_per_thread, ctx, EVICT_BY_KEY);
	}

	ret = 0;

	free(ctx);
	free(threads);

	for (unsigned i = 0; i < nbuffs; ++i)
		free(buffs[i].buff);

	free(buffs);

	vmemcache_delete(cache);

	return ret;
}
