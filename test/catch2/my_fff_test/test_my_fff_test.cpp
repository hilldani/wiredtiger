/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * wiredtiger_strerror  (api_strerror.c)  calls  __wt_strerror  (os_errno.c).
 * The two functions are in different translation units, so their archive members
 * are separate objects.  Defining an FFF fake for __wt_strerror in this test
 * satisfies the linker before it ever loads os_errno.c.o — no weak attributes,
 * no linker flags, just basic archive-member resolution order.
 */

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "wt_internal.h"
#include "fff.h"

DEFINE_FFF_GLOBALS;

extern "C" {
FAKE_VALUE_FUNC(const char *, __wt_strerror, WT_SESSION_IMPL *, int, char *, size_t);
}

TEST_CASE("wiredtiger_strerror returns what __wt_strerror returns", "[my_fff_test]")
{
    __wt_strerror_fake.return_val = "injected_error_string";

    const char *result = wiredtiger_strerror(WT_ERROR);

    REQUIRE(std::string(result) == "injected_error_string");
    REQUIRE(__wt_strerror_fake.call_count == 1);
}
