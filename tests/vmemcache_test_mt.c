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
 * vmemcache_test_mt.c -- multi-threaded test for libvmemcache
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "libvmemcache.h"
#include "test_helpers.h"
#include "os_thread.h"

#define BUF_SIZE 256

struct buffers {
	size_t size;
	char *buff;
};

struct context {
	unsigned thread_number;
	VMEMcache *cache;
	struct buffers *buffs;
	unsigned nbuffs;
	unsigned ops_count;
	void *(*thread_routine)(void *);
};

/*
 * free_cache -- (internal) free the cache
 */
static void
free_cache(VMEMcache *cache)
{
	/* evict all entries from the cache */
	while (vmemcache_evict(cache, NULL, 0) == 0)
		;
}

/*
 * run_threads -- (internal) create and join threads
 */
static void
run_threads(unsigned n_threads, os_thread_t *threads, struct context *ctx)
{
	for (unsigned i = 0; i < n_threads; ++i)
		os_thread_create(&threads[i], NULL, ctx[i].thread_routine,
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
 * worker_thread_put -- (internal) worker testing vmemcache_get()
 */
static void *
worker_thread_get(void *arg)
{
	struct context *ctx = arg;
	unsigned long long i;

	char vbuf[BUF_SIZE];		/* user-provided buffer */
	size_t vbufsize = BUF_SIZE;	/* size of vbuf */
	size_t vsize = 0;		/* real size of the object */

	for (i = 0; i < ctx->ops_count; i++) {
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
		ctx[i].thread_routine = worker_thread_put;
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
		ctx[i].thread_routine = worker_thread_get;
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
		ctx[n_threads >> 1].thread_routine = worker_thread_put_in_gets;
	} else {
		/* 20% of threads (in the middle of their array) are puts */
		unsigned n_puts = (2 * n_threads) / 10; /* 20% of threads */
		unsigned start = (n_threads / 2) - (n_puts / 2);
		for (unsigned i = start; i < start + n_puts; i++)
			ctx[i].thread_routine = worker_thread_put_in_gets;
	}

	printf("%s: STARTED\n", __func__);

	run_threads(n_threads, threads, ctx);

	printf("%s: PASSED\n", __func__);
}

/*
 * on_miss_cb -- (internal) 'on miss' callback for run_test_get_on_miss
 */
static int
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

	return ret;
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
		ctx[i].thread_routine = worker_thread_get;
		ctx[i].ops_count = ops_per_thread;
	}

	printf("%s: STARTED\n", __func__);

	run_threads(n_threads, threads, ctx);

	printf("%s: PASSED\n", __func__);
}

int
main(int argc, char *argv[])
{
	unsigned seed;
	int ret = -1;

	if (argc < 2 || argc > 5) {
		fprintf(stderr,
			"usage: %s dir-name [threads] [ops_count] [seed]\n",
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

	if (argc == 5) {
		if (str_to_unsigned(argv[4], &seed) || seed < 1)
			UT_FATAL("incorrect value of seed: %s", argv[4]);
	} else {
		seed = (unsigned)time(NULL);
	}

	printf("Multi-threaded test parameters:\n");
	printf("   directory           : %s\n", dir);
	printf("   n_threads           : %u\n", n_threads);
	printf("   ops_count           : %u\n", ops_count);
	printf("   nbuffs              : %u\n", nbuffs);
	printf("   min_size            : %zu\n", min_size);
	printf("   max_size            : %zu\n", max_size);
	printf("   seed                : %u\n\n", seed);

	srand(seed);

	VMEMcache *cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL,
				VMEMCACHE_MIN_FRAG, VMEMCACHE_REPLACEMENT_LRU);
	if (cache == NULL)
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
		ctx[i].thread_number = i;
		ctx[i].cache = cache;
		ctx[i].buffs = buffs;
		ctx[i].nbuffs = nbuffs;
	}

	unsigned ops_per_thread = ops_count / n_threads;

	/* run all tests */
	run_test_put(cache, n_threads, threads, ops_per_thread, ctx);
	run_test_get(cache, n_threads, threads, ops_per_thread, ctx);
	run_test_get_put(cache, n_threads, threads, ops_per_thread, ctx);
	run_test_get_on_miss(cache, n_threads, threads, ops_per_thread, ctx);

	ret = 0;

	free(ctx);
	free(threads);

	for (unsigned i = 0; i < nbuffs; ++i)
		free(buffs[i].buff);

	free(buffs);

	vmemcache_delete(cache);

	return ret;
}
