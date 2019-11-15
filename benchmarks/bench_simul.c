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
#include <time.h>
#include <sys/mman.h>
#include <inttypes.h>

#include "libvmemcache.h"
#include "test_helpers.h"
#include "os_thread.h"
#include "benchmark_time.h"
#include "rand.h"
#include "util.h"

#define PROG "bench_simul"
#define MAX_THREADS 4096

#define SIZE_KB (1024ULL)
#define SIZE_MB (1024 * 1024ULL)
#define SIZE_GB (1024 * 1024 * 1024ULL)
#define SIZE_TB (1024 * 1024 * 1024 * 1024ULL)

#define NSECPSEC 1000000000

#define PUT_TAG (1ULL << 63)

/* type of statistics */
typedef unsigned long long stat_t;

enum simul_type {
	ST_INDEX,
	ST_REPL,
	ST_ALLOC,
	ST_FULL,
};

enum size_dist {
	SD_LINEAR,
	SD_A,
	SD_B,
};

static const char *dir;
static uint64_t n_threads = 0;
static uint64_t ops_count = 100000;
static uint64_t warm_up   = 100000;
static uint64_t min_size  = 8;
static uint64_t max_size  = 8 * SIZE_KB;
static uint64_t size_distrib = SD_B;
static uint64_t cache_size = VMEMCACHE_MIN_POOL;
static uint64_t cache_extent_size = VMEMCACHE_MIN_EXTENT;
static uint64_t repl_policy = VMEMCACHE_REPLACEMENT_LRU;
static uint64_t get_size = 1;
static uint64_t type = ST_FULL;
static uint64_t key_diversity = 5;
static uint64_t key_size = 16;
static uint64_t seed = 0;
static uint64_t junk_start = 0;
static uint64_t latency_samples = 0;
static const char *latency_file = NULL;

static VMEMcache *cache;
static int cache_is_full = 0;
static const void *lotta_zeroes;
static uint64_t *latencies = NULL;
static uint64_t vsize_seed;

/* case insensitive */
static const char *enum_repl[] = {
	"none",
	"LRU",
	0
};

static const char *enum_type[] = {
	"index",
	"repl",
	"alloc",
	"full",
	0
};

static const char *enum_size_distrib[] = {
	"linear",
	"a",
	"b",
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
	{ "warm_up", &warm_up, 0, -1ULL, NULL },
	{ "min_size", &min_size, 1, -1ULL, NULL },
	{ "max_size", &max_size, 1, -1ULL, NULL },
	{ "size_distrib", &size_distrib, SD_LINEAR, SD_B, enum_size_distrib },
	{ "cache_size", &cache_size, VMEMCACHE_MIN_POOL, -1ULL, NULL },
	{ "cache_extent_size", &cache_extent_size, VMEMCACHE_MIN_EXTENT,
		4 * SIZE_GB, NULL },
	{ "repl_policy", &repl_policy, 1, 1, enum_repl },
	{ "get_size", &get_size, 1, 4 * SIZE_GB, NULL },
	{ "type", &type, ST_INDEX, ST_FULL, enum_type },
	{ "key_diversity", &key_diversity, 1, 63, NULL },
	{ "key_size", &key_size, 1, SIZE_GB, NULL },
	{ "seed", &seed, 0, -1ULL, NULL },
	/* 100% fill the cache with bogus entries at the start */
	{ "junk_start", &junk_start, 0, 1, NULL },
	{ "latency_samples", &latency_samples, 0, SIZE_GB, NULL },
	{ "latency_file", NULL, 0, 0, NULL },
	{ 0 },
};

static struct {
	os_cond_t cond;
	os_mutex_t mutex;
	uint64_t wanted;
} ready;

#ifdef STATS_ENABLED
/* names of statistics */
static const char *stat_str[VMEMCACHE_STATS_NUM] = {
	"puts",
	"gets",
	"hits",
	"misses",
	"evicts",
	"cache entries",
	"DRAM size used",
	"pool size used",
	"heap entries",
};
#endif /* STATS_ENABLED */

