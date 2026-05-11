/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17379 regression test
 *
 * Bug: in __clayered_iterate_constituents, when the ingest cursor became
 * unpositioned after returning WT_PREPARE_CONFLICT and the prepared
 * transaction was then rolled back, the subsequent cursor->next() call
 * invoked __clayered_cursor_compare with an unpositioned cursor, triggering
 * WT_ASSERT_ALWAYS.
 *
 * Fix: added `F_ISSET(c_current, WT_CURSTD_KEY_INT)` guard before the
 * comparison, so the key-equal advancement step is skipped when the current
 * (ingest) cursor has no key (~line 1161 of cur_layered.c).
 *
 * WHY Catch2 + fff cannot cover this bug:
 *
 *   The crash path requires all of the following in a single run:
 *     1. a LEADER WiredTiger connection that commits keys and checkpoints,
 *     2. a FOLLOWER connection that advances to that checkpoint so the stable
 *        btree contains committed data,
 *     3. a prepared transaction in the follower ingest layer,
 *     4. a read cursor at a timestamp that sees the prepared key as
 *        WT_PREPARE_CONFLICT mid-scan,
 *     5. rollback of the prepared transaction, and
 *     6. cursor->next() again -- only NOW does the assert fire.
 *
 *   This multi-connection, cross-layer, timestamped scenario cannot be set up
 *   with a mock session or a single follower_connection fixture.  The exact
 *   regression is fully covered by the Python integration test
 *   test/suite/test_layered101.py (added in the same commit as the fix).
 *
 *   This file exists only to hold the explanation so future readers know
 *   the coverage gap is intentional, not an oversight.
 */

#include <catch2/catch.hpp>

SCENARIO(
  "WT-17379: not coverable by Catch2 (multi-connection prepared-txn scenario)",
  "[layered][wt-17379]")
{
    /*
     * This scenario requires two WiredTiger connections (leader + follower),
     * prepared transactions, read timestamps, and a WT_PREPARE_CONFLICT mid-scan.
     * See the comment at the top of this file and test_layered101.py for details.
     */
    GIVEN("no testable precondition exists in this framework")
    {
        THEN("the test is acknowledged as out-of-scope for Catch2 + fff")
        {
            SUCCEED("WT-17379 coverage provided by test_layered101.py");
        }
    }
}
