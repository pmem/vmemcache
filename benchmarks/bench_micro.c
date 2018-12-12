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
 * bench_micro.c -- multithreaded micro-benchmark for libvmemcache
 *
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libvmemcache.h"
#include "vmemcache_tests.h"
#include "os_thread.h"
#include "benchmark_time.h"

struct buffers {
	size_t size;
	char *buff;
};

struct context {
	VMEMcache *cache;
	struct buffers *buffs;
	unsigned nbuffs;
	unsigned write_count;
	unsigned thread_number;
	double secs;
};

static void *
worker_thread(void *arg)
{
	struct context *ctx = arg;
	unsigned long long i;
	unsigned long long shift = ctx->thread_number * ctx->write_count;
	benchmark_time_t t1, t2, tdiff;

	benchmark_time_get(&t1);

	for (i = shift; i < (shift + ctx->write_count); i++) {
		if (vmemcache_put(ctx->cache, (char *)&i, sizeof(i),
				ctx->buffs[i % ctx->nbuffs].buff,
				ctx->buffs[i % ctx->nbuffs].size))
			FATAL("ERROR: vmemcache_put: %s", vmemcache_errormsg());
	}

	benchmark_time_get(&t2);
	benchmark_time_diff(&tdiff, &t1, &t2);
	ctx->secs = benchmark_time_get_secs(&tdiff);

	return NULL;
}

int
main(int argc, char *argv[])
{
	unsigned my_seed;
	int ret = -1;

	if (argc < 2 || argc > 10) {
		fprintf(stderr,
			"usage: %s <directory> [threads] [write_count] [cache_max_size] [cache_fragment_size] [nbuffs] [min_size] [max_size] [seed]\n",
			argv[0]);
		exit(-1);
	}

	const char *dir = argv[1];

	/* default values of parameters */
	unsigned n_threads = 10;
	unsigned write_count = 100000;
	size_t cache_max_size = VMEMCACHE_MIN_POOL;
	size_t cache_fragment_size = VMEMCACHE_MIN_FRAG;
	unsigned nbuffs = 10;
	size_t min_size = 8;
	size_t max_size = 64;

	if (argc >= 3)
		n_threads = (unsigned)strtoul(argv[2], NULL, 10);

	if (argc >= 4)
		write_count = (unsigned)strtoul(argv[3], NULL, 10);

	if (argc >= 5)
		cache_max_size = (size_t)strtoul(argv[4], NULL, 10);

	if (argc >= 6)
		cache_fragment_size = (size_t)strtoul(argv[5], NULL, 10);

	if (argc >= 7)
		nbuffs = (unsigned)strtoul(argv[6], NULL, 10);

	if (argc >= 8)
		min_size = (size_t)strtoul(argv[7], NULL, 10);

	if (argc >= 9)
		max_size = (size_t)strtoul(argv[8], NULL, 10);

	if (argc == 10)
		my_seed = (unsigned)strtoul(argv[9], NULL, 10);
	else
		my_seed = (unsigned)time(NULL);

	printf("Test parameters:\n");
	printf("   directory           : %s\n", dir);
	printf("   n_threads           : %u\n", n_threads);
	printf("   write_count         : %u\n", write_count);
	printf("   cache_max_size      : %zu\n", cache_max_size);
	printf("   cache_fragment_size : %zu\n", cache_fragment_size);
	printf("   nbuffs              : %u\n", nbuffs);
	printf("   min_size            : %zu\n", min_size);
	printf("   max_size            : %zu\n", max_size);
	printf("   seed                : %u\n\n", my_seed);

	srand(my_seed);

	VMEMcache *cache = vmemcache_new(dir,
					cache_max_size,
					cache_fragment_size,
					VMEMCACHE_REPLACEMENT_LRU);
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

	struct context *ctx = calloc(n_threads, sizeof(*ctx));
	if (ctx == NULL) {
		FATAL("out of memory");
		goto exit_free_buffs;
	}

	unsigned write_count_per_thread = write_count / n_threads;

	for (unsigned i = 0; i < n_threads; ++i) {
		ctx[i].cache = cache;
		ctx[i].buffs = buffs;
		ctx[i].nbuffs = nbuffs;
		ctx[i].write_count = write_count_per_thread;
		ctx[i].thread_number = i;
	}

	os_thread_t *threads = calloc(n_threads, sizeof(*threads));
	if (threads == NULL) {
		FATAL("out of memory");
		goto exit_free_ctx;
	}

	for (unsigned i = 0; i < n_threads; ++i)
		os_thread_create(&threads[i], NULL, worker_thread, &ctx[i]);

	for (unsigned i = 0; i < n_threads; ++i)
		os_thread_join(&threads[i], NULL);

	double sum = 0.0;
	for (unsigned i = 0; i < n_threads; ++i)
		sum += ctx[i].secs;

	double ops = n_threads * write_count_per_thread;
	double avg_thread = sum / (double)n_threads;
	double avg_put = sum / ops;

	printf("Average time of one thread: %e secs\n\n", avg_thread);
	printf("Average time of one 'put' operation: %e secs\n\n", avg_put);

	ret = 0;

	free(threads);

exit_free_ctx:
	free(ctx);

exit_free_buffs:
	for (unsigned i = 0; i < nbuffs; ++i)
		free(buffs[i].buff);
	free(buffs);

exit_delete:
	/* free all the memory */
	while (vmemcache_evict(cache, NULL, 0) == 0)
		;

	vmemcache_delete(cache);

	return ret;
}
