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
 * vmemcache_basic.c -- unit test for libvmemcache
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libvmemcache.h"
#include "vmemcache_tests.h"

#define VMEMCACHE_FRAGMENT 16

static void
test_new_delete(const char *dir)
{
	VMEMcache *cache;

	/* TEST #1 - minimum values of max_size and fragment_size */
	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_FRAG,
					VMEMCACHE_REPLACEMENT_LRU)) == NULL)
		FATAL("vmemcache_new: %s", dir);

	vmemcache_delete(cache);

	/* TEST #2 - fragment_size = max_size = VMEMCACHE_MIN_POOL */
	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_MIN_POOL,
					VMEMCACHE_REPLACEMENT_LRU)) == NULL)
		FATAL("vmemcache_new: %s", dir);

	vmemcache_delete(cache);

	/* TEST #3 - fragment_size == 0 */
	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, 0,
					VMEMCACHE_REPLACEMENT_LRU)) != NULL)
		FATAL("vmemcache_new did not fail with fragment_size == 0");

	/* TEST #4 - fragment_size == -1 */
	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, (size_t)-1,
					VMEMCACHE_REPLACEMENT_LRU)) != NULL)
		FATAL("vmemcache_new did not fail with fragment_size == -1");

	/* TEST #5 - fragment_size == VMEMCACHE_MIN_FRAG - 1 */
	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL,
					VMEMCACHE_MIN_FRAG - 1,
					VMEMCACHE_REPLACEMENT_LRU)) != NULL)
		FATAL(
			"vmemcache_new did not fail with fragment_size == VMEMCACHE_MIN_FRAG - 1");

	/* TEST #6 - fragment_size == max_size + 1 */
	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL,
					VMEMCACHE_MIN_POOL + 1,
					VMEMCACHE_REPLACEMENT_LRU)) != NULL)
		FATAL(
			"vmemcache_new did not fail with fragment_size == max_size + 1");
}

static void
test_put_get_evict(const char *dir)
{
	VMEMcache *cache;

	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_FRAGMENT,
					VMEMCACHE_REPLACEMENT_LRU)) == NULL)
		FATAL("vmemcache_new: %s", dir);

	const char *key = "KEY";
	size_t key_size = strlen(key) + 1;
	const char *value = "VALUE";
	size_t value_size = strlen(value) + 1;

	if (vmemcache_put(cache, key, key_size, value, value_size))
		FATAL("vmemcache_put: %s", dir);

	char vbuf[VMEMCACHE_FRAGMENT]; /* user-provided buffer */
	size_t vbufsize = VMEMCACHE_FRAGMENT; /* size of vbuf */
	size_t vsize = 0; /* real size of the object */
	ssize_t ret;

	/* get the only one element */
	ret = vmemcache_get(cache, key, key_size, vbuf, vbufsize, 0, &vsize);
	if (ret < 0)
		FATAL("vmemcache_get: %s", dir);

	if ((size_t)ret != vbufsize)
		FATAL("vmemcache_get: wrong return value: %zi (should be %zu)",
			ret, vbufsize);

	if (vsize != value_size)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %zu)",
			vsize, value_size);

	if (strncmp(vbuf, value, vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			vbuf, value);

	/* evict the only one element */
	ret = vmemcache_evict(cache, NULL, 0);
	if (ret == -1)
		FATAL("vmemcache_evict: %s", dir);

	/* getting the evicted element should return 0 (no such element) */
	ret = vmemcache_get(cache, key, key_size, vbuf, vbufsize, 0, &vsize);
	if (ret != 0)
		FATAL("vmemcache_get did not return 0 (no such element)");

	vmemcache_delete(cache);
}

#define LEN (VMEMCACHE_FRAGMENT + 1)
#define KSIZE LEN /* key size */
#define VSIZE LEN /* value size */
#define DNUM 10 /* number of data */

struct ctx_cb {
	char vbuf[VSIZE];
	size_t vbufsize;
	size_t vsize;
};

