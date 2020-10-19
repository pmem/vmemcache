// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

/*
 * vmemcache_repl.h -- API of replacement policy for vmemcache
 */

#ifndef VMEMCACHE_REPL_H
#define VMEMCACHE_REPL_H 1

#include "libvmemcache.h"

#ifdef __cplusplus
extern "C" {
#endif

struct repl_p_head;
struct repl_p_entry;

struct repl_p_ops {
	/* create a new replacement policy list */
	int
		(*repl_p_new)(struct repl_p_head **head);

	/* destroy the replacement policy list */
	void
		(*repl_p_delete)(struct repl_p_head *head);

	/* insert a new element */
	struct repl_p_entry *
		(*repl_p_insert)(struct repl_p_head *head, void *element,
					struct repl_p_entry **ptr_entry);

	/* evict an/the element */
	void *
		(*repl_p_evict)(struct repl_p_head *head,
					struct repl_p_entry **ptr_entry);

	/* use the element */
	void
		(*repl_p_use)(struct repl_p_head *head,
					struct repl_p_entry **ptr_entry);

	/* memory overhead per element */
	size_t dram_per_entry;
};

struct repl_p {
	const struct repl_p_ops *ops;
	struct repl_p_head *head;
};

struct repl_p *repl_p_init(enum vmemcache_repl_p rp);
void repl_p_destroy(struct repl_p *repl_p);

#ifdef __cplusplus
}
#endif

#endif
