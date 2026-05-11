/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17387 (seen_committed accumulation) regression test
 *
 * Bug: in __rec_append_orig_value, the `seen_committed` flag was assigned
 * (`= expr`) on each loop iteration instead of accumulated.  When walking a
 * chain [committed → prepared], the prepared update's iteration would reset
 * seen_committed to false, hiding the earlier committed entry.  With
 * WT_CONN_PRESERVE_PREPARED, this caused a spurious tombstone to be appended
 * to the update chain.
 *
 * Fix: changed `seen_committed = (prepare_state != ...)` to
 * `if (prepare_state != ...) seen_committed = true;`
 * (~line 155-156 of rec_visibility.c).
 */

#include <catch2/catch.hpp>

#include "../../wrappers/mock_session.h"

extern "C" {
#include "wt_internal.h"
#include "../../../../src/reconcile/reconcile_private.h"
}

struct Fixture17387SeenCommitted {
    Fixture17387SeenCommitted() : mock(mock_session::build_test_mock_session())
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

        F_SET(session->txn, WT_TXN_HAS_SNAPSHOT);
        session->txn->snapshot_data.snap_min = 0;
        session->txn->snapshot_data.snap_max = 200;
        session->txn->snapshot_data.snapshot = nullptr;
        session->txn->snapshot_data.snapshot_count = 0;
        session->txn->time_point.id = 0;
        session->txn->isolation = WT_ISO_SNAPSHOT;
        F_CLR(session->dhandle, WT_DHANDLE_HS);

        F_SET(S2C(session), WT_CONN_PRESERVE_PREPARED);

        /*
         * oldest_id=100: txnid=5 globally visible (for tombstone_globally_visible).
         * Committed update uses txnid=150 > 100 (NOT globally visible → loop doesn't
         * short-circuit at the __wt_txn_upd_visible_all check inside the loop).
         */
        S2C(session)->txn_global.oldest_id = 100;
    }

    ~Fixture17387SeenCommitted()
    {
        __wt_free(session, page);
        __wt_free(session, session->txn);
        __wt_free(session, S2C(session)->txn_global.txn_shared_list);
    }

    std::shared_ptr<mock_session> mock;
    WT_SESSION_IMPL *session;
    WT_PAGE *page;
};

SCENARIO(
  "WT-17387: seen_committed is not reset when a prepared update follows a committed one",
  "[reconcile][wt-17387-seen-committed]")
{
    GIVEN(
      "a chain [committed (RESOLVED, txnid=150)] -> [prepared (INPROGRESS)] and "
      "an on-disk stop window with a globally-visible tombstone")
    {
        Fixture17387SeenCommitted f;

        /*
         * Prepared update (older, tail of chain).
         * PREPARE_INPROGRESS → seen_committed must not be cleared by this entry.
         */
        WT_UPDATE *prepared = nullptr;
        WT_ITEM empty{};
        size_t sz;
        REQUIRE(__wt_upd_alloc(f.session, &empty, WT_UPDATE_STANDARD, &prepared, &sz) == 0);
        prepared->txnid = 200;
        prepared->prepare_state = WT_PREPARE_INPROGRESS;
        prepared->prepared_id = 1;
        prepared->upd_start_ts = 10;
        prepared->upd_durable_ts = WT_TS_NONE;
        prepared->next = nullptr;

        /*
         * Committed update (newer, head of chain).
         * PREPARE_RESOLVED → sets seen_committed=true on first iteration.
         * txnid=150 > oldest_id=100 → NOT globally visible → loop stays in.
         */
        WT_UPDATE *committed = nullptr;
        WT_ITEM val{};
        REQUIRE(__wt_upd_alloc(f.session, &val, WT_UPDATE_STANDARD, &committed, &sz) == 0);
        committed->txnid = 150;
        committed->prepare_state = WT_PREPARE_RESOLVED;
        committed->upd_start_ts = 5;
        committed->upd_durable_ts = WT_TS_NONE;
        committed->next = prepared;

        /*
         * On-disk cell: has a stop window whose tombstone is globally visible
         * (stop_txn=5 < oldest_id=100, durable_stop_ts=3 non-zero).
         * durable_stop_ts != WT_TS_NONE triggers the seen_committed check path.
         *
         * For tombstone_globally_visible=true we need __wt_txn_visible_all to
         * return true for (stop_txn=5, durable_stop_ts=3).  oldest_id=100>5 ✓;
         * timestamp: has_pinned_timestamp=true with pinned_timestamp=5 >= 3 ✓.
         */
        S2C(f.session)->txn_global.has_pinned_timestamp = true;
        S2C(f.session)->txn_global.pinned_timestamp = 5;

        WT_CELL_UNPACK_KV unpack{};
        unpack.type = WT_CELL_VALUE;
        WT_TIME_WINDOW_INIT(&unpack.tw);
        unpack.tw.stop_txn = 5;           /* != WT_TXN_MAX → HAS_STOP=true */
        unpack.tw.stop_ts = 2;
        unpack.tw.durable_stop_ts = 3;    /* != WT_TS_NONE → enters the seen_committed path */
        unpack.tw.start_txn = 5;
        unpack.tw.start_ts = 1;
        unpack.tw.durable_start_ts = 1;

        WHEN("__ut_rec_append_orig_value is called with write_prepared=true")
        {
            int ret =
              __ut_rec_append_orig_value(f.session, f.page, committed, &unpack,
                /*write_prepared=*/true);

            THEN("it returns 0 and no spurious tombstone is appended")
            {
                REQUIRE(ret == 0);
                /*
                 * With fix: seen_committed stays true after the prepared iteration →
                 * returns early at `if (seen_committed || !write_prepared) return (0)` →
                 * prepared->next remains NULL.
                 *
                 * Without fix: seen_committed is reset to false by prepared iteration →
                 * falls through to __wt_upd_alloc_tombstone → prepared->next != NULL.
                 */
                REQUIRE(prepared->next == nullptr);
            }
        }

        __wt_free(f.session, committed);
        __wt_free(f.session, prepared);
    }
}
