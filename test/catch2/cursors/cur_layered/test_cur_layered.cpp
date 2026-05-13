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
int __clayered_compare(WT_CURSOR *, WT_CURSOR *, int *);
int __clayered_next(WT_CURSOR *);
int __clayered_prev(WT_CURSOR *);
int __clayered_reset(WT_CURSOR *);
int __clayered_bound(WT_CURSOR *, const char *);
int __clayered_cache(WT_CURSOR *);
int __clayered_reopen(WT_CURSOR *, bool);
int __clayered_search(WT_CURSOR *);
int __clayered_search_near(WT_CURSOR *, int *);
int __clayered_insert(WT_CURSOR *);
int __clayered_modify(WT_CURSOR *, WT_MODIFY *, int);
int __clayered_update(WT_CURSOR *);
int __clayered_remove(WT_CURSOR *);
int __clayered_reserve(WT_CURSOR *);
int __clayered_largest_key(WT_CURSOR *);
int __clayered_close(WT_CURSOR *);
}

/* ─── Fixture ────────────────────────────────────────────────────────────── */

namespace {

static const WT_ITEM *ingest_set_key_fake_item = nullptr;

static void
ingest_set_key_capture(WT_CURSOR *, va_list ap)
{
    ingest_set_key_fake_item = va_arg(ap, const WT_ITEM *);
}

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

    ingest_set_key_fake_item = nullptr;
    ingest_set_key_fake.custom_fake = ingest_set_key_capture;
}

class layered_cursor_fixture {
public:
    WT_CURSOR ingest_cursor = {};
    WT_CURSOR stable_cursor = {};
    WT_CURSOR_LAYERED layered_cursor = {};

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

