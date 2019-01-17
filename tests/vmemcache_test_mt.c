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

#include "libvmemcache.h"
#include "vmemcache_tests.h"
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
	unsigned long long ops_count;
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
		if (vmemcache_put(ctx->cache, (char *)&i, sizeof(i),
				ctx->buffs[i % ctx->nbuffs].buff,
				ctx->buffs[i % ctx->nbuffs].size))
			FATAL("ERROR: vmemcache_put: %s", vmemcache_errormsg());
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

	for (i = 1; i < ctx->ops_count; i++) {
		vmemcache_get(ctx->cache, (char *)&i, sizeof(i),
				vbuf, vbufsize, 0, &vsize);
	}

	return NULL;
}

/*
 * run_test_put -- (internal) run test for vmemcache_put()
 */
static void
run_test_put(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		struct context *ctx)
{
	free_cache(cache);

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].thread_routine = worker_thread_put;
	}

	run_threads(n_threads, threads, ctx);

	printf("%s: PASSED\n", __func__);
}

/*
 * on_evict_cb -- (internal) 'on evict' callback for run_test_get
 */
static void
on_evict_cb(VMEMcache *cache, const char *key, size_t key_size, void *arg)
{
	int *cache_is_full = arg;

	*cache_is_full = 1;
}

/*
 * run_test_get -- (internal) run test for vmemcache_get()
 */
static void
run_test_get(VMEMcache *cache, unsigned n_threads, os_thread_t *threads,
		struct context *ctx)
{
	free_cache(cache);

	int cache_is_full = 0;
	vmemcache_callback_on_evict(cache, on_evict_cb, &cache_is_full);

	unsigned long long i = 0;
	while (!cache_is_full) {
		if (vmemcache_put(ctx->cache, (char *)&i, sizeof(i),
					ctx->buffs[i % ctx->nbuffs].buff,
					ctx->buffs[i % ctx->nbuffs].size))
			FATAL("ERROR: vmemcache_put: %s", vmemcache_errormsg());
		i++;
	}

	unsigned long long ops_count = i;

	vmemcache_callback_on_evict(cache, NULL, NULL);

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].thread_routine = worker_thread_get;
		ctx[i].ops_count = ops_count;
	}

	run_threads(n_threads, threads, ctx);

	printf("%s: PASSED\n", __func__);
}


int
main(int argc, char *argv[])
{
	unsigned my_seed;
	char *endptr = NULL;
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
	unsigned ops_count = 1000;
	unsigned nbuffs = 10;
	size_t min_size = 8;
	size_t max_size = 64;

	if (argc >= 3) {
		n_threads = (unsigned)strtoul(argv[2], &endptr, 10);
		if ((endptr && strcmp(endptr, "")) || (n_threads < 1))
			FATAL("incorrect value of n_threads: %s (%s)",
				argv[2], endptr);
	}

	if (argc >= 4) {
		ops_count = (unsigned)strtoul(argv[3], &endptr, 10);
		if ((endptr && strcmp(endptr, "")) || (ops_count < 1))
			FATAL("incorrect value of ops_count: %s (%s)",
				argv[3], endptr);
	}

	if (argc == 5)
		my_seed = (unsigned)strtoul(argv[4], NULL, 10);
	else
		my_seed = (unsigned)time(NULL);

	printf("value of seed: %u\n", my_seed);
	srand(my_seed);

	VMEMcache *cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL,
				VMEMCACHE_MIN_FRAG, VMEMCACHE_REPLACEMENT_LRU);
	if (cache == NULL)
		FATAL("vmemcache_new: %s (%s)", vmemcache_errormsg(), dir);

	struct buffers *buffs = calloc(nbuffs, sizeof(*buffs));
	if (buffs == NULL) {
		FATAL("out of memory");
		goto exit_delete;
	}

	for (unsigned i = 0; i < nbuffs; ++i) {
		/* generate N random sizes (between A – B bytes) */
		buffs[i].size = min_size +
				(size_t)rand() % (max_size - min_size + 1);

		/* allocate a buffer and fill it for every generated size */
		buffs[i].buff = malloc(buffs[i].size);
		if (buffs[i].buff == NULL) {
			FATAL("out of memory");
			goto exit_free_buffs;
		}

		memset(buffs[i].buff, 0xCC, buffs[i].size);
	}

	os_thread_t *threads = calloc(n_threads, sizeof(*threads));
	if (threads == NULL) {
		FATAL("out of memory");
		goto exit_free_buffs;
	}

	struct context *ctx = calloc(n_threads, sizeof(*ctx));
	if (ctx == NULL) {
		FATAL("out of memory");
		goto exit_free_threads;
	}

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].thread_number = i;
		ctx[i].cache = cache;
		ctx[i].buffs = buffs;
		ctx[i].nbuffs = nbuffs;
		ctx[i].ops_count = ops_count / n_threads;
	}

	/* run all tests */
	run_test_put(cache, n_threads, threads, ctx);
	run_test_get(cache, n_threads, threads, ctx);

	ret = 0;

	free(ctx);

exit_free_threads:
	free(threads);

exit_free_buffs:
	for (unsigned i = 0; i < nbuffs; ++i)
		free(buffs[i].buff);
	free(buffs);

exit_delete:
	free_cache(cache);
	vmemcache_delete(cache);

	return ret;
}
