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

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "os_thread.h"
#include "util.h"
#include "out.h"
#include "critnib.h"

/*
 * WARNING: this implementation fails badly if you try to store two keys
 * where one is a prefix of another.  Pass a struct { int len; char[] key; }
 * or such if such keys are possible.
 */

/*
 * SLICE may be 1, 2, 4 or 8.  1 or 8 could be further optimized (critbit
 * and critbyte respectively); 4 (critnib) strikes a good balance between
 * speed and memory use.
 */
#define SLICE 4
#define NIB ((1 << SLICE) - 1)
#define SLNODES (1 << SLICE)

#define KEYLEN(leaf) (leaf->key.ksize + sizeof(size_t))

typedef uint32_t byten_t;
typedef unsigned char bitn_t;

struct critnib_node {
	struct critnib_node *child[SLNODES];
	byten_t byte;
	bitn_t bit;
};

typedef struct cache_entry critnib_leaf;

/*
 * is_leaf -- (internal) check tagged pointer for leafness
 */
static inline bool
is_leaf(struct critnib_node *n)
{
	return (uint64_t)n & 1;
}

/*
 * to_leaf -- (internal) untag a leaf pointer
 */
static inline critnib_leaf *
to_leaf(struct critnib_node *n)
{
	return (void *)((uint64_t)n & ~1ULL);
}

/*
 * slice_index -- (internal) get index of radix child at a given shift
 */
static inline int
slice_index(char b, bitn_t bit)
{
	return (b >> bit) & NIB;
}

/*
 * critnib_new -- allocate a new hashmap
 */
struct critnib *
critnib_new(void)
{
	struct critnib *c = Malloc(sizeof(struct critnib));
	if (!c)
		return NULL;
	c->root = NULL;
	return c;
}

/*
 * delete_node -- (internal) recursively free a subtree
 */
static void
delete_node(struct critnib_node *n, delete_entry_t del)
{
	if (!n)
		return;
	if (is_leaf(n)) {
		if (del)
			del(to_leaf(n));
		return;
	}
	for (int i = 0; i < SLNODES; i++)
		delete_node(n->child[i], del);
	Free(n);
}

/*
 * critnib_delete -- free a hashmap
 */
void
critnib_delete(struct critnib *c, delete_entry_t del)
{
	delete_node(c->root, del);
	Free(c);
}

/*
 * alloc_node -- (internal) alloc a node
 */
static struct critnib_node *
alloc_node(struct critnib *c)
{
	struct critnib_node *n = Zalloc(sizeof(struct critnib_node));
	if (!n)
		return NULL;
	for (int i = 0; i < SLNODES; i++)
		n->child[i] = NULL;
	return n;
}

/*
 * any_leaf -- (internal) find any leaf in a subtree
 *
 * We know they're all identical up to the divergence point between a prefix
 * shared by all of them vs the new key we're inserting.
 */
static struct critnib_node *
any_leaf(struct critnib_node *n)
{
	for (int i = 0; i < SLNODES; i++) {
		struct critnib_node *m;
		if ((m = n->child[i]))
			return is_leaf(m) ? m : any_leaf(m);
	}
	return NULL;
}

/*
 * critnib_set -- insert a new entry
 */
