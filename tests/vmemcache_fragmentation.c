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
 * vmemcache_fragmentation.c -- fragmentation test source
 */

#include "test_helpers.h"
#include <assert.h>
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
	size_t segment_size;
	size_t val_max;
	char dir[PATH_MAX];
	long seconds;
} test_params;

static const char *usage_str = "usage: %s "
	"-d <dir> "
	"[-p <pool_size>] "
	"[-s <segment_size>] "
	"[-v <val_max_factor>] "
	"[-t <timeout_seconds>] "
	"[-m <timeout_minutes>] "
	"[-o <timeout_hours>] "
	"[-h]\n";

/*
 * get_rand_size - (internal) generate random size value
 */
static size_t
get_rand_size(size_t val_max, size_t segment_size)
{
	size_t val_size =
	    (1 + (size_t) rand() / (RAND_MAX / (val_max / segment_size) + 1)) *
	    segment_size;

	assert(val_size <= val_max);
	assert(val_size >= segment_size);
	assert(val_size % segment_size == 0 &&
		"put value size must be a multiple of segment size");

	return val_size;
}

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
 * parse_umax - (internal) convert string to uintmax_t
 */
static uintmax_t
parse_umax(const char *valname, const char *prog)
{
	char *endptr;
	errno = 0;
	uintmax_t val = strtoumax(optarg, &endptr, 10);
	if (*endptr != 0 || (errno == ERANGE && val == UINTMAX_MAX)) {
		fprintf(stderr, "invalid %s value\n", valname);
		printf(usage_str, prog);
		exit(1);
	}
	return val;
}

/*
 * parse_long - (internal) convert string to long
 */
static long
parse_long(const char *valname, const char *prog)
{
	char *endptr;
	errno = 0;
	long val = strtol(optarg, &endptr, 10);
	if (*endptr != 0 || (errno == ERANGE && val == LONG_MAX)) {
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
		.segment_size = 16,
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
				(size_t)parse_umax("pool size", argv[0]);
			break;
		case 's':
			p.segment_size =
				(size_t)parse_umax("segment size", argv[0]);
			break;
		case 'v':
			val_max_factor =
			    (size_t)parse_umax("val max factor", argv[0]);
			break;
		case 't':
			seconds = parse_long("seconds", argv[0]);
			break;
		case 'm':
			minutes = parse_long("minutes", argv[0]);
			break;
		case 'o':
			hours = parse_long("hours", argv[0]);
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

	p.val_max = val_max_factor * p.segment_size;

	return p;
}

int
main(int argc, char **argv)
{
	test_params p = parse_args(argc, argv);

	/* init pool */
	VMEMcache *vc = vmemcache_new(p.dir, p.pool_size, p.segment_size, 1);
	if (vc == NULL)
		FATAL("vmemcache_new: %s (%s)", vmemcache_errormsg(), p.dir);

	on_evict_info info = { false };
	vmemcache_callback_on_evict(vc, on_evict, &info);

	/* print csv header */
	printf("keynum,ratio\n");

	float prev_ratio;
	float ratio = 0.0f;
	bool print_ratio = false;

	long seed = time(NULL);
	srand((unsigned)seed);

	char val;
	size_t val_size;
	size_t used_size;
	char key[MAX_KEYSIZE];
	size_t keynum = 0;

	long endtime = time(NULL) + p.seconds;
	while (endtime > time(NULL)) {
		/* create key */
		sprintf(key, "%zu", keynum);

		/* generate value */
		val_size = get_rand_size(p.val_max, p.segment_size);

		/* put */
		int ret = vmemcache_put(vc, key, MAX_KEYSIZE, &val, val_size);
		if (ret != 0) {
			fprintf(stderr, "vmemcache_put %s\n",
					vmemcache_errormsg());
			goto err;
		}

		if (vmemcache_get_stat(vc, VMEMCACHE_STAT_POOL_SIZE_USED,
					&used_size, sizeof(used_size)) != 0) {
			fprintf(stderr, "vmemcache_get_stat: %s\n",
					vmemcache_errormsg());
			goto err;
		}

		/*
		 * Do not print the csv line if current ratio value is the same
		 * (taking precision into account) as the previous one. The
		 * intent is to avoid unnecessary bloating of the csv output.
		 */
		ratio = (float)used_size / (float)p.pool_size;
		print_ratio = keynum == 0 || lroundf(ratio * 100)
			!= lroundf(prev_ratio * 100);
		if (print_ratio) {
			printf("%zu,%.2f\n", keynum, ratio);
			prev_ratio = ratio;
		}

		if (info.evicted && ratio < ALLOWED_RATIO) {
			fprintf(stderr,
				"insufficient space utilization. ratio: %.2f: seed %ld\n",
				ratio, seed);
			goto err;
		}

		++keynum;
	}

	/* XXX: should be done by vmemcache_delete */
	while (vmemcache_evict(vc, NULL, 0) == 0)
		;

	vmemcache_delete(vc);

	/* print the last csv line if already not printed */
	if (!print_ratio)
		printf("%zu,%.2f\n", keynum - 1, ratio);

	return 0;

err:
	if (vc != NULL) {
		/* XXX: should be done by vmemcache_delete */
		while (vmemcache_evict(vc, NULL, 0) == 0)
			;

		vmemcache_delete(vc);
	}

	return 1;
}
