// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2019, Intel Corporation */

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
#include "ringbuf.h"

#define LEN_RING_BUF (1 << 12)

struct repl_p_entry {
	TAILQ_ENTRY(repl_p_entry) node;
	void *data;
	struct repl_p_entry **ptr_entry; /* pointer to be zeroed when evicted */
};

struct repl_p_head {
	os_mutex_t lock;
	TAILQ_HEAD(head, repl_p_entry) first;
	struct ringbuf *ringbuf;
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

/* replacement policy operations */
static const struct repl_p_ops repl_p_ops[VMEMCACHE_REPLACEMENT_NUM] = {
{
	.repl_p_new	= repl_p_none_new,
	.repl_p_delete	= repl_p_none_delete,
	.repl_p_insert	= repl_p_none_insert,
	.repl_p_use	= repl_p_none_use,
	.repl_p_evict	= repl_p_none_evict,
	.dram_per_entry	= 0,
},
{
	.repl_p_new	= repl_p_lru_new,
	.repl_p_delete	= repl_p_lru_delete,
	.repl_p_insert	= repl_p_lru_insert,
	.repl_p_use	= repl_p_lru_use,
	.repl_p_evict	= repl_p_lru_evict,
	.dram_per_entry	= sizeof(struct repl_p_entry),
}
};

/*
 * repl_p_init -- allocate and initialize the replacement policy structure
 */
struct repl_p *
repl_p_init(enum vmemcache_repl_p rp)
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
	vmemcache_entry_acquire(element);
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
	return ptr_entry;
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
	h->ringbuf = ringbuf_new(LEN_RING_BUF);
	*head = h;

	return 0;
}

/*
 * dequeue_all -- (internal) dequeue all repl_p entries,
 *                           it MUST be run under a lock
 */
static void
dequeue_all(struct repl_p_head *head)
{
	struct repl_p_entry *e;
	int counter = 0;

	do {
		e = ringbuf_trydequeue_s(head->ringbuf,
						sizeof(struct repl_p_entry));
		if (e == NULL)
			break;

		TAILQ_MOVE_TO_TAIL(&head->first, e, node);

		/* unlock the entry, so that it can be used again */
		util_atomic_store_explicit64(e->ptr_entry, e,
						memory_order_relaxed);
		/*
		 * We are limiting the number of iterations,
		 * so that this loop ends for sure, because other thread
		 * can insert new elements to the ring buffer in the same time.
		 */
	} while (++counter < LEN_RING_BUF);
}

/*
 * repl_p_lru_delete -- (internal) destroy the LRU replacement policy
 */
static void
repl_p_lru_delete(struct repl_p_head *head)
{
	dequeue_all(head);
	ringbuf_delete(head->ringbuf);

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

	/*
	 * 'util_bool_compare_and_swap64' must always succeed here,
	 * because this entry with ptr_entry=NULL has been considered as busy
	 * so it has never been used so far. This is the first time we set
	 * the 'entry->ptr_entry' to 'entry'.
	 */
	int rv = util_bool_compare_and_swap64(entry->ptr_entry, NULL, entry);
	if (rv == 0) {
		FATAL(
			"repl_p_lru_insert(): failed to initialize pointer to the LRU list");
	}

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
	struct repl_p_entry *entry;

	ASSERTne(ptr_entry, NULL);

	entry = *ptr_entry;
	if (entry == NULL)
		return;

	/*
	 * Try to lock the entry by setting 'ptr_entry' to NULL
	 * and enqueue it to the ring buffer,
	 * so that it cannot be used nor evicted.
	 */
	if (!util_bool_compare_and_swap64(ptr_entry, entry, NULL))
		return;

	/*
	 * This the "in the middle of being used" state.
	 * In this state - after bool_compare_and_swap()
	 * and before ringbuf_tryenqueue() - the entry cannot be evicted.
	 */

	while (ringbuf_tryenqueue(head->ringbuf, entry) != 0) {
		util_mutex_lock(&head->lock);
		dequeue_all(head);
		util_mutex_unlock(&head->lock);
	}
}

/*
 * repl_p_lru_evict -- (internal) evict the element
 */
static void *
repl_p_lru_evict(struct repl_p_head *head, struct repl_p_entry **ptr_entry)
{
	struct repl_p_entry *entry;
	void *data = NULL;

	int is_LRU = (ptr_entry == NULL);

	util_mutex_lock(&head->lock);

	if (TAILQ_EMPTY(&head->first)) {
		errno = ESRCH;
		ERR("LRU queue is empty");
		goto exit_unlock;
	}

	if (is_LRU) {
		entry = TAILQ_FIRST(&head->first);
		ptr_entry = entry->ptr_entry;
	} else {
		entry = *ptr_entry;
	}

	/*
	 * Try to lock the entry by setting 'ptr_entry' to NULL,
	 * so that it cannot be used nor evicted in other threads.
	 */
	if (entry != NULL && util_bool_compare_and_swap64(ptr_entry,
								entry, NULL))
		goto evict_found_entry;

	/*
	 * The first try failed. The entry could have been locked and enqueued
	 * in the ring buffer, so let's flush the ring buffer and try again.
	 */
	dequeue_all(head);

	/*
	 * If the entry was assigned as the LRU entry, let's assign it again,
	 * because the LRU entry most likely has been changed in dequeue_all().
	 */
	if (is_LRU) {
		entry = TAILQ_FIRST(&head->first);
		ptr_entry = entry->ptr_entry;
	} else {
		entry = *ptr_entry;
	}

	/* try to lock the entry the second time */
	if (entry != NULL && util_bool_compare_and_swap64(ptr_entry,
								entry, NULL))
		goto evict_found_entry;

	/* the second try failed */

	if (!is_LRU) {
		/* the given entry is busy, give up */
		errno = EAGAIN;
		ERR("entry is busy and cannot be evicted");
		goto exit_unlock;
	}

	if (entry == NULL) {
		/* no entries in the LRU queue, give up */
		errno = ESRCH;
		ERR("LRU queue is empty");
		goto exit_unlock;
	}

	/* try to lock the next entries (repl_p_lru_evict can hardly fail) */
	do {
		entry = TAILQ_NEXT(entry, node);
		if (entry == NULL)
			break;

		ptr_entry = entry->ptr_entry;
	} while (!util_bool_compare_and_swap64(ptr_entry, entry, NULL));

	if (entry != NULL)
		goto evict_found_entry;

	/*
	 * All entries in the LRU queue are locked.
	 * The last chance is to try to dequeue an entry.
	 */
	entry = ringbuf_trydequeue_s(head->ringbuf,
					sizeof(struct repl_p_entry));
	if (entry == NULL) {
		/*
		 * Cannot find any entry to evict.
		 * It means that all entries are heavily used
		 * and they have to be "in the middle of being used" state now
		 * (see repl_p_lru_use()).
		 * There is nothing we can do but fail.
		 */
		errno = ESRCH;
		ERR("no entry eligible for eviction found");
		goto exit_unlock;
	}

evict_found_entry:
	TAILQ_REMOVE(&head->first, entry, node);

	data = entry->data;
	Free(entry);

exit_unlock:
	util_mutex_unlock(&head->lock);
	return data;
}
