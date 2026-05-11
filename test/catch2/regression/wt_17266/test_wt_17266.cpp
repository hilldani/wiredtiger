/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17266 regression test
 *
 * Bug: in __clayered_reposition_truncate_iterate, when the inner cursor
 * search returned WT_NOTFOUND, the old code used WT_RET_NOTFOUND_OK which
 * caused iteration to continue to the next constituent rather than stopping.
 * This caused truncated ranges to be iterated past the truncation boundary.
 *
 * Fix: replaced WT_RET_NOTFOUND_OK with a proper break when WT_NOTFOUND is
 * returned from the inner search (~line 776 of cur_layered.c).
 */

#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>

#include <catch2/catch.hpp>

#include "wt_internal.h"
#include "wrappers/connection_wrapper.h"
#include "../truncate/truncate_list_helpers.hpp"

using namespace truncate_list_helpers;

namespace {

const char *
prepare_home(const char *home)
{
    std::filesystem::remove_all(home);
    return home;
}

class follower_conn_17266 {
public:
    follower_conn_17266()
    {
        constexpr auto uri = "layered:test_17266";
        constexpr auto cfg = "key_format=S,value_format=S,block_manager=disagg,type=layered";
        auto &s = _session->iface;
        REQUIRE(s.create(&s, uri, cfg) == 0);
        REQUIRE(s.begin_transaction(&s, nullptr) == 0);

        /* Insert a few keys */
        WT_CURSOR *cur = nullptr;
        REQUIRE(s.open_cursor(&s, uri, nullptr, nullptr, &cur) == 0);
        for (const char *k : {"a", "b", "c", "d", "e"}) {
            cur->set_key(cur, k);
            cur->set_value(cur, "v");
            REQUIRE(cur->insert(cur) == 0);
        }
        REQUIRE(cur->close(cur) == 0);
        REQUIRE(s.commit_transaction(&s, nullptr) == 0);

        REQUIRE(s.open_cursor(&s, uri, nullptr, nullptr, &_cursor) == 0);
        REQUIRE(s.begin_transaction(&s, nullptr) == 0);
    }

    ~follower_conn_17266()
    {
        auto *txn = _session->txn;
        for (size_t i = 0; i < txn->mod_count; ++i)
            __wt_txn_op_free(_session, &txn->mod[i]);
        txn->mod_count = 0;
    }

    WT_CURSOR *cursor() { return _cursor; }
    WT_SESSION_IMPL &session_impl() { return *_session; }
    WT_LAYERED_TABLE &layered_table()
    {
        auto *cl = reinterpret_cast<WT_CURSOR_LAYERED *>(_cursor);
        return *reinterpret_cast<WT_LAYERED_TABLE *>(cl->dhandle);
    }

private:
    static constexpr auto home = "WT_TEST.wt_17266";
    static constexpr auto conn_cfg =
      "create,"
      "extensions=[./ext/page_log/palite/libwiredtiger_palite.so],"
      "disaggregated=(role=follower,page_log=palite)";

    scoped_fast_truncate_enable _enable;
    connection_wrapper _wrapper{prepare_home(home), conn_cfg};
    WT_SESSION_IMPL *_session{_wrapper.create_session()};
    WT_CURSOR *_cursor{};
};

} // namespace

SCENARIO(
  "WT-17266: truncate iterate stops at boundary instead of leaking past it",
  "[layered][wt-17266]")
{
    GIVEN("a follower with keys a..e and a pending truncate of b..d")
    {
        follower_conn_17266 fc;

        /* Register a truncate entry covering b..d */
        auto start = make_item("b");
        auto stop = make_item("d");
        REQUIRE(
          __wt_insert_truncate_entry(&fc.session_impl(), &fc.layered_table(), &start, &stop) == 0);

        WHEN("the cursor iterates from the start")
        {
            fc.cursor()->reset(fc.cursor());
            int ret = fc.cursor()->next(fc.cursor());

            THEN("the first visible key is 'a' (before the truncated range)")
            {
                /*
                 * Before fix: WT_RET_NOTFOUND_OK in the iterate path causes the
                 * cursor to bleed past the truncation boundary and expose keys
                 * inside the truncated range.
                 * After fix: iteration stops at the boundary and 'a' is visible.
                 */
                REQUIRE(ret == 0);
                const char *key = nullptr;
                REQUIRE(fc.cursor()->get_key(fc.cursor(), &key) == 0);
                REQUIRE(std::string_view(key) == "a");
            }
        }
    }
}
