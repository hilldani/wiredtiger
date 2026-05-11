/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17322 regression test
 *
 * Bug: __rec_validate_upd_chain fired a false WT_ASSERT_ALWAYS when an
 * obsolete check had removed a globally-visible tombstone that previously sat
 * between the update chain and the on-disk value.  The re-insert has a lower
 * durable timestamp than the old on-disk durable_start_ts, which is legitimate
 * once the tombstone is gone, but the assertion lacked the
 * __wt_txn_upd_visible_all escape hatch.
 *
 * Fix: added `|| __wt_txn_upd_visible_all(session, prev_upd)` to both
 * WT_ASSERT_ALWAYS conditions in __rec_validate_upd_chain
 * (~lines 661 and 674 of rec_visibility.c).
 */

#include <catch2/catch.hpp>

#include "../../wrappers/mock_session.h"

extern "C" {
#include "wt_internal.h"
#include "../../../../src/reconcile/reconcile_private.h"
#include "../../../../src/reconcile/reconcile.h"
}

struct Fixture17322 {
    Fixture17322() : mock(mock_session::build_test_mock_session())
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

        /*
         * Make txnid=5 globally visible by ID: oldest_id=100 > 5.
         * upd_durable_ts=0=WT_TS_NONE is always globally visible by timestamp.
         */
        S2C(session)->txn_global.oldest_id = 100;
    }

    ~Fixture17322()
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
  "WT-17322: __rec_validate_upd_chain passes when globally-visible re-insert has lower "
  "durable_ts than on-disk value",
  "[reconcile][wt-17322]")
{
    GIVEN(
      "a globally-visible update whose durable_ts is below the on-disk durable_start_ts")
    {
        Fixture17322 f;

        /*
         * Simulate the scenario where:
         *   - A tombstone between the chain and on-disk was removed by the obsolete check.
         *   - The re-insert (our update) has prepare_ts=3 (upd_start_ts) but durable_ts=0.
         *   - upd_start_ts(3) != upd_durable_ts(0)  → condition 1 of the assert fails.
         *   - upd_durable_ts(0) < vpack->tw.durable_start_ts(10) → condition 2 fails.
         *   - __wt_txn_upd_visible_all: txnid=5 < oldest_id=100 AND durable_ts=WT_TS_NONE → TRUE.
         *
         * Before fix: WT_ASSERT_ALWAYS fires (crash).
         * After fix: __wt_txn_upd_visible_all escape hatch → passes.
         */
        WT_UPDATE *upd = nullptr;
        WT_ITEM val{};
        size_t sz;
        REQUIRE(__wt_upd_alloc(f.session, &val, WT_UPDATE_STANDARD, &upd, &sz) == 0);
        upd->txnid = 5;
        upd->prepare_state = WT_PREPARE_RESOLVED;
        upd->upd_start_ts = 3;   /* != upd_durable_ts → assert condition 1 fails */
        upd->upd_durable_ts = 0; /* WT_TS_NONE → globally visible by timestamp */

        /* select_tw: start_ts=0, stop_ts=0 → passes the stop_ts < start_ts EBUSY check */
        WT_TIME_WINDOW select_tw{};
        WT_TIME_WINDOW_INIT(&select_tw);
        select_tw.start_ts = 0;
        select_tw.stop_ts = 0;
        select_tw.stop_txn = WT_TXN_MAX;

        /* On-disk vpack: higher durable_start_ts triggers the assertion */
        WT_CELL_UNPACK_KV vpack{};
        vpack.type = WT_CELL_VALUE; /* != WT_CELL_DEL → WT_REC_HAS_ON_DISK = true */
        WT_TIME_WINDOW_INIT(&vpack.tw);
        vpack.tw.durable_start_ts = 10; /* > upd_durable_ts(0) → assert fires w/o fix */
        vpack.tw.start_ts = 3;          /* <= upd_start_ts for secondary WT_ASSERT */
        vpack.tw.start_txn = 5;

        WTI_RECONCILE r{};
        r.page = f.page;
        /* WT_REC_HS | WT_REC_CHECKPOINT_RUNNING are required to enter the validation */
        r.flags = WT_REC_HS | WT_REC_CHECKPOINT_RUNNING;

        WHEN("__ut_rec_validate_upd_chain is called")
        {
            int ret = __ut_rec_validate_upd_chain(f.session, &r, upd, &select_tw, &vpack);

            THEN("it returns 0 (no false assertion, globally-visible escape hatch works)")
            {
                REQUIRE(ret == 0);
            }
        }

        __wt_free(f.session, upd);
    }
}
