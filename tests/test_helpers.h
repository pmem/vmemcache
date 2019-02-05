/*
 * Copyright 2018-2019, Intel Corporation
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
 * test_helpers.h -- header with helpers
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H 1

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#define UT_ERR(...) do {\
	fprintf(stderr, "ERROR: " __VA_ARGS__);\
	fprintf(stderr, "\n");\
} while (/*CONSTCOND*/0)

#define UT_FATAL(...) do {\
	fprintf(stderr, "FATAL ERROR at %s:%i in %s(): ",\
			__FILE__, __LINE__, __func__);\
	fprintf(stderr, __VA_ARGS__);\
	fprintf(stderr, "\n");\
	abort();\
} while (/*CONSTCOND*/0)

/* names of statistics */
static const char *stat_str[VMEMCACHE_STATS_NUM] = {
	"puts",
	"gets",
	"hits",
	"misses",
	"evicts",
	"cache entries",
	"DRAM size used",
	"pool size used"
};

/*
 * str_to_unsigned -- (internal) convert string argument to unsigned int
 */
static inline int
str_to_unsigned(const char *str, unsigned *value)
{
	char *endptr = NULL;

	errno = 0;    /* to distinguish success/failure after call */

	unsigned long val = strtoul(str, &endptr, 10);
	if ((errno == ERANGE && val == ULONG_MAX) ||
	    (errno != 0 && val == 0) ||
	    (endptr == str) || (*endptr != '\0')) {
		UT_ERR("strtoul() failed to convert the string %s", str);
		return -1;
	}

	if (val > UINT_MAX) {
		UT_ERR("value %s is bigger than UINT_MAX (%u)", str, UINT_MAX);
		return -1;
	}

	*value = (unsigned)val;

	return 0;
}

#endif /* TEST_HELPERS_H */
