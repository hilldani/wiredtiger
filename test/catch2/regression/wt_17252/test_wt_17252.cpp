/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17252 regression test
 *
 * Bug: in __clayered_insert and __clayered_update, the overwrite flag was read
 * from `clayered` (the internal WT_CURSOR_LAYERED) instead of `cursor` (the
 * outer WT_CURSOR visible to the caller).  On insert/update with
 * overwrite=true set on the outer cursor only, the stable constituent was
 * incorrectly opened or skipped.
 *
 * Fix: changed `F_ISSET(clayered, WT_CURSTD_OVERWRITE)` to
 *      `F_ISSET(cursor, WT_CURSTD_OVERWRITE)`
 * (~lines 2145 and 2207 of cur_layered.c).
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

class follower_conn_17252 {
public:
    follower_conn_17252()
    {
        constexpr auto uri = "layered:test_17252";
        constexpr auto cfg = "key_format=S,value_format=S,block_manager=disagg,type=layered";
        auto &s = _session->iface;
        REQUIRE(s.create(&s, uri, cfg) == 0);

        /* Open cursor with overwrite=true */
        REQUIRE(s.open_cursor(&s, uri, nullptr, "overwrite=true", &_cursor) == 0);
        REQUIRE(s.begin_transaction(&s, nullptr) == 0);
    }

    ~follower_conn_17252()
    {
        auto *txn = _session->txn;
        for (size_t i = 0; i < txn->mod_count; ++i)
            __wt_txn_op_free(_session, &txn->mod[i]);
        txn->mod_count = 0;
    }

    WT_CURSOR *cursor() { return _cursor; }

private:
    static constexpr auto home = "WT_TEST.wt_17252";
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
  "WT-17252: cursor->insert with overwrite=true on outer cursor succeeds",
  "[layered][wt-17252]")
{
    GIVEN("a layered cursor opened with overwrite=true")
    {
        follower_conn_17252 fc;

        WHEN("the same key is inserted twice")
        {
            fc.cursor()->set_key(fc.cursor(), "key1");
            fc.cursor()->set_value(fc.cursor(), "val1");
            int ret1 = fc.cursor()->insert(fc.cursor());

            fc.cursor()->set_key(fc.cursor(), "key1");
            fc.cursor()->set_value(fc.cursor(), "val2");
            int ret2 = fc.cursor()->insert(fc.cursor());

            THEN("both inserts succeed (overwrite=true on outer cursor is honoured)")
            {
                /*
                 * Before fix: WT_CURSTD_OVERWRITE checked on the inner clayered
                 * cursor (which doesn't have the flag), so the second insert
                 * returns WT_DUPLICATE_KEY instead of 0.
                 * After fix: flag is read from the outer cursor → returns 0.
                 */
                REQUIRE(ret1 == 0);
                REQUIRE(ret2 == 0);
            }
        }
    }
}