static void print_stats(VMEMcache *cache);

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
 * parse_other_param -- params with custom behaviour
 */
static void parse_other_param(const char *val, const char *name)
{
	if (strcmp(name, "latency_file"))
		UT_FATAL("unknown other_param");

	latency_file = val;
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

		if (!p->var) {
			parse_other_param(eq + 1, p->name);
			return;
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

/* 64-bit randomness -> float [0..1) */
static double
rnddouble(uint64_t x)
{
	return (double)x / (65536.0 * 65536 * 65536 * 65536);
}

/* linear [0..1) -> exp/etc distributed [0..1) */
static double
rndlength(uint64_t id)
{
	switch (size_distrib) {

	case SD_LINEAR:
		return rnddouble(id);

	case SD_A:
	{
		/* polynomial (x⁴) */
		double x = rnddouble(id);
		return x * x * x * x;
	}

	case SD_B:
	{
		/* piecewise-linear exponential */
		uint64_t magnitude = id >> (64 - 5); /* 0..31 */

		/* Pick a power of two. */
		uint64_t y = 1ULL << magnitude;

		/* Fill lower bits randomly. */
		uint64_t x = y | ((y - 1) & id);

		return (double)x / (65536.0 * 65536);
	}

	default:
		/* someone scribbled over our memory...? */
		UT_FATAL("invalid enum for size distrib");
	}
}

static inline uint64_t getticks(void)
{
	struct timespec tv;
	clock_gettime(CLOCK_MONOTONIC, &tv);
	return (uint64_t)tv.tv_sec * NSECPSEC + (uint64_t)tv.tv_nsec;
}

static void
run_ops(uint64_t ops, rng_t *rng, uint64_t *lat, void *get_buffer)
{
	uint64_t opt, opt_tag;

	for (uint64_t count = 0; count < ops; count++) {
		uint64_t obj = n_lowest_bits(rnd64_r(rng), (int)key_diversity);

		char key[key_size + 1];
		fill_key(key, obj);

		if (lat)
			opt = getticks();

		if (vmemcache_get(cache, key, key_size, get_buffer, get_size, 0,
			NULL) < 0) {

			uint64_t size = min_size
				+ (uint64_t)((double)(max_size - min_size + 1)
				* rndlength(hash64(obj ^ vsize_seed)));

			if (vmemcache_put(cache, key, key_size, lotta_zeroes,
				size) && errno != EEXIST) {
				print_stats(cache);
				UT_FATAL("vmemcache_put failed: %s",
						vmemcache_errormsg());
			}

			opt_tag = PUT_TAG;
		} else {
			opt_tag = 0;
		}

		if (lat)
			*lat++ = (getticks() - opt) | opt_tag;
	}
}

static void __attribute__((noinline))
run_warm_up(rng_t *rng, void *get_buffer)
{
	run_ops(warm_up, rng, NULL, get_buffer);

	/* Prevent tail call optimizations (to force stack frame, for perf). */
	getpid();
}

static void *worker(void *arg)
{
	rng_t rng;
	randomize_r(&rng, seed ? seed + (uintptr_t)arg : 0);

	void *get_buffer = malloc(get_size);
	if (!get_buffer)
		UT_FATAL("couldn't allocate get_buffer");

	uint64_t *lat = NULL;
	if (latencies)
		lat = latencies + ops_count * (uintptr_t)arg;

	run_warm_up(&rng, get_buffer);

	os_mutex_lock(&ready.mutex);
	if (--ready.wanted)
		os_cond_wait(&ready.cond, &ready.mutex);
	else {
		/* If warm_up disabled memcpy, re-enable it. */
		vmemcache_bench_set(cache, VMEMCACHE_BENCH_NO_MEMCPY,
			type < ST_FULL);

		printf("Starting measured run...\n");

		os_cond_broadcast(&ready.cond);
	}
	os_mutex_unlock(&ready.mutex);

	benchmark_time_t t1, t2;
	benchmark_time_get(&t1);

	run_ops(ops_count, &rng, lat, get_buffer);

	benchmark_time_get(&t2);
	benchmark_time_diff(&t1, &t1, &t2);

	free(get_buffer);

	return (void *)(intptr_t)(t1.tv_sec * NSECPSEC + t1.tv_nsec);
}

#ifdef STATS_ENABLED
/*
 * get_stat -- (internal) get one statistic
 */
static void
get_stat(VMEMcache *cache, stat_t *stat_vals, enum vmemcache_statistic i_stat)
{
	int ret = vmemcache_get_stat(cache, i_stat,
				&stat_vals[i_stat], sizeof(*stat_vals));
	if (ret == -1)
		UT_FATAL("vmemcache_get_stat: %s", vmemcache_errormsg());
}
#endif /* STATS_ENABLED */

/*
 * print_stats -- (internal) print all statistics
 */
static void
print_stats(VMEMcache *cache)
{
#ifdef STATS_ENABLED
	stat_t stat_vals[VMEMCACHE_STATS_NUM];

	get_stat(cache, stat_vals, VMEMCACHE_STAT_PUT);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_GET);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_HIT);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_MISS);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_EVICT);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_ENTRIES);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_DRAM_SIZE_USED);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_POOL_SIZE_USED);
	get_stat(cache, stat_vals, VMEMCACHE_STAT_HEAP_ENTRIES);

	float pool_used_percent =
			(100 * (float)stat_vals[VMEMCACHE_STAT_POOL_SIZE_USED])
				/ (float)cache_size;
	float hits_percent =
			(100 * (float)stat_vals[VMEMCACHE_STAT_HIT])
				/ (float)stat_vals[VMEMCACHE_STAT_GET];

	printf("\nStatistics:\n");
	for (int i = 0; i < VMEMCACHE_STATS_NUM; i++)
		printf("  %-20s : %llu\n", stat_str[i], stat_vals[i]);
	printf("  %-20s : %.2f %%\n", "pool size used [%]", pool_used_percent);
	printf("  %-20s : %.2f %%\n", "hits [%]", hits_percent);

	printf("\n");
