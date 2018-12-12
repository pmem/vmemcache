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
 * vmemcache_test_basic.c -- basic unit test for libvmemcache
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libvmemcache.h"
#include "vmemcache_tests.h"

#define VMEMCACHE_FRAGMENT 16
#define LEN (VMEMCACHE_FRAGMENT)
#define KSIZE LEN /* key size */
#define VSIZE LEN /* value size */
#define DNUM 10 /* number of data */

struct ctx_cb {
	char vbuf[VSIZE];
	size_t vbufsize;
	size_t vsize;
};

/*
 * test_new_delete -- (internal) test _new() and _delete()
 */
static void
test_new_delete(const char *dir,
		enum vmemcache_replacement_policy replacement_policy)
{
	VMEMcache *cache;

	/* TEST #1 - minimum values of max_size and fragment_size */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG,
				replacement_policy);
	if (cache == NULL)
		FATAL("vmemcache_new: %s", vmemcache_errormsg());

	vmemcache_delete(cache);

	/* TEST #2 - fragment_size = max_size = VMEMCACHE_MIN_POOL */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_POOL,
				replacement_policy);
	if (cache == NULL)
		FATAL("vmemcache_new: %s", vmemcache_errormsg());

	vmemcache_delete(cache);

	/* TEST #3 - fragment_size == 0 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, 0,
				replacement_policy);
	if (cache != NULL)
		FATAL("vmemcache_new did not fail with fragment_size == 0");

	/* TEST #4 - fragment_size == -1 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, (size_t)-1,
				replacement_policy);
	if (cache != NULL)
		FATAL("vmemcache_new did not fail with fragment_size == -1");

	/* TEST #5 - fragment_size == VMEMCACHE_MIN_FRAG - 1 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG - 1,
				replacement_policy);
	if (cache != NULL)
		FATAL(
			"vmemcache_new did not fail with fragment_size == VMEMCACHE_MIN_FRAG - 1");

	/* TEST #6 - fragment_size == max_size + 1 */
	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_POOL + 1,
				replacement_policy);
	if (cache != NULL)
		FATAL(
			"vmemcache_new did not fail with fragment_size == max_size + 1");
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
		FATAL("vmemcache_new: %s", vmemcache_errormsg());

	const char *key = "KEY";
	size_t key_size = strlen(key) + 1;
	const char *value = "VALUE";
	size_t value_size = strlen(value) + 1;

	if (vmemcache_put(cache, key, key_size, value, value_size))
		FATAL("vmemcache_put: %s", vmemcache_errormsg());

	char vbuf[VMEMCACHE_FRAGMENT]; /* user-provided buffer */
	size_t vbufsize = VMEMCACHE_FRAGMENT; /* size of vbuf */
	size_t vsize = 0; /* real size of the object */
	ssize_t ret;

	/* get the only one element */
	ret = vmemcache_get(cache, key, key_size, vbuf, vbufsize, 0, &vsize);
	if (ret < 0)
		FATAL("vmemcache_get: %s", vmemcache_errormsg());

	if ((size_t)ret != value_size)
		FATAL("vmemcache_get: wrong return value: %zi (should be %zu)",
			ret, value_size);

	if (vsize != value_size)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %zu)",
			vsize, value_size);

	if (strncmp(vbuf, value, vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
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
		FATAL("unknown policy: %u", replacement_policy);
		break;
	}

	if (ret == -1)
		FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	/* getting the evicted element should return 0 (no such element) */
	ret = vmemcache_get(cache, key, key_size, vbuf, vbufsize, 0, &vsize);
	if (ret != 0)
		FATAL("vmemcache_get did not return 0 (no such element)");

	vmemcache_delete(cache);
}

/*
 * on_evict_test_evict_cb -- (internal) 'on evict' callback for test_evict
 */
static void
on_evict_test_evict_cb(VMEMcache *cache, const char *key, size_t key_size,
		void *arg)
{
	struct ctx_cb *ctx = arg;
	ssize_t ret;

	ret = vmemcache_get(cache, key, key_size, ctx->vbuf, ctx->vbufsize, 0,
				&ctx->vsize);
	if (ret < 0)
		FATAL("vmemcache_get");

	if ((size_t)ret != VSIZE)
		FATAL("vmemcache_get: wrong return value: %zi (should be %i)",
			ret, VSIZE);
}

/*
 * on_miss_test_evict_cb -- (internal) 'on miss' callback for test_evict
 */
static int
on_miss_test_evict_cb(VMEMcache *cache, const char *key, size_t key_size,
		void *arg)
{
	struct ctx_cb *ctx = arg;

	size_t size = (key_size <= ctx->vbufsize) ? key_size : ctx->vbufsize;

	memcpy(ctx->vbuf, key, size);
	ctx->vsize = size;

	return 1;
}

/*
 * test_evict -- (internal) test _evict()
 */
