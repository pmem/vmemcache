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
 * vmemcache_repl.c -- replacement policies for vmemcache
 */

#include <stddef.h>

#include "vmemcache.h"
#include "vmemcache_repl.h"
#include "util.h"
#include "out.h"
#include "sys/queue.h"
#include "sys_util.h"

#define BITS_INDEX 6	/* number of bits to save array's index */
#define BITS_ARRAY 4	/* number of bits to save number of arrays */
#define LEN_ARRAYS (1 << (BITS_INDEX))	/* length of an array */
#define NUM_ARRAYS (1 << (BITS_ARRAY))	/* number of arrays */

#define MASK_INDEX (LEN_ARRAYS -  1)			/* mask of index */
#define MASK_ARRAY ((NUM_ARRAYS -  1) << (BITS_INDEX))	/* mask of array */

#define MAX_INDEX MASK_INDEX	/* maximum value of index */

enum entry_state {
	ENTRY_NOT_USED = 0,
	ENTRY_IN_USE,
	ENTRY_WAS_USED,
	ENTRY_IS_EVICTED,
};

struct repl_p_entry {
	TAILQ_ENTRY(repl_p_entry) node;
	void *data;
	struct repl_p_entry **ptr_entry; /* pointer to be zeroed when evicted */
	unsigned was_used;
	unsigned i_used;
};

struct repl_p_head {
	os_mutex_t lock;
	TAILQ_HEAD(head, repl_p_entry) first;
	unsigned i_used;
	struct repl_p_entry **used;
};

/* forward declarations of replacement policy operations */

static int
repl_p_none_new(struct repl_p_head **head);

static void
repl_p_none_delete(struct repl_p_head *head);

static struct repl_p_entry *
repl_p_none_insert(struct repl_p_head *head, void *element,
			struct repl_p_entry **ptr_entry);

static void
repl_p_none_use(struct repl_p_head *head, struct repl_p_entry **ptr_entry);

static void *
repl_p_none_evict(struct repl_p_head *head, struct repl_p_entry **ptr_entry);

static int
repl_p_lru_new(struct repl_p_head **head);

static void
repl_p_lru_delete(struct repl_p_head *head);

static struct repl_p_entry *
repl_p_lru_insert(struct repl_p_head *head, void *element,
			struct repl_p_entry **ptr_entry);

static void
repl_p_lru_use(struct repl_p_head *head, struct repl_p_entry **ptr_entry);

static void *
repl_p_lru_evict(struct repl_p_head *head, struct repl_p_entry **ptr_entry);

static int
repl_p_lru_buff_new(struct repl_p_head **head);

static void
repl_p_lru_buff_delete(struct repl_p_head *head);

static void
repl_p_lru_buff_use(struct repl_p_head *head, struct repl_p_entry **ptr_entry);

static void *
repl_p_lru_buff_evict(struct repl_p_head *head,
			struct repl_p_entry **ptr_entry);

/* replacement policy operations */
static const struct repl_p_ops repl_p_ops[VMEMCACHE_REPLACEMENT_NUM] = {
{
	.repl_p_new	= repl_p_none_new,
	.repl_p_delete	= repl_p_none_delete,
	.repl_p_insert	= repl_p_none_insert,
	.repl_p_use	= repl_p_none_use,
	.repl_p_evict	= repl_p_none_evict,
},
{
	.repl_p_new	= repl_p_lru_new,
	.repl_p_delete	= repl_p_lru_delete,
	.repl_p_insert	= repl_p_lru_insert,
	.repl_p_use	= repl_p_lru_use,
	.repl_p_evict	= repl_p_lru_evict,
},
{
	.repl_p_new	= repl_p_lru_buff_new,
	.repl_p_delete	= repl_p_lru_buff_delete,
	.repl_p_insert	= repl_p_lru_insert,
	.repl_p_use	= repl_p_lru_buff_use,
	.repl_p_evict	= repl_p_lru_buff_evict,
}
};

/*
 * repl_p_init -- allocate and initialize the replacement policy structure
 */
