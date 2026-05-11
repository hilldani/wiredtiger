/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * fff link-seam for WT-17383.
 *
 * This test exercises reconciliation logic directly via __wti_rec_upd_select and
 * does not need to replace any libwiredtiger symbols.  The fakes file is included
 * as a structural requirement of the regression test pattern.
 *
 * If deeper injection were needed (e.g. forcing __txn_visible_id to return a
 * specific value) you would add:
 *
 *   DEFINE_FFF_GLOBALS;
 *   FAKE_VALUE_FUNC(bool, __txn_visible_id, WT_SESSION_IMPL *, uint64_t);
 *
 * and use --wrap on Linux or a __ut_ injection wrapper on all platforms.
 */

#include "../fff.h"

DEFINE_FFF_GLOBALS;
