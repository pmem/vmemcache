// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2019, Intel Corporation */

#include <libvmemcache.h>
#include <stdio.h>
#include <string.h>

#define STR_AND_LEN(x) (x), strlen(x)

static VMEMcache *cache;

static void
on_miss(VMEMcache *cache, const void *key, size_t key_size, void *arg)
{
	vmemcache_put(cache, STR_AND_LEN("meow"),
		STR_AND_LEN("Cthulhu fthagn"));
}

static void
get(const char *key)
{
	char buf[128];
	ssize_t len = vmemcache_get(cache, STR_AND_LEN(key),
		buf, sizeof(buf), 0, NULL);
	if (len >= 0)
		printf("%.*s\n", (int)len, buf);
	else
		printf("(key not found: %s)\n", key);
}

int
main()
{
	cache = vmemcache_new();
	if (vmemcache_add(cache, "/tmp")) {
		fprintf(stderr, "error: vmemcache_add: %s\n",
				vmemcache_errormsg());
		return 1;
	}

	/* Query a non-existent key. */
	get("meow");

	/* Insert then query. */
	vmemcache_put(cache, STR_AND_LEN("bark"), STR_AND_LEN("Lorem ipsum"));
	get("bark");

	/* Install an on-miss handler. */
	vmemcache_callback_on_miss(cache, on_miss, 0);
	get("meow");

	vmemcache_delete(cache);
	return 0;
}