        layered_cursor.ingest_cursor = &ingest_cursor;
        layered_cursor.stable_cursor = &stable_cursor;
        layered_cursor.current_cursor = nullptr;
        layered_cursor.iface.session = reinterpret_cast<WT_SESSION *>(_session);
        layered_cursor.iface.get_key = __wt_cursor_get_key;
        layered_cursor.iface.get_value = __wt_cursor_get_value;
        layered_cursor.iface.get_raw_key_value = __wt_cursor_get_raw_key_value;
        layered_cursor.iface.set_key = __wt_cursor_set_key;
        layered_cursor.iface.set_value = __wt_cursor_set_value;
        layered_cursor.iface.compare = __clayered_compare;
        layered_cursor.iface.equals = __wt_cursor_equals;
        layered_cursor.iface.next = __clayered_next;
        layered_cursor.iface.prev = __clayered_prev;
        layered_cursor.iface.reset = __clayered_reset;
        layered_cursor.iface.search = __clayered_search;
        layered_cursor.iface.search_near = __clayered_search_near;
        layered_cursor.iface.insert = __clayered_insert;
        layered_cursor.iface.modify = __clayered_modify;
        layered_cursor.iface.update = __clayered_update;
        layered_cursor.iface.remove = __clayered_remove;
        layered_cursor.iface.reserve = __clayered_reserve;
        layered_cursor.iface.reconfigure = __wti_cursor_reconfigure;
        layered_cursor.iface.largest_key = __clayered_largest_key;
        layered_cursor.iface.bound = __clayered_bound;
        layered_cursor.iface.cache = __clayered_cache;
        layered_cursor.iface.reopen = __clayered_reopen;
        layered_cursor.iface.checkpoint_id = __wt_cursor_checkpoint_id;
        layered_cursor.iface.close = __clayered_close;
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

/* ─── Group 2: __clayered_lookup_constituent ────────────────────────────── */

SCENARIO(
  "lookup_constituent returns 0 and wires current_cursor on a hit", "[layered_cursor][lookup]")
{
    layered_cursor_fixture f;
    WT_ITEM value{};

    GIVEN("an ingest cursor whose search returns 0")
    {
        ingest_search_fake.return_val = 0;
        ingest_get_value_fake.return_val = 0;

        WHEN("__clayered_lookup_constituent is called with the ingest cursor")
        {
            int ret = __clayered_lookup_constituent(&f.ingest_cursor, &f.layered_cursor, &value);

            THEN("it returns 0, sets current_cursor, and calls get_value once")
            {
                REQUIRE(ret == 0);
                REQUIRE(f.layered_cursor.current_cursor == &f.ingest_cursor);
                REQUIRE(ingest_get_value_fake.call_count == 1);
            }
        }
    }
}

SCENARIO("lookup_constituent propagates WT_NOTFOUND and leaves current_cursor unchanged",
  "[layered_cursor][lookup]")
{
    layered_cursor_fixture f;
    WT_ITEM value{};

    GIVEN("an ingest cursor whose search returns WT_NOTFOUND")
    {
        ingest_search_fake.return_val = WT_NOTFOUND;

        WHEN("__clayered_lookup_constituent is called")
        {
            int ret = __clayered_lookup_constituent(&f.ingest_cursor, &f.layered_cursor, &value);

            THEN("it returns WT_NOTFOUND, skips get_value, and leaves current_cursor null")
            {
                REQUIRE(ret == WT_NOTFOUND);
                REQUIRE(f.layered_cursor.current_cursor == nullptr);
                REQUIRE(ingest_get_value_fake.call_count == 0);
            }
        }
    }
}

SCENARIO("lookup_constituent always sets the layered cursor key on the constituent",
  "[layered_cursor][lookup]")
{
    layered_cursor_fixture f;

    GIVEN("a layered cursor with a key set")
    {
        auto* iface = &f.layered_cursor.iface;
        constexpr std::string_view key = "key123";
        __wt_cursor_set_key(iface, key.data());

        WHEN("__clayered_lookup_constituent is called")
        {
            WT_ITEM value{};
            __clayered_lookup_constituent(&f.ingest_cursor, &f.layered_cursor, &value);

            THEN("the key is forwarded to the constituent cursor")
            {
                REQUIRE(ingest_set_key_fake.call_count == 1);
                REQUIRE(ingest_set_key_fake_item == &f.layered_cursor.iface.key);
            }
        }
    }
}

SCENARIO("lookup_constituent propagates get_value errors without setting current_cursor",
  "[layered_cursor][lookup]")
{
    layered_cursor_fixture f;
    WT_ITEM value{};

    GIVEN("a cursor whose search succeeds but get_value returns EINVAL")
    {
        ingest_search_fake.return_val = 0;
        ingest_get_value_fake.return_val = EINVAL;

        WHEN("__clayered_lookup_constituent is called")
        {
            int ret = __clayered_lookup_constituent(&f.ingest_cursor, &f.layered_cursor, &value);

            THEN("it returns EINVAL and does not update current_cursor")
            {
                REQUIRE(ret == EINVAL);
                REQUIRE(f.layered_cursor.current_cursor == nullptr);
            }
        }
    }
}

SCENARIO("lookup_constituent sets current_cursor to the stable cursor on a stable hit",
  "[layered_cursor][lookup]")
{
    layered_cursor_fixture f;
    WT_ITEM value{};

    GIVEN("a stable cursor whose search returns 0")
    {
        stable_search_fake.return_val = 0;
        stable_get_value_fake.return_val = 0;

        WHEN("__clayered_lookup_constituent is called with the stable cursor")
        {
            int ret = __clayered_lookup_constituent(&f.stable_cursor, &f.layered_cursor, &value);

            THEN("it returns 0 and current_cursor points to the stable cursor")
            {
                REQUIRE(ret == 0);
                REQUIRE(f.layered_cursor.current_cursor == &f.stable_cursor);
            }
        }
    }
}

SCENARIO("lookup_constituent preserves current_cursor when search misses",
  "[layered_cursor][lookup]")
{
    layered_cursor_fixture f;
    WT_ITEM value{};

    GIVEN("current_cursor already points to the stable cursor")
    {
        f.layered_cursor.current_cursor = &f.stable_cursor;
        ingest_search_fake.return_val = WT_NOTFOUND;

        WHEN("__clayered_lookup_constituent is called with the ingest cursor and misses")
        {
            __clayered_lookup_constituent(&f.ingest_cursor, &f.layered_cursor, &value);

            THEN("current_cursor still points to the stable cursor")
            {
                REQUIRE(f.layered_cursor.current_cursor == &f.stable_cursor);
            }
        }
    }
}
