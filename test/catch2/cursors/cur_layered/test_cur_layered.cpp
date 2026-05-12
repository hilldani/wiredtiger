/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "wt_internal.h"
#include "fff.h"
#include "wrappers/mock_session.h"

DEFINE_FFF_GLOBALS;

/* ─── FFF fakes ─────────────────────────────────────────────────────────── */

extern "C" {
FAKE_VALUE_FUNC(int, __wt_layered_table_truncate_detect_write_conflict, WT_SESSION_IMPL *,
  WT_LAYERED_TABLE *, const WT_ITEM *);

FAKE_VALUE_FUNC(int, ingest_search, WT_CURSOR *);
FAKE_VALUE_FUNC(int, ingest_insert, WT_CURSOR *);
FAKE_VALUE_FUNC(int, ingest_update, WT_CURSOR *);
FAKE_VALUE_FUNC(int, ingest_remove, WT_CURSOR *);
FAKE_VALUE_FUNC(int, ingest_reset, WT_CURSOR *);
FAKE_VALUE_FUNC_VARARG(int, ingest_get_value, WT_CURSOR *, ...);
FAKE_VOID_FUNC_VARARG(ingest_set_key, WT_CURSOR *, ...);
FAKE_VOID_FUNC_VARARG(ingest_set_value, WT_CURSOR *, ...);

FAKE_VALUE_FUNC(int, stable_search, WT_CURSOR *);
FAKE_VALUE_FUNC(int, stable_insert, WT_CURSOR *);
FAKE_VALUE_FUNC(int, stable_update, WT_CURSOR *);
FAKE_VALUE_FUNC(int, stable_remove, WT_CURSOR *);
FAKE_VALUE_FUNC(int, stable_reset, WT_CURSOR *);
FAKE_VALUE_FUNC_VARARG(int, stable_get_value, WT_CURSOR *, ...);
FAKE_VOID_FUNC_VARARG(stable_set_key, WT_CURSOR *, ...);
FAKE_VOID_FUNC_VARARG(stable_set_value, WT_CURSOR *, ...);
}

/* ─── Functions under test (made non-static in cur_layered.c) ───────────── */

extern "C" {
int __clayered_lookup_constituent(WT_CURSOR *, WT_CURSOR_LAYERED *, WT_ITEM *);
int __clayered_put(WT_SESSION_IMPL *, WT_CURSOR_LAYERED *, const WT_ITEM *, const WT_ITEM *,
  WT_CLAYERED_PUT_OP);
int __clayered_remove_leader(WT_SESSION_IMPL *, WT_CURSOR_LAYERED *, const WT_ITEM *, bool);
}

/* ─── Fixture ────────────────────────────────────────────────────────────── */

namespace {

static void
reset_all_fakes()
{
    RESET_FAKE(__wt_layered_table_truncate_detect_write_conflict);
    RESET_FAKE(ingest_search);
    RESET_FAKE(ingest_insert);
    RESET_FAKE(ingest_update);
    RESET_FAKE(ingest_remove);
    RESET_FAKE(ingest_reset);
    RESET_FAKE(ingest_get_value);
    RESET_FAKE(ingest_set_key);
    RESET_FAKE(ingest_set_value);
    RESET_FAKE(stable_search);
    RESET_FAKE(stable_insert);
    RESET_FAKE(stable_update);
    RESET_FAKE(stable_remove);
    RESET_FAKE(stable_reset);
    RESET_FAKE(stable_get_value);
    RESET_FAKE(stable_set_key);
    RESET_FAKE(stable_set_value);
}

class layered_cursor_fixture {
public:
    WT_CURSOR ingest_cursor = {};
    WT_CURSOR stable_cursor = {};
    WT_CURSOR_LAYERED clayered = {};

    explicit layered_cursor_fixture()
    {
        _mock_session = mock_session::build_test_mock_session();
        _session = _mock_session->get_wt_session_impl();
        wire_cursors();
        reset_all_fakes();
        set_follower();
    }

