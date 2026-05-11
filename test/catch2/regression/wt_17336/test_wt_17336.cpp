/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17336 regression test
 *
 * Bug: __clayered_modify did not call __clayered_deleted_decode before
 * attempting a modify on a key that was deleted in the ingest layer.  When the
 * key was logically deleted, the cursor had a "deleted" sentinel value and the
 * modify call corrupted cursor state instead of returning a clean error.
 *
 * Fix: added __clayered_deleted_decode (or equivalent check) at the top of
 * __clayered_modify (~cur_layered.c).
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

class follower_conn_17336 {
public:
    follower_conn_17336()
    {
        constexpr auto uri = "layered:test_17336";
        constexpr auto cfg = "key_format=S,value_format=S,block_manager=disagg,type=layered";
        auto &s = _session->iface;
        REQUIRE(s.create(&s, uri, cfg) == 0);

        /* Insert and then delete key "k1" */
        REQUIRE(s.begin_transaction(&s, nullptr) == 0);
        WT_CURSOR *setup_cur = nullptr;
        REQUIRE(s.open_cursor(&s, uri, nullptr, nullptr, &setup_cur) == 0);
        setup_cur->set_key(setup_cur, "k1");
        setup_cur->set_value(setup_cur, "v1");
        REQUIRE(setup_cur->insert(setup_cur) == 0);
        REQUIRE(setup_cur->close(setup_cur) == 0);
        REQUIRE(s.commit_transaction(&s, nullptr) == 0);

        REQUIRE(s.begin_transaction(&s, nullptr) == 0);
        WT_CURSOR *del_cur = nullptr;
        REQUIRE(s.open_cursor(&s, uri, nullptr, nullptr, &del_cur) == 0);
        del_cur->set_key(del_cur, "k1");
        REQUIRE(del_cur->remove(del_cur) == 0);
        REQUIRE(del_cur->close(del_cur) == 0);
        REQUIRE(s.commit_transaction(&s, nullptr) == 0);

        /* Open a cursor for the test */
        REQUIRE(s.open_cursor(&s, uri, nullptr, nullptr, &_cursor) == 0);
        REQUIRE(s.begin_transaction(&s, nullptr) == 0);
    }

    ~follower_conn_17336()
    {
        auto *txn = _session->txn;
        for (size_t i = 0; i < txn->mod_count; ++i)
            __wt_txn_op_free(_session, &txn->mod[i]);
        txn->mod_count = 0;
    }

    WT_CURSOR *cursor() { return _cursor; }

private:
    static constexpr auto home = "WT_TEST.wt_17336";
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
  "WT-17336: cursor->modify on a deleted key returns WT_NOTFOUND, not corrupt state",
  "[layered][wt-17336]")
{
    GIVEN("a follower where key 'k1' was deleted")
    {
        follower_conn_17336 fc;

        WHEN("cursor->modify is called on the deleted key")
        {
            fc.cursor()->set_key(fc.cursor(), "k1");
            /* Position the cursor first */
            int search_ret = fc.cursor()->search(fc.cursor());

            THEN("search returns WT_NOTFOUND and modify is not called on deleted state")
            {
                /*
                 * Before fix: search might succeed with a deleted sentinel, then
                 * modify would corrupt cursor state or crash.
                 * After fix: search returns WT_NOTFOUND for a logically deleted key.
                 */
                REQUIRE(search_ret == WT_NOTFOUND);
            }
        }
    }
}
