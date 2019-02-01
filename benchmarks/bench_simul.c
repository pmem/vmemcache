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
 * bench_simul.c -- benchmark simulating expected workloads
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "libvmemcache.h"
#include "test_helpers.h"
#include "os_thread.h"
#include "benchmark_time.h"
#include "rand.h"

#define PROG "bench_simul"
#define MAX_THREADS 4096

#define SIZE_KB (1024ULL)
#define SIZE_MB (1024 * 1024ULL)
#define SIZE_GB (1024 * 1024 * 1024ULL)
#define SIZE_TB (1024 * 1024 * 1024 * 1024ULL)

#define NSECPSEC 1000000000

static const char *dir;
static uint64_t n_threads = 100;
static uint64_t ops_count = 100000;
static uint64_t min_size  = 8;
static uint64_t max_size  = 8 * SIZE_MB;
static uint64_t cache_size = VMEMCACHE_MIN_POOL;
static uint64_t cache_fragment_size = VMEMCACHE_MIN_FRAG;
static uint64_t repl_policy = VMEMCACHE_REPLACEMENT_LRU;
static uint64_t key_size = 16;
static uint64_t seed = 0;

static VMEMcache *cache;

/* case insensitive */
static const char *enum_repl[] = {
	"none",
	"LRU",
	0
};

static struct param_t {
	const char *name;
	uint64_t *var;
	uint64_t min;
	uint64_t max;
	const char **enums;
} params[] = {
	{ "n_threads", &n_threads, 0 /* n_procs */, MAX_THREADS, NULL },
	{ "ops_count", &ops_count, 1, -1ULL, NULL },
	{ "min_size", &min_size, 1, -1ULL, NULL },
	{ "max_size", &max_size, 1, -1ULL, NULL },
	{ "cache_size", &cache_size, VMEMCACHE_MIN_POOL, -1ULL, NULL },
	{ "cache_fragment_size", &cache_fragment_size, VMEMCACHE_MIN_FRAG,
		4 * SIZE_GB, NULL },
	{ "repl_policy", &repl_policy, 1, 1, enum_repl },
	{ "key_size", &key_size, 1, SIZE_GB, NULL },
	{ "seed", &seed, 0, -1ULL, NULL },
	{ 0 },
};

/*
 * parse_uint_param -- parse an uint, accepting suffixes
 */
static uint64_t parse_uint_param(const char *val, const char *name)
{
	char *endptr;
	errno = 0;
	uint64_t x = strtoull(val, &endptr, 0);

	if (errno)
		UT_FATAL("invalid value for %s: \"%s\"", name, val);

	if (*endptr) {
		if (strcmp(endptr, "K") == 0 || strcmp(endptr, "KB") == 0)
			x *= SIZE_KB;
		else if (strcmp(endptr, "M") == 0 || strcmp(endptr, "MB") == 0)
			x *= SIZE_MB;
		else if (strcmp(endptr, "G") == 0 || strcmp(endptr, "GB") == 0)
			x *= SIZE_GB;
		else if (strcmp(endptr, "T") == 0 || strcmp(endptr, "TB") == 0)
			x *= SIZE_TB;
		else {
			UT_FATAL("invalid value for %s: \"%s\"", name,
				val);
		}
	}

	return x;
}

/*
 * parse_enum_param -- find an enum by name
 */
static uint64_t parse_enum_param(const char *val, const char *name,
	const char **enums)
{
	for (uint64_t x = 0; enums[x]; x++) {
		if (!strcasecmp(val, enums[x]))
			return x;
	}

	fprintf(stderr, "Unknown value of %s; valid ones:", name);
	for (uint64_t x = 0; enums[x]; x++)
		fprintf(stderr, " %s", enums[x]);
	fprintf(stderr, "\n");
	exit(1);
}

/*
 * parse_param_arg -- parse a single name=value arg
 */
static void parse_param_arg(const char *arg)
{
	const char *eq = strchr(arg, '=');
	if (!eq)
		UT_FATAL("params need to be var=value, got \"%s\"", arg);

	if (!eq[1])
		UT_FATAL("empty value in \"%s\"", arg);

	for (struct param_t *p = params; p->name; p++) {
		if (strncmp(p->name, arg, (size_t)(eq - arg)) ||
			p->name[eq - arg]) {
			continue;
		}

		uint64_t x = p->enums ?
			parse_enum_param(eq + 1, p->name, p->enums) :
			parse_uint_param(eq + 1, p->name);

		if (x < p->min) {
			UT_FATAL(
				"value for %s too small: wanted %lu..%lu, got %lu",
				p->name, p->min, p->max, x);
		}

		if (x > p->max) {
			UT_FATAL(
				"value for %s too big: wanted %lu..%lu, got %lu",
				p->name, p->min, p->max, x);
		}

		*p->var = x;
		return;
	}

	fprintf(stderr, "Unknown parameter \"%s\"; valid ones:", arg);
	for (struct param_t *p = params; p->name; p++)
		fprintf(stderr, " %s", p->name);
	fprintf(stderr, "\n");
	exit(1);
}