#endif /* STATS_ENABLED */
}

/*
 * on_evict_cb -- (internal) 'on evict' callback for run_test_get
 */
static void
on_evict_cb(VMEMcache *cache, const void *key, size_t key_size, void *arg)
{
	cache_is_full = 1;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t l = *(uint64_t *)a;
	uint64_t r = *(uint64_t *)b;

	if (l < r)
		return -1;
	if (l > r)
		return 1;
	return 0;
}

static void print_ntiles(FILE *f, uint64_t *t, uint64_t n)
{
	if (!n) {
		fprintf(f, "-\n");
		return;
	}

	/* special case: if only one value is called for, give median */
	if (latency_samples == 1) {
		fprintf(f, "%llu\n", t[n / 2] & ~PUT_TAG);
		return;
	}

	/* otherwise, give minimum, evenly spaced values, then maximum */
	for (uint64_t i = 0; i < latency_samples; i++) {
		fprintf(f, i ? ";%llu" : "%llu", t[i * (n - 1) /
			(latency_samples - 1)] & ~PUT_TAG);
	}
	fprintf(f, "\n");
}

static void dump_latencies()
{
	FILE *f = stdout;

	if (latency_file) {
		f = fopen(latency_file, "w");
		if (!f) {
			UT_FATAL("can't create latency file: %s",
				strerror(errno));
		}
	}

	qsort(latencies, n_threads * ops_count, sizeof(uint64_t), cmp_u64);

	/* sentinel */
	latencies[ops_count * n_threads] = -1ULL;

	uint64_t *latm = latencies;
	for (; !(*latm & PUT_TAG); latm++)
		;

	uint64_t nhits = (uint64_t)(latm - latencies);
	uint64_t nmiss = n_threads * ops_count - nhits;

	print_ntiles(f, latencies, nhits);
	print_ntiles(f, latm, nmiss);

	if (latency_file)
		fclose(f);
}

