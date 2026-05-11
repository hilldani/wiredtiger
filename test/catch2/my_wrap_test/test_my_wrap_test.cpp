/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * --wrap=funcname (GNU ld / LLVM lld only; not Apple ld) tells the linker to
 * redirect every call site for funcname to __wrap_funcname.  The original
 * implementation remains reachable as __real_funcname.
 *
 * This is the same-translation-unit case that defeats plain archive-resolution
 * ordering: wiredtiger_unpack_start and wiredtiger_pack_start both live in
 * pack_stream.c.o.  Loading that archive member to supply wiredtiger_unpack_start
 * also brings in the real wiredtiger_pack_start, so a same-named FFF fake causes
 * a duplicate-symbol error.  --wrap sidesteps this entirely — it operates on
 * call sites, not on symbol definitions, so the duplicate never arises.
 *
 * On Apple ld this file compiles to an empty test binary (HAVE_LINKER_WRAP is
 * not defined).
 */

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "wiredtiger.h"
#include "fff.h"

DEFINE_FFF_GLOBALS;

#ifdef HAVE_LINKER_WRAP

extern "C" {
/*
 * Defining __wrap_wiredtiger_pack_start is sufficient: the linker has already
 * replaced every call to wiredtiger_pack_start (including the one inside
 * wiredtiger_unpack_start) with a call to this symbol.
 */
FAKE_VALUE_FUNC(int, __wrap_wiredtiger_pack_start,
  WT_SESSION *, const char *, void *, size_t, WT_PACK_STREAM **);
}

TEST_CASE(
  "wiredtiger_unpack_start calls wiredtiger_pack_start (same-TU intercept via --wrap)",
  "[wrap_test]")
{
    __wrap_wiredtiger_pack_start_fake.return_val = 0;

    uint8_t buf[16] = {};
    WT_PACK_STREAM *ps = nullptr;

    int ret = wiredtiger_unpack_start(nullptr, "Q", buf, sizeof(buf), &ps);

    REQUIRE(ret == 0);
    REQUIRE(__wrap_wiredtiger_pack_start_fake.call_count == 1);
}

#endif /* HAVE_LINKER_WRAP */
