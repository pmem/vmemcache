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
 * vmemcache_fragmentation.h -- fragmentation test header
 */

#ifndef VMEMCACHE_FRAGMENTATION_H
#define VMEMCACHE_FRAGMENTATION_H

#include "vmemcache_tests.h"
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

typedef struct {
	size_t allocated_size;
	size_t index;
	bool evicted;

} on_evict_info;

typedef struct {
	size_t pool_size;
	size_t segment_size;
	size_t val_max;
	char dir[PATH_MAX];
	char csv[PATH_MAX];
	int seconds, minutes, hours;
} test_params;

size_t get_rand_size(size_t val_max, size_t segment_size);
void on_evict(VMEMcache *cache, const char *key, size_t key_size, void *arg);
test_params parse_args(int argc, char **argv);
int timeout(time_t start, int seconds, int minutes, int hours);
void invalid_arg_msg(const char *arg);

#endif /* VMEMCACHE_FRAGMENTATION_H */
