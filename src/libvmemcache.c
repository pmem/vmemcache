/*
 * Copyright 2018, Intel Corporation
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
	/*
	 * Avoid expensive error string manipulation on very common error
	 * messages.
	 */
	switch (errno) {
	case ENOENT:
		return "entry not found in cache";
	case ESRCH:
		return "no entry eligible for eviction found";
	}
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
#error not implemented
	return out_get_errormsgW();
}

#endif
