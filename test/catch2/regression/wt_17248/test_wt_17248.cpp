/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * WT-17248 regression test
 *
 * Bug: __conn_chunk_cache_check returned EINVAL when chunk_cache was enabled,
 * instead of ENOTSUP.  The EINVAL error code triggered the data-corruption
 * mapping path in the connection error handler, incorrectly setting
 * WT_CONN_DATA_CORRUPTION on the connection.
 *
 * Fix: changed `WT_RET_MSG(session, EINVAL, …)` to
 *      `WT_RET_MSG(session, ENOTSUP, …)`
 * (~line 1625 of conn_api.c).
 */

#include <catch2/catch.hpp>

#include "../../wrappers/mock_session.h"

extern "C" {
#include "wt_internal.h"
}

SCENARIO(
  "WT-17248: chunk_cache deprecation returns ENOTSUP, not EINVAL",
  "[conn][wt-17248]")
{
    GIVEN("a mock session and a config string with chunk_cache enabled")
    {
        auto mock = mock_session::build_test_mock_session();
        mock->setup_block_manager_file_operations();
        WT_SESSION_IMPL *session = mock->get_wt_session_impl();

        const char *config = "chunk_cache=(enabled=true)";

        WHEN("__ut_conn_chunk_cache_check is called")
        {
            int ret = __ut_conn_chunk_cache_check(session, config);

            THEN("it returns ENOTSUP (not EINVAL)")
            {
                REQUIRE(ret == ENOTSUP);
            }

            THEN("WT_CONN_DATA_CORRUPTION is not set on the connection")
            {
                REQUIRE(!F_ISSET(S2C(session), WT_CONN_DATA_CORRUPTION));
            }
        }
    }

    GIVEN("a config string with chunk_cache disabled")
    {
        auto mock = mock_session::build_test_mock_session();
        mock->setup_block_manager_file_operations();
        WT_SESSION_IMPL *session = mock->get_wt_session_impl();

        const char *config = "chunk_cache=(enabled=false)";

        WHEN("__ut_conn_chunk_cache_check is called")
        {
            int ret = __ut_conn_chunk_cache_check(session, config);

            THEN("it returns 0 (deprecated warning, not an error)")
            {
                REQUIRE(ret == 0);
            }
        }
    }

    GIVEN("a config string with no chunk_cache key")
    {
        auto mock = mock_session::build_test_mock_session();
        mock->setup_block_manager_file_operations();
        WT_SESSION_IMPL *session = mock->get_wt_session_impl();

        WHEN("__ut_conn_chunk_cache_check is called with unrelated config")
        {
            int ret = __ut_conn_chunk_cache_check(session, "create=true");

            THEN("it returns 0")
            {
                REQUIRE(ret == 0);
            }
        }
    }
}
