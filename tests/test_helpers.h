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
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
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
	if (errno)\
		fprintf(stderr, "ERRNO: %s (%i)\n",\
			strerror(errno), errno);\
	abort();\
} while (/*CONSTCOND*/0)

#define UT_ASSERTeq(x, y) do if ((x) != (y)) {\
	UT_FATAL("ASSERT FAILED : " #x " (%llu) ≠ %llu",\
		(unsigned long long)(x), (unsigned long long)(y));\
} while (/*CONSTCOND*/0)

#define UT_ASSERTin(x, min, max) do if ((x) < (min) || (x) > (max)) {\
	UT_FATAL("ASSERT FAILED : " #x " = %llu not in [%llu,%llu]",\
		(unsigned long long)(x),\
		(unsigned long long)(min), (unsigned long long)(max));\
} while (/*CONSTCOND*/0)


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

/*
 * str_to_ull -- (internal) convert string argument to unsigned long long
 */
static inline int
str_to_ull(const char *str, unsigned long long *value)
{
	char *endptr = NULL;

	errno = 0;    /* to distinguish success/failure after call */

	unsigned long long val = strtoull(str, &endptr, 10);
	if ((errno == ERANGE && val == ULLONG_MAX) ||
	    (errno != 0 && val == 0) ||
	    (endptr == str) || (*endptr != '\0')) {
		UT_ERR("strtoull() failed to convert the string %s", str);
		return -1;
	}

	*value = (unsigned long long)val;

	return 0;
}

/*
 * get_granular_rand_size - (internal) generate random size value
 * with specified granularity
 */
static inline size_t
get_granular_rand_size(size_t val_max, size_t granularity)
{
	size_t val_size =
	    (1 + (size_t) rand() / (RAND_MAX / (val_max / granularity) + 1)) *
	    granularity;

	assert(val_size <= val_max);
	assert(val_size >= granularity);
	assert(val_size % granularity == 0 &&
		"put value size must be a multiple of granularity");

	return val_size;
}

#endif /* TEST_HELPERS_H */
