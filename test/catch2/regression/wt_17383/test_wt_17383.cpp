/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17383 regression test
 *
 * Bug: prepare_rollback_tombstone was not cleared when the update immediately
 * following it had an invisible (by txn-ID) saved-txnid.  After the loop,
 * upd_select->upd was set to the rollback tombstone even though the prepared
 * transaction was not visible, causing a premature disk write.
 *
 * Fix: added `prepare_rollback_tombstone = NULL;` inside the invisible-txnid
 * skip block in __rec_upd_select (rec_visibility.c ~line 858).
 */

#include <catch2/catch.hpp>
#include <vector>

#include "../../wrappers/mock_session.h"

extern "C" {
#include "wt_internal.h"
#include "../../../../src/reconcile/reconcile_private.h"
}

/* ── helpers reused from misc_tests/test_rec_upd_select.cpp ─────────────── */

static WT_UPDATE *
make_update(WT_SESSION_IMPL *session, uint8_t type, uint64_t txnid, uint16_t flags,
  uint8_t prepare_state, uint64_t saved_txnid = 0, uint64_t prepared_id = 0)
{
    WT_UPDATE *upd = nullptr;
    WT_ITEM val{};
    size_t sz;
    const WT_ITEM *valp = (type == WT_UPDATE_TOMBSTONE || type == WT_UPDATE_RESERVE) ? nullptr : &val;
    if (__wt_upd_alloc(session, valp, type, &upd, &sz) != 0)
        return nullptr;
    upd->txnid = txnid;
    upd->prepare_state = prepare_state;
    upd->flags = flags;
    upd->prepared_id = prepared_id;
    if (txnid == WT_TXN_ABORTED) {
        upd->upd_rollback_ts = WT_TS_NONE; /* u.prepare_rollback.rollback_ts */
        upd->upd_saved_txnid = saved_txnid; /* u.prepare_rollback.saved_txnid */
    }
    return upd;
}

static void
setup_r(WTI_RECONCILE *r, WT_PAGE *page, uint64_t pinned_id = 50,
  wt_timestamp_t pinned_ts = 50)
{
    memset(r, 0, sizeof(*r));
    r->page = page;
    r->rec_start_pinned_id = pinned_id;
    r->rec_start_oldest_id = pinned_id;
    r->rec_start_pinned_stable_ts = pinned_ts;
    r->rec_start_pinned_ts = pinned_ts;
    r->max_txn = WT_TXN_NONE;
    r->max_ts = WT_TS_NONE;
}

/* ── fixture ────────────────────────────────────────────────────────────── */

struct Fixture17383 {
    Fixture17383() : mock(mock_session::build_test_mock_session())
    {
        mock->setup_block_manager_file_operations();
        session = mock->get_wt_session_impl();
        session->id = 0;
        WT_TXN_SHARED *shared;
        REQUIRE(__wt_calloc(session, 1, sizeof(WT_TXN_SHARED), &shared) == 0);
        S2C(session)->txn_global.txn_shared_list = shared;
        REQUIRE(__wt_calloc(session, 1, sizeof(WT_TXN), &session->txn) == 0);
        REQUIRE(__wt_calloc(session, 1, sizeof(WT_PAGE), &page) == 0);
        page->type = WT_PAGE_ROW_LEAF;

        /* snapshot: txnids [0..200] are visible; 201+ are not */
        F_SET(session->txn, WT_TXN_HAS_SNAPSHOT);
        session->txn->snapshot_data.snap_min = 0;
        session->txn->snapshot_data.snap_max = 200;
        session->txn->snapshot_data.snapshot = nullptr;
        session->txn->snapshot_data.snapshot_count = 0;
        session->txn->time_point.id = 0; /* this session's own txnid */
        session->txn->isolation = WT_ISO_SNAPSHOT;
        F_CLR(session->dhandle, WT_DHANDLE_HS);
    }

    ~Fixture17383()
    {
        __wt_free(session, page);
        __wt_free(session, session->txn);
        __wt_free(session, S2C(session)->txn_global.txn_shared_list);
    }

    std::shared_ptr<mock_session> mock;
    WT_SESSION_IMPL *session;
    WT_PAGE *page;
};

/* ── test ───────────────────────────────────────────────────────────────── */

SCENARIO(
  "WT-17383: prepare_rollback_tombstone is cleared when the aborted prepared update "
  "has an invisible saved-txnid",
  "[reconcile][wt-17383]")
{
    GIVEN("connection with WT_CONN_PRESERVE_PREPARED and a two-entry update chain")
    {
        Fixture17383 f;
        F_SET(S2C(f.session), WT_CONN_PRESERVE_PREPARED);

        /*
         * Chain (newest first):
         *   [0] visible tombstone with WT_UPDATE_PREPARE_ROLLBACK (txnid=100)
         *   [1] aborted prepared update whose saved-txnid (300) is invisible (>snap_max=200)
         *
         * Before the fix, the loop would set prepare_rollback_tombstone=[0] when
         * processing [0], then skip [1] without clearing it, so upd_select->upd
         * would be set to the tombstone (wrong).
         *
         * After the fix, processing [1]'s invisible saved-txnid clears
         * prepare_rollback_tombstone to NULL, so upd_select->upd stays NULL.
         */

        /* older: aborted prepared (saved_txnid=300, invisible) */
        WT_UPDATE *aborted = make_update(f.session, WT_UPDATE_STANDARD, WT_TXN_ABORTED,
          0, WT_PREPARE_INPROGRESS, /*saved_txnid=*/300, /*prepared_id=*/1);
        REQUIRE(aborted != nullptr);

        /* newer: visible PREPARE_ROLLBACK tombstone */
        WT_UPDATE *tombstone = make_update(f.session, WT_UPDATE_TOMBSTONE, /*txnid=*/100,
          WT_UPDATE_PREPARE_ROLLBACK, WT_PREPARE_RESOLVED);
        REQUIRE(tombstone != nullptr);
        tombstone->next = aborted;

        /* wrap in WT_INSERT */
        WT_INSERT *ins = nullptr;
        REQUIRE(__wt_calloc(f.session, 1, sizeof(WT_INSERT) + sizeof(WT_INSERT *), &ins) == 0);
        ins->upd = tombstone;

        WTI_RECONCILE r;
        setup_r(&r, f.page);

        WHEN("__wti_rec_upd_select is called")
        {
            WTI_UPDATE_SELECT upd_select{};
            int ret = __wti_rec_upd_select(f.session, &r, ins, nullptr, nullptr, &upd_select);

            THEN("it returns 0 and selects no update (tombstone not leaked)")
            {
                REQUIRE(ret == 0);
                REQUIRE(upd_select.upd == nullptr);
            }
        }

        __wt_free(f.session, ins);
        __wt_free(f.session, tombstone);
        __wt_free(f.session, aborted);
    }
}