/*
 * parse_args -- parse all args
 */
static void parse_args(const char **argv)
{
	if (! *argv)
		UT_FATAL("Usage: "PROG" dir [arg=val] [...]");
	dir = *argv++;

	/*
	 * The dir argument is mandatory, but I expect users to forget about
	 * it most of the time.  Thus, let's validate it, requiring ./foo
	 * for local paths (almost anyone will use /tmp/ or /path/to/pmem).
	 * And, it's only for benchmarks anyway.
	 */
	if (*dir != '.' && !strchr(dir, '/'))
		UT_FATAL(
			"implausible dir -- prefix with ./ if you want %s",
			dir);

	for (; *argv; argv++)
		parse_param_arg(*argv);
}

static void
fill_key(char *key, uint64_t r)
{
	rng_t rng;
	randomize_r(&rng, r);

	size_t len = key_size;
	for (; len >= 8; len -= 8, key += 8)
		*((uint64_t *)key) = rnd64_r(&rng);

	if (!len)
		return;

	uint64_t rest = rnd64_r(&rng);
	memcpy(key, &rest, len);
}

static void *worker(void *arg)
{
	rng_t rng;
	randomize_r(&rng, 0);

	benchmark_time_t t1, t2;
	benchmark_time_get(&t1);

	for (uint64_t count = 0; count < ops_count; count++) {
		uint64_t obj = n_lowest_bits(rnd64_r(&rng), 3);

		char key[key_size + 1];
		fill_key(key, obj);

		char val[1];
		if (vmemcache_get(cache, key, key_size, val, sizeof(val), 0,
			NULL) <= 0) {
			if (vmemcache_put(cache, key, key_size, val, 1) &&
				errno != EEXIST) {
				UT_FATAL("vmemcache_put failed");
			}
		}
	}

	benchmark_time_get(&t2);
	benchmark_time_diff(&t1, &t1, &t2);

	return (void *)(intptr_t)(t1.tv_sec * NSECPSEC + t1.tv_nsec);
}

static void run_bench()
{
	cache = vmemcache_new(dir, cache_size, cache_fragment_size,
		(enum vmemcache_replacement_policy)repl_policy);
	if (!cache)
		UT_FATAL("vmemcache_new: %s (%s)", vmemcache_errormsg(), dir);

	os_thread_t th[MAX_THREADS];
	for (uint64_t i = 0; i < n_threads; i++) {
		if (os_thread_create(&th[i], 0, worker, 0))
			UT_FATAL("thread creation failed: %s", strerror(errno));
	}

	uint64_t total = 0;

	for (uint64_t i = 0; i < n_threads; i++) {
		uint64_t t;
		if (os_thread_join(&th[i], (void **)&t))
			UT_FATAL("thread join failed: %s", strerror(errno));
		total += t;
	}

	vmemcache_delete(cache);

	printf("Total time: %lu.%09lu s\n",
		total / NSECPSEC, total % NSECPSEC);
	total /= n_threads;
	total /= ops_count;
	printf("Avg time per op: %lu.%03lu μs\n",
		total / 1000, total % 1000);
}

int
main(int argc, const char **argv)
{
	parse_args(argv + 1);

	if (!n_threads) {
		n_threads = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
		if (n_threads > MAX_THREADS)
			n_threads = MAX_THREADS;
		if (!n_threads)
			UT_FATAL("can't obtain number of processor cores");
	}

	printf("Parameters:\n  %-20s : %s\n", "dir", dir);
	for (struct param_t *p = params; p->name; p++) {
		printf("  %-20s : ", p->name);
		if (p->enums) {
			uint64_t nvalid = 0;
			for (; p->enums[nvalid]; nvalid++)
				;

			if (*p->var < nvalid)
				printf("%s", p->enums[*p->var]);
			else
				printf("ENUM OUT OF RANGE: %lu", *p->var);
		} else
			printf("%lu", *p->var);
		printf("\n");
	}

	run_bench();

	return 0;
}