static void run_bench()
{
	rng_t rng;
	randomize_r(&rng, seed);
	vsize_seed = rnd64_r(&rng);

	os_cond_init(&ready.cond);
	os_mutex_init(&ready.mutex);
	ready.wanted = n_threads;

	cache = vmemcache_new();
	vmemcache_set_size(cache, cache_size);
	vmemcache_set_extent_size(cache, cache_extent_size);
	vmemcache_set_eviction_policy(cache,
		(enum vmemcache_repl_p)repl_policy);
	if (vmemcache_add(cache, dir))
		UT_FATAL("vmemcache_add: %s (%s)", vmemcache_errormsg(), dir);

	if (latency_samples) {
		latencies = malloc((ops_count * n_threads + 1) *
			sizeof(uint64_t));
		if (!latencies)
			UT_FATAL("can't malloc latency ledger");

		/* sentinel */
		latencies[ops_count * n_threads] = -1ULL;
	}

	if (junk_start) {
		printf("Pre-filling the cache with junk...\n");

		char junk[256];
		memset(junk, '!' /* arbitrary */, sizeof(junk));

		vmemcache_callback_on_evict(cache, on_evict_cb, NULL);
		uint64_t ndummies = 0;
		while (!cache_is_full) {
			ndummies++;
			vmemcache_put(cache, &ndummies, sizeof(ndummies),
				junk, sizeof(junk));
		}
		vmemcache_callback_on_evict(cache, NULL, NULL);
	}

	vmemcache_bench_set(cache, VMEMCACHE_BENCH_INDEX_ONLY,
		type <= ST_INDEX);
	/* memcpy is enabled after warm_up */
	vmemcache_bench_set(cache, VMEMCACHE_BENCH_NO_MEMCPY, 1);
	/* but if there's any warm_up, touch the space once */
	if (warm_up)
		vmemcache_bench_set(cache, VMEMCACHE_BENCH_PREFAULT, 1);

	printf("Spawning threads...\n");

	os_thread_t th[MAX_THREADS];
	for (uint64_t i = 0; i < n_threads; i++) {
		if (os_thread_create(&th[i], 0, worker, (void *)i))
			UT_FATAL("thread creation failed: %s", strerror(errno));
	}

	uint64_t total = 0;

	for (uint64_t i = 0; i < n_threads; i++) {
		uintptr_t t;
		if (os_thread_join(&th[i], (void **)&t))
			UT_FATAL("thread join failed: %s", strerror(errno));
		total += t;
	}

	printf("Done.\n");

	print_stats(cache);

	if (latencies)
		dump_latencies();

	vmemcache_delete(cache);

	printf("Total time: %lu.%09lu s\n",
		total / NSECPSEC, total % NSECPSEC);
	total /= n_threads;
	total /= ops_count;
	printf("Avg time per op: %lu.%03lu μs\n",
		total / 1000, total % 1000);

	free(latencies);
}

static void
print_units(uint64_t x)
{
	if (x == -1ULL) {
		printf("∞");
		return;
	}

	const char *units[] = { "", "K", "M", "G", "T", "P", "E" };
	int u = 0;

	while (x && !(x % 1024)) {
		u++;
		x /= 1024;
	}

	printf("%"PRIu64"%s", x, units[u]);
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

	if (min_size > max_size)
		UT_FATAL("min_size > max_size");

	printf("Parameters:\n  %-20s : %s\n", "dir", dir);
	for (struct param_t *p = params; p->name; p++) {
		if (!p->var)
			continue;

		printf("  %-20s : ", p->name);
		if (p->enums) {
			uint64_t nvalid = 0;
			for (; p->enums[nvalid]; nvalid++)
				;

			if (*p->var < nvalid)
				printf("%s", p->enums[*p->var]);
			else
				printf("enum out of range: %lu", *p->var);
		} else
			print_units(*p->var);
		printf("\n");
	}

	lotta_zeroes = mmap(NULL, max_size, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (!lotta_zeroes) {
		UT_FATAL("couldn't grab a zero buffer: mmap failed: %s",
			strerror(errno));
	}

	run_bench();

	return 0;
}
