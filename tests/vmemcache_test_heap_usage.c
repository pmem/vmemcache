// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2019, Intel Corporation */

/*
 * vmemcache_test_heap_usage.c -- libvmemcache heap usage tracing test.
 * The test passes if measured unit usage (usage per entry) is lower than
 * MAX_BYTES_PER_ENTRY
 */

/* enable RTLD_NEXT not defined by POSIX  */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <dlfcn.h>
#include <errno.h>

#include <libvmemcache.h>
#include "test_helpers.h"

#define MAX_BYTES_PER_ENTRY 180

static __thread int trace = 0;
/*
 * TRACE_HEAP - heap usage is traced only for expressions wrapped inside
 * this macro
 */
#define TRACE_HEAP(fn) do { trace = 1; fn; trace = 0; } while (0)

static void *(*actual_malloc)(size_t);
static void *(*actual_realloc)(void *, size_t);
static void (*actual_free)(void *);

typedef struct {
	ssize_t usage;
	size_t entries;
	ssize_t unit_usage;
	int evicted;
} heap_usage;

static __thread heap_usage usage = {0, 0, 0, 0};
static int verbose = 0;

/*
 * on_evict_cb -- (internal) 'on evict' callback
 */
static void
on_evict_cb(VMEMcache *cache, const void *key, size_t key_size,
		void *arg)
{
	heap_usage *usage = arg;
	usage->entries--;
	usage->evicted = 1;
}

/*
 * log_line -- (internal) print log line with current heap status
 */
static void
log_line(heap_usage *usage, size_t size, void *ptr, const char *prefix)
{
	printf("%s %zu bytes\t(%p)\theap usage: %zd bytes\n",
			prefix, size, ptr, usage->usage);
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

	void *p = actual_malloc(size);
	if (p == NULL)
		goto end;

	if (tmp_trace) {
		size_t s = malloc_usable_size(p);
		usage.usage += (ssize_t)s;
		if (verbose)
			log_line(&usage, s, p, "allocating");
	}

end:
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
		goto end;

	if (tmp_trace) {
		size_t new_size = malloc_usable_size(p);
		usage.usage += (ssize_t)new_size;
		if (verbose)
			log_line(&usage, new_size, p, "allocating");
	}

end:
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
		usage.usage -= (ssize_t)size;
		if (verbose)
			log_line(&usage, size, ptr, "freeing");
	}

	actual_free(ptr);
	trace = tmp_trace;
}

/*
 * test_heap_usage -- (internal) test heap usage
 */
static int
test_heap_usage(const char *dir, heap_usage *usage)
{
	int ret = 0;

	VMEMcache *cache;
	TRACE_HEAP(cache = vmemcache_new());
	if (cache == NULL)
		UT_FATAL("vmemcache_new: %s", vmemcache_errormsg());
	vmemcache_set_size(cache, VMEMCACHE_MIN_POOL);
	vmemcache_set_extent_size(cache, VMEMCACHE_MIN_EXTENT);
	TRACE_HEAP(ret = vmemcache_add(cache, dir));
	if (ret)
		UT_FATAL("vmemcache_add: %s", vmemcache_errormsg());

	TRACE_HEAP(vmemcache_callback_on_evict(cache, on_evict_cb, usage));

	size_t key = 0;
	size_t vsize = 32;

	char *value = malloc(vsize);
	if (value == NULL)
		UT_FATAL("out of memory");
	memset(value, 'a', vsize - 1);
	value[vsize - 1] = '\0';

	int putret;
	while (!usage->evicted) {
		TRACE_HEAP(putret = vmemcache_put(cache, &key, sizeof(key),
					value, vsize));
		if (putret)
			UT_FATAL("vmemcache put: %s. errno: %s",
				vmemcache_errormsg(), strerror(errno));

		usage->entries++;
		key++;
		usage->unit_usage =
			usage->usage / (ssize_t)usage->entries;

		if (verbose)
			printf(
				"bytes per entry: %zu, (number of entries: %zu)\n",
				usage->unit_usage, usage->entries);
	}
	free(value);
	ssize_t unit_usage_full_cache = usage->unit_usage;

	TRACE_HEAP(vmemcache_delete(cache));

	printf("heap usage per entry: %zd bytes\n", unit_usage_full_cache);
	if (unit_usage_full_cache > MAX_BYTES_PER_ENTRY) {
		UT_ERR(
			"heap usage per entry equals %zd bytes, should be lower than %d bytes",
			unit_usage_full_cache, MAX_BYTES_PER_ENTRY);
			ret = 1;
		}

	if (usage->usage != 0)
		UT_FATAL(
			"Final heap usage is different than 0 (%zd): possible memory leak",
			usage->usage);

	return ret;
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

	return test_heap_usage(argv[1], &usage);
}