struct repl_p *
repl_p_init(enum vmemcache_replacement_policy rp)
{
	struct repl_p *repl_p = Malloc(sizeof(struct repl_p));
	if (repl_p == NULL)
		return NULL;

	repl_p->ops = &repl_p_ops[rp];

	if (repl_p->ops->repl_p_new(&repl_p->head)) {
		Free(repl_p);
		return NULL;
	}

	return repl_p;
}

/*
 * repl_p_destroy -- destroy the replacement policy structure
 */
void
repl_p_destroy(struct repl_p *repl_p)
{
	ASSERTne(repl_p, NULL);

	repl_p->ops->repl_p_delete(repl_p->head);
	Free(repl_p);
}

/*
 * repl_p_none_new -- (internal) create a new "none" replacement policy
 */
static int
repl_p_none_new(struct repl_p_head **head)
{
	*head = NULL;
	return 0;
}

/*
 * repl_p_none_delete -- (internal) destroy the "none" replacement policy
 */
static void
repl_p_none_delete(struct repl_p_head *head)
{
}

/*
 * repl_p_none_insert -- (internal) insert a new element
 */
static struct repl_p_entry *
repl_p_none_insert(struct repl_p_head *head, void *element,
			struct repl_p_entry **ptr_entry)
{
	return NULL;
}

/*
 * repl_p_none_use -- (internal) use the element
 */
static void
repl_p_none_use(struct repl_p_head *head, struct repl_p_entry **ptr_entry)
{
}

/*
 * repl_p_none_evict -- (internal) evict the element
 */
static void *
repl_p_none_evict(struct repl_p_head *head, struct repl_p_entry **ptr_entry)
{
	return NULL;
}


/*
 * repl_p_lru_new -- (internal) create a new LRU replacement policy
 */
static int
repl_p_lru_new(struct repl_p_head **head)
{
	struct repl_p_head *h = Zalloc(sizeof(struct repl_p_head));
	if (h == NULL)
		return -1;

	util_mutex_init(&h->lock);
	TAILQ_INIT(&h->first);
	*head = h;

	return 0;
}

/*
 * repl_p_lru_delete -- (internal) destroy the LRU replacement policy
 */
static void
repl_p_lru_delete(struct repl_p_head *head)
{
	while (!TAILQ_EMPTY(&head->first)) {
		struct repl_p_entry *entry = TAILQ_FIRST(&head->first);
		TAILQ_REMOVE(&head->first, entry, node);
		Free(entry);
	}

	util_mutex_destroy(&head->lock);
	Free(head);
}

/*
 * repl_p_lru_insert -- (internal) insert a new element
 */
static struct repl_p_entry *
repl_p_lru_insert(struct repl_p_head *head, void *element,
			struct repl_p_entry **ptr_entry)
{
	struct repl_p_entry *entry = Zalloc(sizeof(struct repl_p_entry));
	if (entry == NULL)
		return NULL;

	entry->data = element;

	ASSERTne(ptr_entry, NULL);
	entry->ptr_entry = ptr_entry;
	*(entry->ptr_entry) = entry;

	util_mutex_lock(&head->lock);

	vmemcache_entry_acquire(element);
	TAILQ_INSERT_TAIL(&head->first, entry, node);

	util_mutex_unlock(&head->lock);

	return entry;
}

/*
 * repl_p_lru_use -- (internal) use the element
 */
static void
repl_p_lru_use(struct repl_p_head *head, struct repl_p_entry **ptr_entry)
{
	ASSERTne(ptr_entry, NULL);

	util_mutex_lock(&head->lock);

	if (*ptr_entry != NULL)
		TAILQ_MOVE_TO_TAIL(&head->first, *ptr_entry, node);

	util_mutex_unlock(&head->lock);
}

/*
 * repl_p_lru_evict -- (internal) evict the element
 */
static void *
repl_p_lru_evict(struct repl_p_head *head, struct repl_p_entry **ptr_entry)
{
	struct repl_p_entry *entry;
	void *data;

	util_mutex_lock(&head->lock);

	if (ptr_entry != NULL)
		entry = *ptr_entry;
	else
		entry = TAILQ_FIRST(&head->first);

	if (entry == NULL) {
		util_mutex_unlock(&head->lock);
		return NULL;
	}

	TAILQ_REMOVE(&head->first, entry, node);

	ASSERTne(entry->ptr_entry, NULL);
	*(entry->ptr_entry) = NULL;

	util_mutex_unlock(&head->lock);

	data = entry->data;

	Free(entry);

	return data;
}