static void
on_evict_cb(VMEMcache *cache, const char *key, size_t key_size, void *arg)
{
	struct ctx_cb *ctx = arg;
	ssize_t ret;

	ret = vmemcache_get(cache, key, key_size, ctx->vbuf, ctx->vbufsize, 0,
				&ctx->vsize);
	if (ret < 0)
		FATAL("vmemcache_get");

	if ((size_t)ret != ctx->vbufsize)
		FATAL("vmemcache_get: wrong return value: %zi (should be %zu)",
			ret, ctx->vbufsize);
}

static int
on_miss_cb(VMEMcache *cache, const char *key, size_t key_size, void *arg)
{
	struct ctx_cb *ctx = arg;

	size_t size = (key_size <= ctx->vbufsize)? key_size : ctx->vbufsize;

	memcpy(ctx->vbuf, key, size);
	ctx->vsize = size;

	return 1;
}

static void
test_evict(const char *dir)
{
	VMEMcache *cache;
	struct ctx_cb ctx = {"", VSIZE, 0};

	struct kv {
		char key[KSIZE];
		char value[VSIZE];
	} data[DNUM];

	if ((cache = vmemcache_new(dir, VMEMCACHE_MIN_POOL, VMEMCACHE_FRAGMENT,
					VMEMCACHE_REPLACEMENT_LRU)) == NULL)
		FATAL("vmemcache_new: %s", dir);

	vmemcache_callback_on_evict(cache, on_evict_cb, &ctx);
	vmemcache_callback_on_miss(cache, on_miss_cb, &ctx);

	for (int i = 0; i < DNUM; i++) {
		data[i].key[0] = 'k';
		memset(&data[i].key[1], '0' + i, KSIZE - 2);
		data[i].key[KSIZE - 1] = 0;

		data[i].value[0] = 'v';
		memset(&data[i].value[1], '0' + i, VSIZE - 2);
		data[i].value[VSIZE - 1] = 0;

		if (vmemcache_put(cache, data[i].key, KSIZE,
					data[i].value, VSIZE))
			FATAL("vmemcache_put: %s", dir);
	}

	ssize_t ret;

	/* TEST #1 - evict the element with index #5 */
	ret = vmemcache_evict(cache, data[5].key, KSIZE);
	if (ret == -1)
		FATAL("vmemcache_evict: %s", dir);

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
		FATAL("vmemcache_evict: %s", dir);

	if (ctx.vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted LRU element is #0 */
	if (strncmp(ctx.vbuf, data[0].value, ctx.vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[0].value);

	/* TEST #3 - get the element with index #1 (to change LRU one to #2) */
	ret = vmemcache_get(cache, data[1].key, KSIZE, ctx.vbuf,
				ctx.vbufsize, 0, &ctx.vsize);
	if (ret < 0)
		FATAL("vmemcache_get");

	if ((size_t)ret != ctx.vbufsize)
		FATAL("vmemcache_get: wrong return value: %zi (should be %zu)",
			ret, ctx.vbufsize);

	if (ctx.vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the got element is #1 */
	if (strncmp(ctx.vbuf, data[1].value, ctx.vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[1].value);

	/* TEST #4 - evict the LRU element (it should be #2 now) */
	ret = vmemcache_evict(cache, NULL, 0);
	if (ret == -1)
		FATAL("vmemcache_evict: %s", dir);

	if (ctx.vsize != VSIZE)
		FATAL("vmemcache_get: wrong size of value: %zi (should be %i)",
			ctx.vsize, VSIZE);

	/* check if the evicted LRU element is #2 */
	if (strncmp(ctx.vbuf, data[2].value, ctx.vsize))
		FATAL("vmemcache_get: wrong value: %s (should be %s)",
			ctx.vbuf, data[2].value);

	/* TEST #5 - get the evicted element with index #2 */
	ret = vmemcache_get(cache, data[2].key, KSIZE, ctx.vbuf,
				ctx.vbufsize, 0, &ctx.vsize);
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

	vmemcache_delete(cache);
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		FATAL("usage: %s dir-name", argv[0]);

	const char *dir = argv[1];

	test_new_delete(dir);
	test_put_get_evict(dir);
	test_evict(dir);
}
