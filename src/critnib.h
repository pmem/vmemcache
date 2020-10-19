// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

#ifndef CRITNIB_H
#define CRITNIB_H

#include "vmemcache.h"
#include "os_thread.h"

/*
 * SLICE may be 1, 2, 4 or 8.  1 or 8 could be further optimized (critbit
 * and critbyte respectively); 4 (critnib) strikes a good balance between
 * speed and memory use.
 */
#define SLICE 4
#define SLNODES (1 << SLICE)

typedef uint32_t byten_t;
typedef unsigned char bitn_t;

struct critnib_node {
	struct critnib_node *child[SLNODES];
	byten_t byte;
	bitn_t bit;
};

struct critnib {
	struct critnib_node *root;
	os_rwlock_t lock;
	size_t leaf_count; /* entries */
	size_t node_count; /* internal nodes only */
	size_t DRAM_usage; /* ... of leaves (nodes are constant-sized) */
	/* operation counts */
	size_t put_count;
	size_t evict_count;
	size_t hit_count;
	size_t miss_count;
};

struct cache_entry;

struct critnib *critnib_new(void);
void critnib_delete(struct critnib *c, delete_entry_t del);
int critnib_set(struct critnib *c, struct cache_entry *e);
void *critnib_get(struct critnib *c, const struct cache_entry *e);
void *critnib_remove(struct critnib *c, const struct cache_entry *e);

#endif
