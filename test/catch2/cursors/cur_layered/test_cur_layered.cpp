/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <fff.h>

#include "wt_internal.h"
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
int __clayered_put(
  WT_SESSION_IMPL *, WT_CURSOR_LAYERED *, const WT_ITEM *, const WT_ITEM *, WT_CLAYERED_PUT_OP);
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
capture_ingest_key(WT_CURSOR *, va_list ap)
{
    ingest_set_key_fake_item = va_arg(ap, const WT_ITEM *);
}

static WT_ITEM ingest_get_value_fake_item{};

static int
return_ingest_value(WT_CURSOR *, va_list ap)
{
    WT_ITEM *out = va_arg(ap, WT_ITEM *);

    if (out != nullptr)
        *out = ingest_get_value_fake_item;

    return 0;
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
    ingest_get_value_fake_item = {};
}

class layered_cursor_fixture {
public:
    WT_CURSOR ingest = {};
    WT_CURSOR stable_cursor = {};
    WT_CURSOR_LAYERED layered = {};

    explicit layered_cursor_fixture()
    {
        _mock_session = mock_session::build_test_mock_session();
        _session = _mock_session->get_wt_session_impl();
        wire_cursors();
        reset_all_fakes();
        set_follower();

        layered.iface.key_format = "S";
        layered.iface.value_format = "u";
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
        ingest.set_key = ingest_set_key;
        ingest.set_value = ingest_set_value;
        ingest.get_value = ingest_get_value;
        ingest.search = ingest_search;
        ingest.insert = ingest_insert;
        ingest.update = ingest_update;
        ingest.remove = ingest_remove;
        ingest.reset = ingest_reset;

        stable_cursor.set_key = stable_set_key;
        stable_cursor.set_value = stable_set_value;
        stable_cursor.get_value = stable_get_value;
        stable_cursor.search = stable_search;
        stable_cursor.insert = stable_insert;
        stable_cursor.update = stable_update;
        stable_cursor.remove = stable_remove;
        stable_cursor.reset = stable_reset;

        layered.ingest_cursor = &ingest;
        layered.stable_cursor = &stable_cursor;
        layered.current_cursor = nullptr;
        layered.iface.session = reinterpret_cast<WT_SESSION *>(_session);
        layered.iface.get_key = __wt_cursor_get_key;
        layered.iface.get_value = __wt_cursor_get_value;
        layered.iface.get_raw_key_value = __wt_cursor_get_raw_key_value;
        layered.iface.set_key = __wt_cursor_set_key;
        layered.iface.set_value = __wt_cursor_set_value;
        layered.iface.compare = __clayered_compare;
        layered.iface.equals = __wt_cursor_equals;
        layered.iface.next = __clayered_next;
        layered.iface.prev = __clayered_prev;
        layered.iface.reset = __clayered_reset;
        layered.iface.search = __clayered_search;
        layered.iface.search_near = __clayered_search_near;
        layered.iface.insert = __clayered_insert;
        layered.iface.modify = __clayered_modify;
        layered.iface.update = __clayered_update;
        layered.iface.remove = __clayered_remove;
        layered.iface.reserve = __clayered_reserve;
        layered.iface.reconfigure = __wti_cursor_reconfigure;
        layered.iface.largest_key = __clayered_largest_key;
        layered.iface.bound = __clayered_bound;
        layered.iface.cache = __clayered_cache;
        layered.iface.reopen = __clayered_reopen;
        layered.iface.checkpoint_id = __wt_cursor_checkpoint_id;
        layered.iface.close = __clayered_close;
    }

    std::shared_ptr<mock_session> _mock_session;
    WT_SESSION_IMPL *_session = nullptr;
};

} // namespace

/* ─── Group 1: tombstone detection ──────────────────────────────────────── */