    WT_SESSION_IMPL *
    session() const
    {
        return _session;
    }

    void
    set_follower()
    {
        S2C(_session)->layered_table_manager.leader = false;
    }

    void
    set_leader()
    {
        S2C(_session)->layered_table_manager.leader = true;
    }

private:
    void
    wire_cursors()
    {
        ingest_cursor.set_key = ingest_set_key;
        ingest_cursor.set_value = ingest_set_value;
        ingest_cursor.get_value = ingest_get_value;
        ingest_cursor.search = ingest_search;
        ingest_cursor.insert = ingest_insert;
        ingest_cursor.update = ingest_update;
        ingest_cursor.remove = ingest_remove;
        ingest_cursor.reset = ingest_reset;

        stable_cursor.set_key = stable_set_key;
        stable_cursor.set_value = stable_set_value;
        stable_cursor.get_value = stable_get_value;
        stable_cursor.search = stable_search;
        stable_cursor.insert = stable_insert;
        stable_cursor.update = stable_update;
        stable_cursor.remove = stable_remove;
        stable_cursor.reset = stable_reset;

        clayered.ingest_cursor = &ingest_cursor;
        clayered.stable_cursor = &stable_cursor;
        clayered.current_cursor = nullptr;
        clayered.iface.session = reinterpret_cast<WT_SESSION *>(_session);
    }

    std::shared_ptr<mock_session> _mock_session;
    WT_SESSION_IMPL *_session = nullptr;
};

} // namespace

/* ─── Smoke tests for the fixture ───────────────────────────────────────── */

SCENARIO("fixture wires constituent cursors into the layered cursor", "[layered_cursor][fixture]")
{
    GIVEN("a default-constructed fixture")
    {
        layered_cursor_fixture fx;

        THEN("ingest_cursor points at the ingest cursor")
        {
            REQUIRE(fx.clayered.ingest_cursor == &fx.ingest_cursor);
        }

        THEN("stable_cursor points at the stable cursor")
        {
            REQUIRE(fx.clayered.stable_cursor == &fx.stable_cursor);
        }

        THEN("current_cursor is null")
        {
            REQUIRE(fx.clayered.current_cursor == nullptr);
        }

        THEN("session is wired so CUR2S resolves")
        {
            REQUIRE(fx.clayered.iface.session == reinterpret_cast<WT_SESSION *>(fx.session()));
        }
    }
}

SCENARIO("fixture defaults to follower mode", "[layered_cursor][fixture]")
{
    GIVEN("a default-constructed fixture")
    {
        layered_cursor_fixture fx;

        THEN("the connection reports leader = false")
        {
            REQUIRE(S2C(fx.session())->layered_table_manager.leader == false);
        }

        WHEN("set_leader is called")
        {
            fx.set_leader();

            THEN("the connection reports leader = true")
            {
                REQUIRE(S2C(fx.session())->layered_table_manager.leader == true);
            }
        }
    }
}

SCENARIO("FFF fakes track calls independently per cursor", "[layered_cursor][fixture]")
{
    GIVEN("a default-constructed fixture")
    {
        layered_cursor_fixture fx;

        WHEN("search is called on the ingest cursor")
        {
            ingest_search(&fx.ingest_cursor);

            THEN("ingest_search call count is 1")
            {
                REQUIRE(ingest_search_fake.call_count == 1);
            }

            THEN("stable_search call count remains 0")
            {
                REQUIRE(stable_search_fake.call_count == 0);
            }
        }

        WHEN("stable_search is configured to return WT_NOTFOUND")
        {
            stable_search_fake.return_val = WT_NOTFOUND;
            int ret = stable_search(&fx.stable_cursor);

            THEN("the configured value is returned")
            {
                REQUIRE(ret == WT_NOTFOUND);
            }
        }
    }
}
