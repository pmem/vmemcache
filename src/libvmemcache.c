// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018, Intel Corporation */

/*
 * libvmemcache.c -- libvmemcache entry points
 */

#include <stdio.h>
#include <stdint.h>

#include "common.h"
#include "libvmemcache.h"
#include "vmemcache.h"

/*
 * vmcache_init -- load-time initialization for vmcache
 *
 * Called automatically by the run-time loader.
 */
ATTR_CONSTRUCTOR
void
libvmemcache_init(void)
{
	common_init(VMEMCACHE_PREFIX, VMEMCACHE_LEVEL_VAR,
			VMEMCACHE_FILE_VAR, VMEMCACHE_MAJOR_VERSION,
			VMEMCACHE_MINOR_VERSION);
	LOG(3, NULL);
}

/*
 * libvmemcache_fini -- libvmemcache cleanup routine
 *
 * Called automatically when the process terminates.
 */
ATTR_DESTRUCTOR
void
libvmemcache_fini(void)
{
	LOG(3, NULL);
	common_fini();
}

/*
 * vmemcache_errormsgU -- return last error message
 */
#ifndef _WIN32
static inline
#endif
const char *
vmemcache_errormsgU(void)
{
	return out_get_errormsg();
}

#ifndef _WIN32
/*
 * vmemcache_errormsg -- return last error message
 */
const char *
vmemcache_errormsg(void)
{
	return vmemcache_errormsgU();
}
#else
/*
 * vmemcache_errormsgW -- return last error message as wchar_t
 */
const wchar_t *
vmemcache_errormsgW(void)
{
	return out_get_errormsgW();
}

#endif
