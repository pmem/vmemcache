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
#include <errno.h>

#include "libvmemcache.h"
#include "test_helpers.h"
#include "os_thread.h"
#include "benchmark_time.h"

#define PROG "bench_simul"

static const char *dir;
static uint64_t n_threads = 100;
static uint64_t ops_count = 100000;
static uint64_t min_size  = 8;
static uint64_t max_size  = 8 * 1048576;

static struct param_t {
	const char *name;
	uint64_t *var;
	uint64_t min;
	uint64_t max;
} params[] = {
	{ "n_threads", &n_threads, 0 /* n_procs */, 4095 },
	{ "ops_count", &ops_count, 1, -1ULL },
	{ "min_size", &min_size, 1, -1ULL },
	{ "max_size", &max_size, 1, -1ULL },
	{ 0 },
};

/*
 * parse_param_arg -- parse a single name=value arg
 */
static void parse_param_arg(const char *arg)
{
	const char *eq = strchr(arg, '=');
	if (!eq)
		FATAL("params need to be var=value, got \"%s\"", arg);

	if (!eq[1])
		FATAL("empty value in \"%s\"", arg);

	for (struct param_t *p = params; p->name; p++) {
		if (strncmp(p->name, arg, (size_t)(eq - arg)) ||
			p->name[eq - arg]) {
			continue;
		}

		char *endptr;
		errno = 0;
		uint64_t x = strtoull(eq + 1, &endptr, 0);

		if (errno)
			FATAL("invalid value for %s: \"%s\"", p->name, eq + 1);

		if (*endptr)
			FATAL("invalid value for %s: \"%s\"", p->name, eq + 1);

		if (x < p->min) {
			FATAL("value for %s too small: wanted %lu..%lu, "
				"got %lu", p->name, p->min, p->max, x);
		}

		if (x > p->max) {
			FATAL("value for %s too big: wanted %lu..%lu, "
				"got %lu", p->name, p->min, p->max, x);
		}

		*p->var = x;
		return;
	}

	FATAL("unknown param \"%s\"", arg);
}

/*
 * parse_args -- parse all args
 */
static void parse_args(const char **argv)
{
	if (! *argv)
		FATAL("Usage: "PROG" dir [arg=val] [...]");
	dir = *argv++;

	/*
	 * The dir argument is mandatory, but I expect users to forget about
	 * it most of the time.  Thus, let's validate it, requiring ./foo
	 * for local paths (almost anyone will use /tmp/ or /path/to/pmem).
	 * And, it's only for benchmarkers anyway.
	 */
	if (*dir != '.' && !strchr(dir, '/'))
		FATAL("implausible dir -- prefix with ./ if you want %s", dir);

	for (; *argv; argv++)
		parse_param_arg(*argv);
}

int
main(int argc, const char **argv)
{
	parse_args(argv + 1);

	printf("Parameters:\n  %-20s : %s\n", "dir", dir);
	for (struct param_t *p = params; p->name; p++)
		printf("  %-20s : %lu\n", p->name, *p->var);

	return 0;
}
