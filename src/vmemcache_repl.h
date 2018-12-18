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
					struct repl_p_entry *entry);

	/* use the element */
	int
		(*repl_p_use)(struct repl_p_head *head,
					struct repl_p_entry *entry);
};

struct repl_p {
	const struct repl_p_ops *ops;
	struct repl_p_head *head;
};

int repl_p_init(struct repl_p *repl_p, enum vmemcache_replacement_policy rp);
void repl_p_destroy(struct repl_p *repl_p);

#ifdef __cplusplus
}
#endif

#endif
