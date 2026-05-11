/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17321 regression test
 *
 * Bug: when an aborted non-tombstone prepared update was skipped during
 * reconciliation because its prepare_ts was > rec_start_pinned_stable_ts,
 * the skip_aborted_prepared_value flag in WTI_UPDATE_SELECT was NOT set.
 * Without this flag, the on-disk rollback fallback cell would later be
 * dropped during eviction, permanently losing the key.
 *
 * Fix: added `upd_select->skip_aborted_prepared_value = true;` in the
 * PRECISE_CHECKPOINT aborted-prepared-skip path (~line 895 rec_visibility.c).
 */

#include <catch2/catch.hpp>

#include "../../wrappers/mock_session.h"

extern "C" {
#include "wt_internal.h"
#include "../../../../src/reconcile/reconcile_private.h"
}

static WT_UPDATE *
make_aborted_prepared(WT_SESSION_IMPL *session, wt_timestamp_t prepare_ts, uint64_t saved_txnid)
{
    WT_UPDATE *upd = nullptr;
    WT_ITEM val{};
    size_t sz;
    if (__wt_upd_alloc(session, &val, WT_UPDATE_STANDARD, &upd, &sz) != 0)
        return nullptr;
    upd->txnid = WT_TXN_ABORTED;
    upd->prepare_state = WT_PREPARE_INPROGRESS;
    upd->prepared_id = 1; /* != WT_PREPARED_ID_NONE */
    upd->prepare_ts = prepare_ts;
    /* upd_saved_txnid overlaps upd_start_ts in the union */
    upd->upd_saved_txnid = saved_txnid; /* visible (<=snap_max) */
    upd->upd_rollback_ts = WT_TS_NONE;  /* don't take the rollback-ts-stable branch */
    return upd;
}

struct Fixture17321 {
    Fixture17321() : mock(mock_session::build_test_mock_session())
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
    }

    ~Fixture17321()
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
  "WT-17321: skip_aborted_prepared_value is set when aborted prepared update is skipped "
  "by PRECISE_CHECKPOINT",
  "[reconcile][wt-17321]")
{
    GIVEN(
      "connection with PRESERVE_PREPARED + PRECISE_CHECKPOINT and an aborted prepared update")
    {
        Fixture17321 f;
        F_SET(S2C(f.session), WT_CONN_PRESERVE_PREPARED | WT_CONN_PRECISE_CHECKPOINT);

        /*
         * Aborted prepared standard update:
         *   prepare_ts=100 > rec_start_pinned_stable_ts=50  → skipped
         *   saved_txnid=150 <= snap_max=200                 → visible (passes invisible check)
         *   type=STANDARD (not tombstone)
         *   txnid=WT_TXN_ABORTED
         *
         * Before fix: skip_aborted_prepared_value stays false
         * After fix:  skip_aborted_prepared_value is set to true
         */
        WT_UPDATE *upd = make_aborted_prepared(f.session, /*prepare_ts=*/100, /*saved_txnid=*/150);
        REQUIRE(upd != nullptr);

        WT_INSERT *ins = nullptr;
        REQUIRE(__wt_calloc(f.session, 1, sizeof(WT_INSERT) + sizeof(WT_INSERT *), &ins) == 0);
        ins->upd = upd;

        WTI_RECONCILE r{};
        r.page = f.page;
        r.rec_start_pinned_stable_ts = 50; /* prepare_ts(100) > 50 → skip */
        r.rec_start_pinned_id = 50;
        r.rec_start_oldest_id = 50;
        r.rec_start_pinned_ts = 50;
        r.max_txn = WT_TXN_NONE;
        r.max_ts = WT_TS_NONE;

        WHEN("__wti_rec_upd_select is called")
        {
            WTI_UPDATE_SELECT upd_select{};
            int ret = __wti_rec_upd_select(f.session, &r, ins, nullptr, nullptr, &upd_select);

            THEN("skip_aborted_prepared_value is true")
            {
                REQUIRE(ret == 0);
                REQUIRE(upd_select.skip_aborted_prepared_value == true);
            }
        }

        __wt_free(f.session, ins);
        __wt_free(f.session, upd);
    }
}
