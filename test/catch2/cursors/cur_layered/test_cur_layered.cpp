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

DEFINE_FFF_GLOBALS;

extern "C" {
FAKE_VALUE_FUNC(int, __wt_layered_table_truncate_detect_write_conflict, WT_SESSION_IMPL *,
  WT_LAYERED_TABLE *, const WT_ITEM *);
}

SCENARIO("placeholder — FFF globals compile and link", "[layered_cursor]")
{
    GIVEN("a reset fake")
    {
        RESET_FAKE(__wt_layered_table_truncate_detect_write_conflict);

        WHEN("the fake has not been called")
        {
            THEN("call count is zero")
            {
                REQUIRE(__wt_layered_table_truncate_detect_write_conflict_fake.call_count == 0);
            }
        }
    }
}
