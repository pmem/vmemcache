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
 * vmemcache_test_utilization.c -- space utilization test source
 */

#include "test_helpers.h"
#include <errno.h>
#include <inttypes.h>
#include <libvmemcache.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ALLOWED_RATIO 0.95
#define MAX_KEYSIZE 30

typedef struct {
	bool evicted;
} on_evict_info;

typedef struct {
	size_t pool_size;
	size_t extent_size;
	size_t val_max;
	char dir[PATH_MAX];
	long seconds;
} test_params;

static const char *usage_str = "usage: %s "
	"-d <dir> "
	"[-p <pool_size>] "
	"[-s <extent_size>] "
	"[-v <val_max_factor>] "
	"[-t <timeout_seconds>] "
	"[-m <timeout_minutes>] "
	"[-o <timeout_hours>] "
	"[-h]\n";


/*
 * on_evict - (internal) on evict callback function
 */
static void
on_evict(VMEMcache *cache, const void *key, size_t key_size, void *arg)
{
	on_evict_info *info = (on_evict_info *)arg;
	info->evicted = true;
}

/*
 * parse_ull - (internal) try parsing unsigned long long command line argument
 */
static unsigned long long
parse_ull(const char *valname, const char *prog)
{
	unsigned long long val;
	if (str_to_ull(optarg, &val) != 0) {
		fprintf(stderr, "invalid %s value\n", valname);
		printf(usage_str, prog);
		exit(1);
	}
	return val;
}

/*
 * parse_unsigned - (internal) try parsing unsigned command line argument
 */
static unsigned
parse_unsigned(const char *valname, const char *prog)
{
	unsigned val;
	if (str_to_unsigned(optarg, &val) != 0) {
		fprintf(stderr, "invalid %s value\n", valname);
		printf(usage_str, prog);
		exit(1);
	}
	return val;
}

/*
 * argerror - (internal) exit with message on command line argument error
 */
static void
argerror(const char *msg, const char *prog)
{
	fprintf(stderr, "%s", msg);
	printf(usage_str, prog);
	exit(1);
}

/*
 * parse_args - (internal) parse command line arguments
 */
static test_params
parse_args(int argc, char **argv)
{
	test_params p = {
		.pool_size = VMEMCACHE_MIN_POOL,
		.extent_size = VMEMCACHE_MIN_EXTENT,
		.val_max = 0,
		.dir = "",
		.seconds = 0,
	};
	size_t val_max_factor = 70;

	const char *optstr = "hp:s:v:t:m:o:d:";
	int opt;
	long seconds = 0;
	long minutes = 0;
	long hours = 0;
	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case 'h':
			printf(usage_str, argv[0]);
			exit(0);
		case 'p':
			p.pool_size =
				(size_t)parse_ull("pool size", argv[0]);
			break;
		case 's':
			p.extent_size =
				(size_t)parse_ull("extent size", argv[0]);
			break;
		case 'v':
			val_max_factor =
			    (size_t)parse_ull("val max factor", argv[0]);
			break;
		case 't':
			seconds = parse_unsigned("seconds", argv[0]);
			break;
		case 'm':
			minutes = parse_unsigned("minutes", argv[0]);
			break;
		case 'o':
			hours = parse_unsigned("hours", argv[0]);
			break;
		case 'd':
			if (*optarg == 0)
				argerror("invalid dir argument\n", argv[0]);
			strcpy(p.dir, optarg);
			break;
		default:
			argerror("", argv[0]);
			break;
		}
	}

	if (*p.dir == 0)
		argerror("missing required dir argument\n", argv[0]);

	p.seconds = seconds + 60 * minutes + 3600 * hours;
	if (p.seconds <= 0)
		argerror("timeout must be greater than 0\n", argv[0]);

	p.val_max = val_max_factor * p.extent_size;

	return p;
}


/*
 * put_until_timeout - (internal) put random-sized values into cache,
 * print utilization ratio as a csv
 */
static int
put_until_timeout(VMEMcache *vc, const test_params *p)
{
	int ret = 1;

	on_evict_info info = { false };
	vmemcache_callback_on_evict(vc, on_evict, &info);

	/* print csv header */
	printf("keynum,ratio\n");

	float prev_ratio;
	float ratio = 0.0f;
	bool print_ratio = false;

	long seed = time(NULL);
	srand((unsigned)seed);

	char *val = malloc(p->val_max);
	if (val == NULL) {
		fprintf(stderr, "malloc: cannot allocate memory (%zu bytes)\n",
				p->val_max);
		return ret;
	}

	size_t val_size;
	unsigned long long used_size;
	char key[MAX_KEYSIZE];
	int len;
	size_t keynum = 0;

	long endtime = time(NULL) + p->seconds;
	while (endtime > time(NULL)) {
		/* create key */
		len = sprintf(key, "%zu", keynum);
		if (len < 0) {
			fprintf(stderr, "sprintf return value: %d\n", len);
			goto exit_free;
		}

		/* generate value */
		val_size = get_granular_rand_size(p->val_max, p->extent_size);

		/* put */
		int ret = vmemcache_put(vc, key, (size_t)len, val, val_size);
		if (ret != 0) {
			fprintf(stderr, "vmemcache_put: %s\n",
					vmemcache_errormsg());
			goto exit_free;
		}

#ifdef STATS_ENABLED
		if (vmemcache_get_stat(vc, VMEMCACHE_STAT_POOL_SIZE_USED,
					&used_size, sizeof(used_size)) != 0) {
			fprintf(stderr, "vmemcache_get_stat: %s\n",
					vmemcache_errormsg());
			goto exit_free;
		}
#else
		/*
		 * This test will always pass and show 100% utilization,
		 * if statistics are disabled.
		 */
		used_size = p->pool_size;
#endif /* STATS_ENABLED */

		/*
		 * Do not print the csv line if current ratio value is the same
		 * (taking precision into account) as the previous one. The
		 * intent is to avoid unnecessary bloating of the csv output.
		 */
		ratio = (float)used_size / (float)p->pool_size;
		print_ratio = keynum == 0 || lroundf(ratio * 100)
			!= lroundf(prev_ratio * 100);
		if (print_ratio) {
			printf("%zu,%.3f\n", keynum, ratio);
			prev_ratio = ratio;
		}

		if (info.evicted && ratio < ALLOWED_RATIO) {
			fprintf(stderr,
				"insufficient space utilization. ratio: %.3f: seed %ld\n",
				ratio, seed);
			goto exit_free;
		}

		++keynum;
	}

	ret = 0;

	/* print the last csv line if already not printed */
	if (!print_ratio)
		printf("%zu,%.3f\n", keynum - 1, ratio);

exit_free:
	free(val);

	return ret;
}

int
main(int argc, char **argv)
{
	test_params p = parse_args(argc, argv);

	VMEMcache *vc = vmemcache_new(p.dir, p.pool_size, p.extent_size, 1);
	if (vc == NULL)
		UT_FATAL("vmemcache_new: %s (%s)", vmemcache_errormsg(), p.dir);

	int ret = put_until_timeout(vc, &p);

	vmemcache_delete(vc);

	return ret;
}
