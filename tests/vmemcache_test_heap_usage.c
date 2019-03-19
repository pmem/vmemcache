/*
 * Copyright 2019, Intel Corporation
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
 * vmemcache_test_heap_usage.c -- libvmemcache heap usage tracing test
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <dlfcn.h>
#include <errno.h>

#include <libvmemcache.h>
#include "test_helpers.h"

#define ALLOWED_RATIO 1.0

static __thread int trace = 0;
/*
 * TRACE_HEAP - heap usage is traced only for expressions wrapped inside
 * this macro
 */
#define TRACE_HEAP(fn) do { trace = 1; fn; trace = 0; } while (0)

static void *(*actual_malloc)(size_t) = NULL;
static void *(*actual_realloc)(void *, size_t) = NULL;
static void (*actual_free)(void *) = NULL;

typedef struct {
	ssize_t usage;
	ssize_t max_usage;
	float ratio;
	size_t cache_size;
} heap_usage;

__thread heap_usage usage = {0, 0, 0.0, 0};
int verbose = 0;

/*
 * on_evict_cb -- (internal) 'on evict' callback
 */
static void
on_evict_cb(VMEMcache *cache, const void *key, size_t key_size,
		void *arg)
{
	int *evicted = arg;
	*evicted = 1;
}

/*
 * log_line -- (internal) print log line with current heap status
 */
static void
log_line(heap_usage *usage, size_t size, void *ptr, const char *prefix)
{
	if (usage->cache_size == 0)
		usage->ratio = 0;
	else
		usage->ratio = (float)usage->usage / (float)usage->cache_size;

	printf("%s %zu bytes\t(%p)\theap usage: %zd bytes\tratio: %.3f\n",
			prefix, size, ptr, usage->usage, usage->ratio);
}

/*
 * malloc -- (internal) 'malloc' wrapper
 */
void
*malloc(size_t size)
{
	int tmp_trace = trace;
	trace = 0;

	if (actual_malloc == NULL) {
		actual_malloc = dlsym(RTLD_NEXT, "malloc");
		if (actual_malloc == NULL)
			UT_FATAL("dlsym: could not load 'malloc' symbol");
	}

	void *p = NULL;
	p = actual_malloc(size);

	if (p == NULL)
		UT_FATAL("malloc fail. errno: %s", strerror(errno));

	if (tmp_trace) {
		size_t s = malloc_usable_size(p);
		usage.usage = usage.usage + (ssize_t)s;
		if (verbose)
			log_line(&usage, s, p, "allocating");
		if (usage.usage > usage.max_usage)
			usage.max_usage = usage.usage;
	}

	trace = tmp_trace;
	return p;
}

/*
 * realloc -- (internal) 'realloc' wrapper
 */
void
*realloc(void *ptr, size_t size)
{
	int tmp_trace = trace;
	trace = 0;

	if (actual_realloc == NULL) {
		actual_realloc = dlsym(RTLD_NEXT, "realloc");
		if (actual_realloc == NULL)
			UT_FATAL("dlsym: could not load 'realloc' symbol");
	}

	if (tmp_trace) {
		size_t old_size = malloc_usable_size(ptr);
		usage.usage -= (ssize_t)old_size;
	}

	void *p = actual_realloc(ptr, size);
	if (p == NULL)
		UT_FATAL("realloc fail, errno: %s", strerror(errno));

	if (tmp_trace) {
		size_t new_size = malloc_usable_size(p);
		usage.usage += (ssize_t)new_size;
		if (verbose)
			log_line(&usage, new_size, p, "allocating");
		if (usage.usage > usage.max_usage)
			usage.max_usage = usage.usage;
	}

	trace = tmp_trace;
	return p;
}

/*
 * free -- (internal) 'free' wrapper
 */
void
free(void *ptr)
{
	int tmp_trace = trace;
	trace = 0;

	if (actual_free == NULL) {
		actual_free = dlsym(RTLD_NEXT, "free");
		if (actual_free == NULL)
			UT_FATAL("dlsym: could not load 'free' symbol");
	}

	if (tmp_trace) {
		size_t size = malloc_usable_size(ptr);
		usage.usage = usage.usage - (ssize_t)size;
		if (verbose)
			log_line(&usage, size, ptr, "freeing");
	}

	actual_free(ptr);
	trace = tmp_trace;
}

/*
 * check_max_ratio -- (internal) calculate max usage ratio, handle error
 * condition
 */
static void
check_max_ratio(heap_usage *usage)
{
	float max_ratio = (float)usage->max_usage / (float)usage->cache_size;
	printf("maximal heap usage / cache size equals: %.3f\n", max_ratio);
	if (max_ratio > ALLOWED_RATIO)
		UT_FATAL("Maximal ratio is too high (should be lower than %f)",
			ALLOWED_RATIO);
}

/*
 * test_heap_usage -- (internal) test heap usage
 */
static void
test_heap_usage(const char *dir, heap_usage *usage)
{
	size_t cache_size = VMEMCACHE_MIN_POOL;
	usage->cache_size = cache_size;

	VMEMcache *cache;
	TRACE_HEAP(cache = vmemcache_new(dir, cache_size,
		VMEMCACHE_MIN_EXTENT, VMEMCACHE_REPLACEMENT_LRU));
	if (cache == NULL)
		UT_FATAL("vmemcache_new: %s", vmemcache_errormsg());

	int evicted = 0;
	TRACE_HEAP(vmemcache_callback_on_evict(cache, on_evict_cb, &evicted));

	size_t key = 0;
	size_t vsize = 32;
	char *value = malloc(vsize);
	memset(value, 'a', vsize - 1);
	value[vsize - 1] = '\0';

	int ret;
	while (!evicted) {
		TRACE_HEAP(ret = vmemcache_put(cache, &key, sizeof(key),
						value, vsize));
		if (ret)
			UT_FATAL("vmemcache put: %s. errno: %s",
				vmemcache_errormsg(), strerror(errno));
		key++;
	}

	free(value);

	TRACE_HEAP(vmemcache_delete(cache));

	if (usage->usage != 0)
		UT_FATAL("final indicated usage equals %zd (should be 0)",
			usage->usage);

	check_max_ratio(usage);
}

int
main(int argc, char **argv)
{
	if (argc < 2)
		UT_FATAL("%s <dir>\n", argv[0]);

	if (argc == 3) {
		if (strcmp("verbose", argv[2]) == 0)
			verbose = 1;
		else
			UT_FATAL("Unknown argument: %s", argv[2]);
	}

	test_heap_usage(argv[1], &usage);
}
