// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2016-2019, Intel Corporation */

/*
 * common.h -- common definitions
 */

#ifndef COMMON_H
#define COMMON_H 1

#include "util.h"
#include "out.h"
#include "mmap.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void
common_init(const char *log_prefix, const char *log_level_var,
		const char *log_file_var, int major_version,
		int minor_version)
{
	util_init();
	out_init(log_prefix, log_level_var, log_file_var, major_version,
		minor_version);
}

static inline void
common_fini(void)
{
	out_fini();
}

#ifdef __cplusplus
}
#endif

#endif