/*
 * repl_p_lru_buff_new -- (internal) create a new LRU replacement policy
 */
static int
repl_p_lru_buff_new(struct repl_p_head **head)
{
	struct repl_p_head *h = Zalloc(sizeof(struct repl_p_head));
	if (h == NULL)
		return -1;

	util_mutex_init(&h->lock);
	TAILQ_INIT(&h->first);
	h->i_used = 0;
	h->used = Zalloc(NUM_ARRAYS * LEN_ARRAYS * sizeof(void *));
	if (h->used == NULL)
		return -1;
	*head = h;

	return 0;
}

/*
 * repl_p_lru_buff_delete -- (internal) destroy the LRU replacement policy
 */
static void
repl_p_lru_buff_delete(struct repl_p_head *head)
{
	while (!TAILQ_EMPTY(&head->first)) {
		struct repl_p_entry *entry = TAILQ_FIRST(&head->first);
		TAILQ_REMOVE(&head->first, entry, node);
		Free(entry);
	}

	util_mutex_destroy(&head->lock);
	Free(head->used);
	Free(head);
}

/*
 * clear_used_array -- (internal) clear array of used elements
 */
static void
clear_used_array(struct repl_p_head *head, unsigned i_array)
{
	util_mutex_lock(&head->lock);

	struct repl_p_entry **array = head->used + i_array;

	for (unsigned i = 0; i < MAX_INDEX; i++) {
		struct repl_p_entry *entry = array[i];
		if (entry != NULL) {
			TAILQ_MOVE_TO_TAIL(&head->first, entry, node);
			entry->was_used = ENTRY_NOT_USED;
			array[i] = NULL;
		}
	}

	util_mutex_unlock(&head->lock);
}

/*
 * get_used_index -- (internal) get index in array of used elements
 */
static unsigned
get_used_index(struct repl_p_head *head)
{
	unsigned i_used = __sync_fetch_and_add(&head->i_used, 1);

	unsigned i = i_used & MASK_INDEX;
	unsigned a = i_used & MASK_ARRAY;

	if (i == MAX_INDEX) {
		clear_used_array(head, a);
		i = 0;
	}

	return a | i;
}

/*
 * repl_p_lru_buff_use -- (internal) use the element
 */
static void
repl_p_lru_buff_use(struct repl_p_head *head, struct repl_p_entry **ptr_entry)
{
	ASSERTne(ptr_entry, NULL);

	struct repl_p_entry *entry = *ptr_entry;
	if (entry == NULL)
		return;

	if (__sync_bool_compare_and_swap(&entry->was_used,
					ENTRY_NOT_USED, ENTRY_IN_USE)) {
		entry->i_used = get_used_index(head);
		head->used[entry->i_used] = entry;
		__sync_bool_compare_and_swap(&entry->was_used,
					ENTRY_IN_USE, ENTRY_WAS_USED);
	}
}

/*
 * repl_p_lru_buff_evict -- (internal) evict the element
 */
static void *
repl_p_lru_buff_evict(struct repl_p_head *head, struct repl_p_entry **ptr_entry)
{
	struct repl_p_entry *entry;
	void *data;

	util_mutex_lock(&head->lock);

	if (ptr_entry != NULL)
		entry = *ptr_entry;
	else
		entry = TAILQ_FIRST(&head->first);

	if (entry == NULL) {
		util_mutex_unlock(&head->lock);
		return NULL;
	}

	TAILQ_REMOVE(&head->first, entry, node);

	ASSERTne(entry->ptr_entry, NULL);
	*(entry->ptr_entry) = NULL;

	if (__sync_bool_compare_and_swap(&entry->was_used,
					ENTRY_WAS_USED, ENTRY_IS_EVICTED)) {
		head->used[entry->i_used] = NULL;
		__sync_bool_compare_and_swap(&entry->was_used,
					ENTRY_IS_EVICTED, ENTRY_NOT_USED);
	}

	util_mutex_unlock(&head->lock);

	data = entry->data;

	Free(entry);

	return data;
}
