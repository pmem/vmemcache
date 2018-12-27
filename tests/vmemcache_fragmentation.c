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

#include "vmemcache_fragmentation.h"

/* Generate random size value */
size_t
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

/* Callback function on evict, used for calculating allocated space size */
void
on_evict(VMEMcache * cache, const char *key, size_t key_size, void *arg)
{
	on_evict_info *info = (on_evict_info *) arg;
	info->evicted = true;
	char buf[1];
	size_t vsize = 0;

	ssize_t ret;
	if ((ret =
		vmemcache_get(cache, key, key_size, &buf, 1, 0, &vsize)) != 1) {
		FATAL("vmemcache_get: invalid ret (actual %zu, expected 1). %s",
		ret, strerror(errno));
	}
	info->allocated_size -= vsize;
}

/* Return 0 if timeout occurs, 1 otherwise */
int
timeout(time_t start, int seconds, int minutes, int hours)
{
	time_t end;
	int diff;
	int timeout = seconds + 60 * minutes + 3600 * hours;

	/* run indefinitely */
	if (timeout == 0)
		return 1;

	time(&end);

	diff = (int)difftime(end, start);
	if (diff >= timeout) {
		return 0;
	}
	return 1;
}

/* Parse cmd args */
test_params
parse_args(int argc, char **argv)
{
	test_params p = {.pool_size = VMEMCACHE_MIN_POOL,
		.segment_size = 16,
		.val_max = 0,
		.dir = "/tmp",
		.csv = "",
		.seconds = 0,
		.minutes = 0,
		.hours = 0
	};
	size_t val_max_factor = 70;

	static const char *usage_str = "usage: %s "
	    "[-h] "
	    "[-p <pool_size>] "
	    "[-s <segment_size>] "
	    "[-v <val_max_factor>] "
	    "[-t <timeout_seconds>] "
	    "[-m <timeout_minutes>] "
	    "[-o <timeout_hours>] " "[-c <csv>] " "[-d <dir>]\n ";

	const char *optstr = "hp:s:v:t:m:o:d:c:";
	int opt;
	while ((opt = getopt(argc, argv, optstr)) != -1) {
		char *endptr;
		switch (opt) {
		case 'h':
			printf(usage_str, argv[0]);
			exit(0);
		case 'p':
			p.pool_size = (size_t) (strtoumax(optarg, &endptr, 10));
			if (strlen(endptr) != 0)
				FATAL("invalid pool size provided");
			break;
		case 's':
			p.segment_size =
			    (size_t) (strtoumax(optarg, &endptr, 10));
			if (strlen(endptr) != 0)
				FATAL("invalid segment size provided");
			break;
		case 'v':
			val_max_factor =
			    (size_t) (strtoumax(optarg, &endptr, 10));
			if (strlen(endptr) != 0)
				FATAL("invalid val max factor ovided");
			break;
		case 't':
			p.seconds = (int)(strtoul(optarg, &endptr, 10));
			if (strlen(endptr) != 0)
				FATAL("invalid seconds provided");
			break;
		case 'm':
			p.minutes = (int)(strtoul(optarg, &endptr, 10));
			if (strlen(endptr) != 0)
				FATAL("invalid minutes provided");
			break;
		case 'o':
			p.hours = (int)(strtoul(optarg, &endptr, 10));
			if (strlen(endptr) != 0)
				FATAL("invalid hours provided");
			break;
		case 'd':
			strcpy(p.dir, optarg);
			break;
		case 'c':
			strcpy(p.csv, optarg);
			break;
		}
	}
	p.val_max = val_max_factor * p.segment_size;

	return p;
}

int
main(int argc, char **argv)
{
	test_params p = parse_args(argc, argv);

	char *val = NULL;
	FILE *fcsv = NULL;
	VMEMcache *vc = NULL;

	/* init pool */
	vc = vmemcache_new(p.dir, p.pool_size, p.segment_size, 1);

	if (!vc)
		FATAL("vmemcache_new: %s", p.dir);

	on_evict_info info = { 0, 0, false };
	vmemcache_callback_on_evict(vc, on_evict, &info);


	/* create log file */
	if (strlen(p.csv) != 0) {
		fcsv = fopen(p.csv, "w");
		if (!fcsv) {
			perror(p.csv);
			goto err;
		}
		fprintf(fcsv, "keynum,ratio\n");
	}

	size_t keynum = 0;
	size_t keysize = sizeof(size_t);

	float prev;
	float ratio = 0.0f;

	long seed = time(NULL);
	srand((unsigned)seed);

	time_t start;
	time(&start);
	while (timeout(start, p.seconds, p.minutes, p.hours)) {

		/* create key */
		char key[keysize];
		sprintf(key, "%zu", keynum);

		/* generate val */
		size_t val_size = get_rand_size(p.val_max, p.segment_size);

		val = malloc(val_size);
		if (!val) {
			goto err;
		}
		memset(val, 'a', val_size - 1);
		val[val_size - 1] = '\0';

		info.allocated_size += val_size;

		/* put */
		int ret = vmemcache_put(vc, key, keysize, val, val_size);
		free(val);
		val = NULL;
		if (ret != 0) {
			fprintf(stderr, "vmemcache_put ret: %d\n", ret);
			goto err;
		}

		/* write to log file */
		ratio = (float)info.allocated_size / (float)p.pool_size;
		if (strlen(p.csv) != 0) {
			/*
			 * Do not write the ratio value to csv if it is the same
			 * as the previous one. The intent is to reduce
			 * unnecessary bloating of the csv file.
			 */
			if (keynum == 0 || roundf(ratio * 100)
				!= roundf(prev * 100)) {
				fprintf(fcsv, "%zu,%.2f\n", keynum, ratio);
				prev = ratio;
			}
		}
		if (info.evicted && ratio < ALLOWED_RATIO) {
			fprintf(stderr,
				"insufficient space utilization. ratio: %.2f: seed %ld",
				ratio, seed);
			goto err;
		}
		++keynum;
	}
	vmemcache_delete(vc);
	vc = NULL;

	/* always write the last record to csv */
	if (strlen(p.csv) != 0) {
		fprintf(fcsv, "%zu,%.2f\n", keynum, ratio);
		fclose(fcsv);
		fcsv = NULL;
	}
	return 0;

err:
	if (val)
		free(val);
	if (fcsv)
		fclose(fcsv);
	if (vc)
		vmemcache_delete(vc);
	return 1;
}
