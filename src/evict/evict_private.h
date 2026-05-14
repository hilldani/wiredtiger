/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#pragma once

/*
 * Tuning constants: I hesitate to call this tuning, but we want to review some number of pages from
 * each file's in-memory tree for each page we evict.
 */
#define WTI_EVICT_MAX_TREES WT_THOUSAND /* Maximum walk points */
#define WTI_EVICT_WALK_BASE 300         /* Pages tracked across file visits */
#define WTI_EVICT_WALK_INCR 100         /* Pages added each walk */

/* True if there are eviction worker threads beyond the server thread itself. */
#define WT_EVICT_HAS_WORKERS(s) \
    (__wt_atomic_load_uint32_relaxed(&S2C(s)->evict_threads.current_threads) > 1)

/*
 * WTI_EVICT_ENTRY --
 *	Encapsulation of an eviction candidate.
 */
struct __wti_evict_entry {
    WT_BTREE *btree; /* Enclosing btree object */
    WT_REF *ref;     /* Page to flush/evict */
    uint64_t score;  /* Relative eviction priority */
};

#define WTI_EVICT_QUEUE_MAX 3    /* Two ordinary queues plus urgent */
#define WTI_EVICT_URGENT_QUEUE 2 /* Urgent queue index */

/*
 * WTI_DIRTY_INDEX --
 *	Per-btree ring of WT_REF pointers fed from the modify path. The eviction walker
 *	drains entries in FIFO order to supply dirty candidates without walking the tree.
 *	Capacity is a power of two so the consumer can mask-index into the slot array.
 */
#define WTI_DIRTY_INDEX_MIN_CAPACITY 4096u
#define WTI_DIRTY_INDEX_MAX_CAPACITY 262144u
#define WTI_DIRTY_INDEX_SLOTS_PER_GB 500u

/*
 * Adaptive drain scheduling thresholds (see __evict_walk_tree). The drain is attempted on odd
 * passes. After WTI_DRAIN_EMPTY_THRESHOLD consecutive empty drains the per-btree drain is parked
 * (walker-only mode) and re-probed once every WTI_DRAIN_PROBE_INTERVAL passes.
 */
#define WTI_DRAIN_EMPTY_THRESHOLD 8u
#define WTI_DRAIN_PROBE_INTERVAL 32u

struct __wti_dirty_index {
    /*
     * Read-mostly: set at alloc and never modified afterwards.
     */
    WT_REF **slots;    /* Circular buffer of ref pointers */
    uint32_t capacity; /* Slot count (power of two) */
    uint32_t mask;     /* capacity - 1 */

    /*
     * head is hammered by every producer's atomic fetch-add; tail is written only by the single
     * consumer but read by every producer for the overflow check. Without separation, the
     * producer's exclusive acquire of head's cache line invalidates the consumer's writes to tail
     * (and vice versa) on every iteration -- the classic multi-producer-single-consumer ring
     * false-sharing trap.
     *
     * Manual padding (rather than two WT_CACHE_LINE_PAD_BEGIN/END blocks) is necessary because the
     * macro's anonymous-union __padding member would collide if used twice. The padding bytes
     * between head and tail guarantee they live in different cache lines regardless of struct
     * alignment in the heap.
     */
    wt_shared uint64_t head; /* Next slot to be filled (monotonic, fetch-add by producers) */
    char __pad_head_tail[WT_CACHE_LINE_ALIGNMENT - sizeof(uint64_t)];
    wt_shared uint64_t tail; /* Next slot to drain (monotonic, advanced by the consumer) */
};

/*
 * WTI_EVICT_QUEUE --
 *	Encapsulation of an eviction candidate queue.
 */
struct __wti_evict_queue {
    WT_SPINLOCK evict_lock;                /* Eviction LRU queue */
    WTI_EVICT_ENTRY *evict_queue;          /* LRU pages being tracked */
    WTI_EVICT_ENTRY *evict_current;        /* LRU current page to be evicted */
    uint32_t evict_candidates;             /* LRU list pages to evict */
    uint32_t evict_entries;                /* LRU entries in the queue */
    wt_shared volatile uint32_t evict_max; /* LRU maximum eviction slot used */
};

#define WTI_WITH_PASS_LOCK(session, op)                                                  \
    do {                                                                                 \
        WT_WITH_LOCK_WAIT(session, &evict->evict_pass_lock, WT_SESSION_LOCKED_PASS, op); \
    } while (0)

/* DO NOT EDIT: automatically built by prototypes.py: BEGIN */

extern bool __wti_evict_push_candidate(WT_SESSION_IMPL *session, WTI_EVICT_QUEUE *queue,
  WTI_EVICT_ENTRY *evict_entry, WT_REF *ref) WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_app_assist_worker(WT_SESSION_IMPL *session, bool busy, bool readonly,
  bool interruptible) WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_clear_all_walks_and_saved_tree(WT_SESSION_IMPL *session)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_clear_walk_and_saved_tree_if_current_locked(WT_SESSION_IMPL *session)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_lock_handle_list(WT_SESSION_IMPL *session)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_lru_pages(WT_SESSION_IMPL *session, bool is_server)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_lru_walk(WT_SESSION_IMPL *session)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_page(WT_SESSION_IMPL *session, bool is_server)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern int __wti_evict_walk(WT_SESSION_IMPL *session, WTI_EVICT_QUEUE *queue)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
extern void __wti_dirty_index_clear_page(
  WT_SESSION_IMPL *session, WT_BTREE *btree, WT_REF *ref, WT_PAGE *page);
extern void __wti_evict_queue_clear_page(WT_SESSION_IMPL *session, WT_REF *ref);
extern void __wti_evict_queue_clear_page_locked(
  WT_SESSION_IMPL *session, WT_REF *ref, bool exclude_urgent);
extern void __wti_evict_set_saved_walk_tree(WT_SESSION_IMPL *session, WT_DATA_HANDLE *new_dhandle);
static WT_INLINE bool __wti_evict_hs_dirty(WT_SESSION_IMPL *session)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
static WT_INLINE bool __wti_evict_readgen_is_soon_or_wont_need(uint64_t *readgen)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
static WT_INLINE bool __wti_evict_updates_needed(WT_SESSION_IMPL *session, double *pct_fullp)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
static WT_INLINE double __wti_evict_dirty_target(WT_EVICT *evict)
  WT_GCC_FUNC_DECL_ATTRIBUTE((warn_unused_result));
static WT_INLINE void __wti_evict_read_gen_bump(WT_SESSION_IMPL *session, WT_PAGE *page);
static WT_INLINE void __wti_evict_read_gen_new(WT_SESSION_IMPL *session, WT_PAGE *page);

#ifdef HAVE_UNITTEST

#endif

/* DO NOT EDIT: automatically built by prototypes.py: END */
