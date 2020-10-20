// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

/*
 * bench_micro.c -- multithreaded micro-benchmark for libvmemcache
 *
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libvmemcache.h"
#include "test_helpers.h"
#include "os_thread.h"
#include "benchmark_time.h"

#define MAX_VALUE_SIZE 256

#define BENCH_PUT (0x01)
#define BENCH_GET (0x02)
#define BENCH_ALL (BENCH_PUT | BENCH_GET)

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
	double secs;
	void *(*worker)(void *);
};

/*
 * bench_init -- (internal) initialize benchmark
 */
static VMEMcache *
bench_init(const char *path, size_t size, size_t extent_size,
		enum vmemcache_repl_p repl_p,
		unsigned n_threads, struct context *ctx)
{
	VMEMcache *cache = vmemcache_new();
	vmemcache_set_size(cache, size);
	vmemcache_set_eviction_policy(cache, repl_p);
	if (vmemcache_add(cache, path))
		UT_FATAL("vmemcache_add: %s (%s)", vmemcache_errormsg(), path);

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].cache = cache;
		ctx[i].secs = 0.0;
	}

	return cache;
}

/*
 * bench_fini -- (internal) finalize benchmark
 */
static void
bench_fini(VMEMcache *cache)
{
	vmemcache_delete(cache);
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
	benchmark_time_t t1, t2, tdiff;

	benchmark_time_get(&t1);

	for (i = shift; i < (shift + ctx->ops_count); i++) {
		if (vmemcache_put(ctx->cache, &i, sizeof(i),
				ctx->buffs[i % ctx->nbuffs].buff,
				ctx->buffs[i % ctx->nbuffs].size))
			UT_FATAL("ERROR: vmemcache_put: %s",
					vmemcache_errormsg());
	}

	benchmark_time_get(&t2);
	benchmark_time_diff(&tdiff, &t1, &t2);
	ctx->secs = benchmark_time_get_secs(&tdiff);

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
	benchmark_time_t t1, t2, tdiff;

	char vbuf[MAX_VALUE_SIZE];		/* user-provided buffer */
	size_t vbufsize = MAX_VALUE_SIZE;	/* size of vbuf */
	size_t vsize = 0;			/* real size of the object */

	benchmark_time_get(&t1);

	for (i = 0; i < ctx->ops_count; i++) {
		vmemcache_get(ctx->cache, &i, sizeof(i),
				vbuf, vbufsize, 0, &vsize);
	}

	benchmark_time_get(&t2);
	benchmark_time_diff(&tdiff, &t1, &t2);
	ctx->secs = benchmark_time_get_secs(&tdiff);

	return NULL;
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
 * print_bench_results -- (internal) print results of the benchmark
 */
static void
print_bench_results(const char *op_name, unsigned n_threads,
			unsigned ops_per_thread, struct context *ctx)
{
	double total_time = 0.0;
	for (unsigned i = 0; i < n_threads; ++i)
		total_time += ctx[i].secs;

	double ops = n_threads * ops_per_thread;
	double avg_thread = total_time / (double)n_threads;
	double avg_put = total_time / ops;
	double avg_ops = ops / total_time;

	printf("Total time of all threads  : %e secs\n", total_time);
	printf("Average time of one thread : %e secs\n\n", avg_thread);

	printf("Average time of one '%s' operation : %e secs\n",
		op_name, avg_put);
	printf("Average number of '%s' operations  : %e ops/sec\n\n",
		op_name, avg_ops);
}

/*
 * run_test_put -- (internal) run test for vmemcache_put()
 */
static void
run_bench_put(const char *path, size_t size, size_t extent_size,
		enum vmemcache_repl_p repl_p,
		unsigned n_threads, os_thread_t *threads,
		unsigned ops_count, struct context *ctx)
{
	VMEMcache *cache = bench_init(path, size, extent_size,
					repl_p, n_threads, ctx);

	unsigned ops_per_thread = ops_count / n_threads;

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].worker = worker_thread_put;
		ctx[i].ops_count = ops_per_thread;
	}

	printf("PUT benchmark:\n");
	printf("==============\n");
	printf("\n");

	run_threads(n_threads, threads, ctx);

	print_bench_results("put", n_threads, ops_per_thread, ctx);

	bench_fini(cache);
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
 * run_bench_get -- (internal) run test for vmemcache_get()
 */
static void
run_bench_get(const char *path, size_t size, size_t extent_size,
		enum vmemcache_repl_p repl_p,
		unsigned n_threads, os_thread_t *threads,
		unsigned ops_count, struct context *ctx)
{
	VMEMcache *cache = bench_init(path, size, extent_size,
					repl_p, n_threads, ctx);

	int cache_is_full = 0;
	vmemcache_callback_on_evict(cache, on_evict_cb, &cache_is_full);

	unsigned long long i = 0;
	while (!cache_is_full) {
		if (vmemcache_put(ctx->cache, &i, sizeof(i),
					ctx->buffs[i % ctx->nbuffs].buff,
					ctx->buffs[i % ctx->nbuffs].size))
			UT_FATAL("ERROR: vmemcache_put: %s",
					vmemcache_errormsg());
		i++;
	}

	unsigned ops_per_thread = (unsigned)i;

	vmemcache_callback_on_evict(cache, NULL, NULL);

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].worker = worker_thread_get;
		ctx[i].ops_count = ops_per_thread;
	}

	printf("GET benchmark:\n");
	printf("==============\n");
	printf("\n");

	run_threads(n_threads, threads, ctx);

	print_bench_results("get", n_threads, ops_per_thread, ctx);

	bench_fini(cache);
}

