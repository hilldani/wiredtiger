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

/* ─── Group 1: tombstone detection ──────────────────────────────────────── */

SCENARIO("empty item is not a tombstone", "[layered_cursor][tombstone]")
{
    GIVEN("a zero-length WT_ITEM")
    {
        WT_ITEM item{};

        WHEN("__wt_clayered_deleted is called")
        {
            const bool result = __wt_clayered_deleted(&item);

            THEN("it returns false")
            {
                REQUIRE(result == false);
            }
        }
    }
}

SCENARIO("exact tombstone bytes are recognised as deleted", "[layered_cursor][tombstone]")
{
    GIVEN("a WT_ITEM containing the two-byte tombstone value")
    {
        WT_ITEM item{};
        item.data = "\x14\x14";
        item.size = 2;

        WHEN("__wt_clayered_deleted is called")
        {
            const bool result = __wt_clayered_deleted(&item);

            THEN("it returns true")
            {
                REQUIRE(result == true);
            }
        }
    }
}

SCENARIO("tombstone prefix with trailing byte is not deleted", "[layered_cursor][tombstone]")
{
    GIVEN("a WT_ITEM with tombstone bytes followed by an extra byte")
    {
        WT_ITEM item{};
        item.data = "\x14\x14\x01";
        item.size = 3;

        WHEN("__wt_clayered_deleted is called")
        {
            const bool result = __wt_clayered_deleted(&item);

            THEN("it returns false")
            {
                REQUIRE(result == false);
            }
        }
    }
}

SCENARIO("two-byte item with wrong bytes is not deleted", "[layered_cursor][tombstone]")
{
    GIVEN("a WT_ITEM with size 2 but data that differs from the tombstone")
    {
        WT_ITEM item{};
        item.data = "\x14\x15";
        item.size = 2;

        WHEN("__wt_clayered_deleted is called")
        {
            const bool result = __wt_clayered_deleted(&item);

            THEN("it returns false")
            {
                REQUIRE(result == false);
            }
        }
    }
}
