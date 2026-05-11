/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17387 (tombstone path) regression test
 *
 * Bug: tombstone_globally_visible in __rec_fill_tw_from_upd_select was computed
 * without guarding on !write_prepare.  When write_prepare=true and the tombstone
 * happened to be globally visible (race: it was resolved between the selection
 * loop and the fill step), tombstone_globally_visible was incorrectly true,
 * causing upd_select->upd to remain as the tombstone instead of being advanced
 * to the underlying value.
 *
 * Fix: `tombstone_globally_visible = !write_prepare && __wt_txn_upd_visible_all(…)`
 * (~line 1350 of rec_visibility.c).
 */

#include <catch2/catch.hpp>

#include "../../wrappers/mock_session.h"

extern "C" {
#include "wt_internal.h"
#include "../../../../src/reconcile/reconcile_private.h"
#include "../../../../src/reconcile/reconcile.h"
}

struct Fixture17387Tombstone {
    Fixture17387Tombstone() : mock(mock_session::build_test_mock_session())
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

        /* oldest_id=100: txnid=5 < 100 → globally visible by ID */
        S2C(session)->txn_global.oldest_id = 100;
    }

    ~Fixture17387Tombstone()
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
  "WT-17387: tombstone_globally_visible is false when write_prepare=true",
  "[reconcile][wt-17387]")
{
    GIVEN("a globally-visible tombstone followed by an underlying value, write_prepare=true")
    {
        Fixture17387Tombstone f;

        /*
         * Underlying value: committed, txnid=5 (globally visible), no timestamp.
         */
        WT_UPDATE *value_upd = nullptr;
        WT_ITEM val{};
        size_t sz;
        REQUIRE(__wt_upd_alloc(f.session, &val, WT_UPDATE_STANDARD, &value_upd, &sz) == 0);
        value_upd->txnid = 5;
        value_upd->prepare_state = WT_PREPARE_RESOLVED;
        value_upd->upd_start_ts = WT_TS_NONE;
        value_upd->upd_durable_ts = WT_TS_NONE;

        /*
         * Tombstone: globally visible (txnid=5 < oldest_id=100, durable_ts=WT_TS_NONE).
         * prepare_state=RESOLVED so __wt_txn_upd_visible_all can return true.
         * write_prepare=true simulates the race where the tombstone was INPROGRESS
         * during the selection loop but has since been resolved.
         */
        WT_UPDATE *tombstone = nullptr;
        REQUIRE(__wt_upd_alloc(f.session, nullptr, WT_UPDATE_TOMBSTONE, &tombstone, &sz) == 0);
        tombstone->txnid = 5;
        tombstone->prepare_state = WT_PREPARE_RESOLVED;
        tombstone->upd_start_ts = WT_TS_NONE;
        tombstone->upd_durable_ts = WT_TS_NONE;
        tombstone->next = value_upd;

        WTI_UPDATE_SELECT upd_select{};
        upd_select.upd = tombstone;

        WTI_RECONCILE r{};
        r.page = f.page;

        WHEN("__ut_rec_fill_tw_from_upd_select is called with write_prepare=true")
        {
            int ret =
              __ut_rec_fill_tw_from_upd_select(f.session, f.page, nullptr, &upd_select,
                /*write_prepare=*/true, &r);

            THEN("it returns 0 and upd_select->upd is advanced past the tombstone")
            {
                REQUIRE(ret == 0);
                /*
                 * With the fix: tombstone_globally_visible = !write_prepare && ... = false
                 * → else branch → upd_select->upd = tombstone->next = value_upd.
                 *
                 * Without fix: tombstone_globally_visible = true (globally visible)
                 * → if branch → upd_select->upd stays as tombstone.
                 */
                REQUIRE(upd_select.upd == value_upd);
            }
        }

        __wt_free(f.session, tombstone);
        __wt_free(f.session, value_upd);
    }
}