#define USAGE_STRING \
"usage: %s <directory> [benchmark] [threads] [ops_count] [cache_size] [cache_extent_size] [nbuffs] [min_size] [max_size] [seed]\n"\
"       [benchmark] - can be: all (default), put or get\n"\
"       Default values of parameters:\n"\
"       - benchmark           = all (put and get)\n"\
"       - threads             = %u\n"\
"       - ops_count           = %u\n"\
"       - cache_size          = %u\n"\
"       - cache_extent_size   = %u\n"\
"       - nbuffs              = %u\n"\
"       - min_size            = %u\n"\
"       - max_size            = %u\n"\
"       - seed                = <random value>\n"

int
main(int argc, char *argv[])
{
	unsigned seed;
	int ret = -1;

	/* default values of parameters */
	unsigned benchmark = BENCH_ALL;
	unsigned n_threads = 10;
	unsigned ops_count = 100000;
	unsigned cache_size = VMEMCACHE_MIN_POOL;
	unsigned cache_extent_size = VMEMCACHE_MIN_EXTENT;
	unsigned nbuffs = 10;
	unsigned min_size = 128;
	unsigned max_size = MAX_VALUE_SIZE;

	if (argc < 2 || argc > 11) {
		fprintf(stderr, USAGE_STRING, argv[0], n_threads, ops_count,
			cache_size, cache_extent_size,
			nbuffs, min_size, max_size);
		exit(-1);
	}

	const char *dir = argv[1];

	if (argc >= 3) {
		if (strcmp(argv[2], "put") == 0)
			benchmark = BENCH_PUT;
		else if (strcmp(argv[2], "get") == 0)
			benchmark = BENCH_GET;
		else if (strcmp(argv[2], "all") == 0)
			benchmark = BENCH_ALL;
		else {
			fprintf(stderr, "unknown benchmark: %s\n", argv[2]);
			exit(-1);
		}

	}

	if (argc >= 4 &&
	    (str_to_unsigned(argv[3], &n_threads) || n_threads < 1))
		UT_FATAL("incorrect value of n_threads: %s", argv[3]);

	if (argc >= 5 &&
	    (str_to_unsigned(argv[4], &ops_count) || ops_count < 1))
		UT_FATAL("incorrect value of ops_count: %s", argv[4]);

	if (argc >= 6 &&
	    (str_to_unsigned(argv[5], &cache_size) ||
			    cache_size < VMEMCACHE_MIN_POOL))
		UT_FATAL("incorrect value of cache_size: %s", argv[5]);

	if (argc >= 7 &&
	    (str_to_unsigned(argv[6], &cache_extent_size) ||
			    cache_extent_size < VMEMCACHE_MIN_EXTENT))
		UT_FATAL("incorrect value of cache_extent_size: %s", argv[6]);

	if (argc >= 8 &&
	    (str_to_unsigned(argv[7], &nbuffs) || nbuffs < 2))
		UT_FATAL("incorrect value of nbuffs: %s", argv[7]);

	if (argc >= 9 &&
	    (str_to_unsigned(argv[8], &min_size) ||
			    min_size < VMEMCACHE_MIN_EXTENT))
		UT_FATAL("incorrect value of min_size: %s", argv[8]);

	if (argc >= 10 &&
	    (str_to_unsigned(argv[9], &max_size) || max_size < min_size))
		UT_FATAL("incorrect value of max_size: %s", argv[9]);

	if (argc == 11) {
		if (str_to_unsigned(argv[10], &seed))
			UT_FATAL("incorrect value of seed: %s", argv[10]);
	} else {
		seed = (unsigned)time(NULL);
	}

	printf("Benchmark parameters:\n");
	printf("   directory           : %s\n", dir);
	printf("   n_threads           : %u\n", n_threads);
	printf("   ops_count           : %u\n", ops_count);
	printf("   cache_size          : %u\n", cache_size);
	printf("   cache_extent_size   : %u\n", cache_extent_size);
	printf("   nbuffs              : %u\n", nbuffs);
	printf("   min_size            : %u\n", min_size);
	printf("   max_size            : %u\n", max_size);
	printf("   seed                : %u\n\n", seed);

	srand(seed);

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

	struct context *ctx = calloc(n_threads, sizeof(*ctx));
	if (ctx == NULL)
		UT_FATAL("out of memory");

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].thread_number = i;
		ctx[i].buffs = buffs;
		ctx[i].nbuffs = nbuffs;
	}

	os_thread_t *threads = calloc(n_threads, sizeof(*threads));
	if (threads == NULL)
		UT_FATAL("out of memory");

	if (benchmark & BENCH_PUT)
		run_bench_put(dir, cache_size, cache_extent_size,
				VMEMCACHE_REPLACEMENT_LRU,
				n_threads, threads, ops_count, ctx);

	if (benchmark & BENCH_GET)
		run_bench_get(dir, cache_size, cache_extent_size,
				VMEMCACHE_REPLACEMENT_LRU,
				n_threads, threads, ops_count, ctx);

	ret = 0;

	free(threads);
	free(ctx);

	for (unsigned i = 0; i < nbuffs; ++i)
		free(buffs[i].buff);

	free(buffs);

	return ret;
}