static void
test_evict(const char *dir)
{
	VMEMcache *cache;
	char vbuf[VSIZE];
	size_t vsize = 0;
	ssize_t ret;

	struct ctx_cb ctx = {"", VSIZE, 0};

	struct kv {
		char key[KSIZE];
		char value[VSIZE];
	} data[DNUM];

	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_FRAGMENT,
					VMEMCACHE_REPLACEMENT_LRU);
	if (cache == NULL)
		FATAL("vmemcache_new: %s", vmemcache_errormsg());

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
			FATAL("vmemcache_put: %s", vmemcache_errormsg());
	}

	/* TEST #1 - evict the element with index #5 */
	ret = vmemcache_evict(cache, data[5].key, KSIZE);
	if (ret == -1)
		FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	if (ctx.vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted element is #5 */
	if (strncmp(ctx.vbuf, data[5].value, ctx.vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[5].value);

	/* TEST #2 - evict the LRU element */
	ret = vmemcache_evict(cache, NULL, 0);
	if (ret == -1)
		FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	if (ctx.vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted LRU element is #0 */
	if (strncmp(ctx.vbuf, data[0].value, ctx.vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[0].value);

	/* TEST #3 - get the element with index #1 (to change LRU one to #2) */
	ret = vmemcache_get(cache, data[1].key, KSIZE, vbuf, VSIZE,
			0, &vsize);
	if (ret < 0)
		FATAL("vmemcache_get");

	if ((size_t)ret != VSIZE)
		FATAL("vmemcache_get: wrong return value: %zi (should be %i)",
			ret, VSIZE);

	if (vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the got element is #1 */
	if (strncmp(vbuf, data[1].value, vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			vbuf, data[1].value);

	/* TEST #4 - evict the LRU element (it should be #2 now) */
	ret = vmemcache_evict(cache, NULL, 0);
	if (ret == -1)
		FATAL("vmemcache_evict: %s", vmemcache_errormsg());

	if (ctx.vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted LRU element is #2 */
	if (strncmp(ctx.vbuf, data[2].value, ctx.vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[2].value);

	/* TEST #5 - get the evicted element with index #2 */
	ret = vmemcache_get(cache, data[2].key, KSIZE, vbuf, VSIZE,
			0, &vsize);
	if (ret == -1)
		FATAL("vmemcache_get");

	if (ret != 0)
		FATAL("vmemcache_get: wrong return value: %zi (should be %i)",
			ret, 0);

	if (ctx.vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the 'on_miss' callback got key #2 */
	if (strncmp(ctx.vbuf, data[2].key, ctx.vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[2].key);

	/* free all the memory */
	while (vmemcache_evict(cache, NULL, 0) == 0)
		;

	vmemcache_delete(cache);
}

/*
 * on_evict_test_memory_leaks_cb -- (internal) 'on evict' callback for
 *                                   test_memory_leaks
 */
static void
on_evict_test_memory_leaks_cb(VMEMcache *cache,
				const char *key, size_t key_size, void *arg)
{
	unsigned long long *counter = arg;

	(*counter)++;
}

/*
 * test_memory_leaks -- (internal) test if there are any memory leaks
 */
static void
test_memory_leaks(const char *dir)
{
	VMEMcache *cache;
	char *buff;
	size_t size;

	srand((unsigned)time(NULL));

	unsigned long long n_puts = 0;
	unsigned long long n_evicts = 0;

	size_t min_size = VMEMCACHE_MIN_FRAG / 2;
	size_t max_size = VMEMCACHE_MIN_POOL / 16;

	cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG,
				VMEMCACHE_REPLACEMENT_LRU);
	if (cache == NULL)
		FATAL("vmemcache_new: %s", vmemcache_errormsg());

	vmemcache_callback_on_evict(cache, on_evict_test_memory_leaks_cb,
					&n_evicts);

	while (n_evicts < 1000) {
		n_puts++;

		size = min_size + (size_t)rand() % (max_size - min_size + 1);
		buff = malloc(size);
		if (buff == NULL)
			FATAL("out of memory");

		if (vmemcache_put(cache, (char *)&n_puts, sizeof(n_puts),
					buff, size))
			FATAL("vmemcache_put(n_puts: %llu n_evicts: %llu): %s",
				n_puts, n_evicts, vmemcache_errormsg());

		free(buff);
	}

	/* free all the memory */
	while (vmemcache_evict(cache, NULL, 0) == 0)
		;

	vmemcache_delete(cache);

	if (n_evicts != n_puts)
		FATAL("memory leak detected");
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s dir-name\n", argv[0]);
		exit(-1);
	}

	const char *dir = argv[1];

	test_new_delete(dir, VMEMCACHE_REPLACEMENT_NONE);
	test_new_delete(dir, VMEMCACHE_REPLACEMENT_LRU);
	test_put_get_evict(dir, VMEMCACHE_REPLACEMENT_NONE);
	test_put_get_evict(dir, VMEMCACHE_REPLACEMENT_LRU);
	test_evict(dir);
	test_memory_leaks(dir);
}