SCENARIO("clayered_deleted correctly identifies tombstone values", "[layered_cursor][tombstone]")
{
    GIVEN("a WT_ITEM")
    {
        WT_ITEM item{};

        WHEN("the item is zero-length")
        {
            // item already default-initialised to zero-length

            THEN("it is not considered deleted")
            {
                REQUIRE(__wt_clayered_deleted(&item) == false);
            }
        }

        WHEN("the item contains the exact two-byte tombstone value")
        {
            item.data = "\x14\x14";
            item.size = 2;

            THEN("it is considered deleted")
            {
                REQUIRE(__wt_clayered_deleted(&item) == true);
            }
        }

        WHEN("the item has tombstone bytes followed by a trailing byte")
        {
            item.data = "\x14\x14\x01";
            item.size = 3;

            THEN("it is not considered deleted")
            {
                REQUIRE(__wt_clayered_deleted(&item) == false);
            }
        }

        WHEN("the item has the right size but wrong bytes")
        {
            item.data = "\x14\x15";
            item.size = 2;

            THEN("it is not considered deleted")
            {
                REQUIRE(__wt_clayered_deleted(&item) == false);
            }
        }
    }
}

/* ─── Group 2: __clayered_lookup_constituent ────────────────────────────── */

SCENARIO("lookup_constituent correctly searches a constituent cursor for a key",
  "[layered_cursor][lookup]")
{
    layered_cursor_fixture f;

    GIVEN("a layered cursor with a key set")
    {
        auto *iface = &f.layered.iface;

        constexpr std::string_view key = "key123";
        iface->key.data = key.data();
        iface->key.size = key.size() + 1;

        WHEN("any search outcome occurs")
        {
            const auto outcome = GENERATE(0, WT_NOTFOUND, WT_PANIC);
            ingest_search_fake.return_val = outcome;

            ingest_set_key_fake.custom_fake = capture_ingest_key;
            const auto ret = __clayered_lookup_constituent(&f.ingest, &f.layered, nullptr);

            THEN("the key is forwarded to the constituent cursor")
            {
                REQUIRE(ingest_set_key_fake_item == &iface->key);
            }
        }

        WHEN("any unsuccessful search outcome occurs")
        {
            const auto outcome = GENERATE(WT_NOTFOUND, WT_PANIC);
            ingest_search_fake.return_val = outcome;

            const auto ret = __clayered_lookup_constituent(&f.ingest, &f.layered, nullptr);

            THEN("the current cursor is not updated")
            {
                REQUIRE(f.layered.current_cursor == nullptr);
            }
        }

        WHEN("the constituent cursor finds the key")
        {
            ingest_search_fake.return_val = 0;
            ingest_get_value_fake.custom_fake = return_ingest_value;

            WT_ITEM value{};
            const auto ret = __clayered_lookup_constituent(&f.ingest, &f.layered, &value);

            THEN("0 is returned")
            {
                REQUIRE(ret == 0);
            }

            AND_THEN("the current cursor is updated to the constituent cursor")
            {
                REQUIRE(f.layered.current_cursor == &f.ingest);
            }

            AND_THEN("the value is retrieved from the constituent cursor")
            {
                REQUIRE(value.data == ingest_get_value_fake_item.data);
                REQUIRE(value.size == ingest_get_value_fake_item.size);
            }
        }

        WHEN("the constituent cursor does not find the key")
        {
            ingest_search_fake.return_val = WT_NOTFOUND;
            const auto ret = __clayered_lookup_constituent(&f.ingest, &f.layered, nullptr);

            THEN("WT_NOTFOUND is returned")
            {
                REQUIRE(ret == WT_NOTFOUND);
            }
        }

        WHEN("a hard error occurs during the search")
        {
            ingest_search_fake.return_val = WT_PANIC;
            const auto ret = __clayered_lookup_constituent(&f.ingest, &f.layered, nullptr);

            THEN("the error is returned")
            {
                REQUIRE(ret == WT_PANIC);
            }
        }

        WHEN("search succeeds but there is an error getting the value")
        {
            ingest_search_fake.return_val = 0;
            ingest_get_value_fake.return_val = WT_ROLLBACK;

            const auto ret = __clayered_lookup_constituent(&f.ingest, &f.layered, nullptr);

            THEN("the error is returned")
            {
                REQUIRE(ret == WT_ROLLBACK);
            }
        }
    }
}