int
critnib_set(struct critnib *c, struct cache_entry *e)
{
	const char *key = (void *)&e->key;
	byten_t key_len = (byten_t)KEYLEN(e);
	critnib_leaf *k = (void *)((uint64_t)e | 1);

	struct critnib_node *n = c->root;
	if (!n) {
		c->root = (void *)k;
		return 0;
	}

	/*
	 * Need to descend the tree twice: first to find a leaf that
	 * represents a subtree whose all keys share a prefix at least as
	 * long as the one common to the new key and that subtree.
	 */
	while (!is_leaf(n) && n->byte < key_len) {
		struct critnib_node *nn =
			n->child[slice_index(key[n->byte], n->bit)];
		if (nn)
			n = nn;
		else {
			n = any_leaf(n);
			break;
		}
	}

	ASSERT(n);
	if (!is_leaf(n))
		n = any_leaf(n);

	ASSERT(n);
	ASSERT(is_leaf(n));
	critnib_leaf *nk = to_leaf(n);
	const char *nkey = (void *)&nk->key;

	/* Find the divergence point, accurate to a byte. */
	byten_t common_len = ((byten_t)KEYLEN(nk) < key_len)
			    ? (byten_t)KEYLEN(nk) : key_len;
	byten_t diff;
	for (diff = 0; diff < common_len; diff++) {
		if (nkey[diff] != key[diff])
			break;
	}

	if (diff >= common_len) {
		/*
		 * Either an update or a conflict between keys being a
		 * prefix of each other.
		 */
		return EEXIST;
	}

	/* Calculate the divergence point within the single byte. */
	char at = nkey[diff] ^ key[diff];
	bitn_t sh = util_mssb_index((uint32_t)at) & (bitn_t)~(SLICE - 1);

	/* Descend into the tree again. */
	n = c->root;
	struct critnib_node **parent = &c->root;
	while (n && !is_leaf(n) &&
			(n->byte < diff || (n->byte == diff && n->bit >= sh))) {
		parent = &n->child[slice_index(key[n->byte], n->bit)];
		n = *parent;
	}

	/*
	 * If the divergence point is at same nib as an existing node, and
	 * the subtree there is empty, just place our leaf there and we're
	 * done.  Obviously this can't happen if SLICE == 1.
	 */
	if (!n) {
		*parent = (void *)k;
		return 0;
	}

	/* If not, we need to insert a new node in the middle of an edge. */
	if (!(n = alloc_node(c)))
		return ENOMEM;

	n->child[slice_index(nkey[diff], sh)] = *parent;
	n->child[slice_index(key[diff], sh)] = (void *)k;
	n->byte = diff;
	n->bit = sh;
	*parent = n;
	return 0;
}

/*
 * critnib_get -- query a key
 */
void *
critnib_get(struct critnib *c, const struct cache_entry *e)
{
	const char *key = (void *)&e->key;
	byten_t key_len = (byten_t)KEYLEN(e);

	struct critnib_node *n = c->root;
	while (n && !is_leaf(n)) {
		if (n->byte >= key_len)
			return NULL;
		n = n->child[slice_index(key[n->byte], n->bit)];
	}

	if (!n)
		return NULL;

	critnib_leaf *k = to_leaf(n);

	/*
	 * We checked only nibs at divergence points, have to re-check the
	 * whole key.
	 */
	return (key_len != KEYLEN(k) || memcmp(key, (void *)&k->key,
		key_len)) ? NULL : k;
}

/*
 * critnib_remove -- query and delete a key
 *
 * Neither the key nor its value are freed, just our private nodes.
 */
void *
critnib_remove(struct critnib *c, const struct cache_entry *e)
{
	const char *key = (void *)&e->key;
	byten_t key_len = (byten_t)KEYLEN(e);

	struct critnib_node **pp = NULL;
	struct critnib_node *n = c->root;
	struct critnib_node **parent = &c->root;

	/* First, do a get. */
	while (n && !is_leaf(n)) {
		if (n->byte >= key_len)
			return NULL;
		pp = parent;
		parent = &n->child[slice_index(key[n->byte], n->bit)];
		n = *parent;
	}

	if (!n)
		return NULL;

	critnib_leaf *k = to_leaf(n);
	if (key_len != KEYLEN(k) || memcmp(key, (void *)&k->key, key_len))
		return NULL;

	/* Remove the entry (leaf). */
	*parent = NULL;

	if (!pp) /* was root */
		return k;

	/* Check if after deletion the node has just a single child left. */
	n = *pp;
	struct critnib_node *only_child = NULL;
	for (int i = 0; i < SLNODES; i++) {
		if (n->child[i]) {
			if (only_child) /* Nope. */
				return k;
			only_child = n->child[i];
		}
	}

	/* Yes -- shorten the tree's edge. */
	ASSERT(only_child);
	*pp = only_child;
	Free(n);
	return k;
}
