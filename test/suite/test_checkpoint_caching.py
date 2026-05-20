#!/usr/bin/env python
#
# Public Domain 2014-present MongoDB, Inc.
# Public Domain 2008-2014 WiredTiger, Inc.
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

# test_checkpoint_caching.py
# End-to-end tests for the cross-checkpoint shared disk cache (WT-17060).
# Exercises put, get (hit), and release through the real WiredTiger API.

import wiredtiger, wttest
from helper_disagg import disagg_test_class, gen_disagg_storages
from wtscenario import make_scenarios

@disagg_test_class
class test_checkpoint_caching(wttest.WiredTigerTestCase):

    conn_base_config = ',create,statistics=(all),statistics_log=(wait=1,json=true,on_close=true),'

    def conn_config(self):
        return self.extensionsConfig() + self.conn_base_config + 'disaggregated=(role="leader")'

    uri = 'layered:test_checkpoint_caching'
    # Enough rows to span several leaf pages so multiple put/get/release calls are exercised.
    nrows = 1000

    disagg_storages = gen_disagg_storages('test_checkpoint_caching', disagg_only=True)
    scenarios = make_scenarios(disagg_storages)

    def setUp(self):
        super().setUp()
        # Follower reads all page data from the shared page log, exercising the cache on every read.
        self.conn_follow = self.wiredtiger_open(
            'follower',
            self.extensionsConfig() + self.conn_base_config + 'disaggregated=(role="follower")')
        self.session_follow = self.conn_follow.open_session('')
        table_cfg = 'key_format=S,value_format=S'
        self.session.create(self.uri, table_cfg)
        self.session_follow.create(self.uri, table_cfg)

    def tearDown(self):
        if hasattr(self, 'conn_follow'):
            self.session_follow.close()
            self.conn_follow.close()
        super().tearDown()

    def populate(self):
        cursor = self.session.open_cursor(self.uri)
        for i in range(self.nrows):
            cursor[str(i).zfill(10)] = 'value_' + str(i)
        cursor.close()
        self.session.checkpoint()

    def scan_all(self, session):
        cursor = session.open_cursor(self.uri)
        while cursor.next() == 0:
            pass
        return cursor

    def get_stat(self, stat_key, session):
        stat_cursor = session.open_cursor('statistics:')
        val = stat_cursor[stat_key][2]
        stat_cursor.close()
        return val

    def test_put_and_miss(self):
        # A fresh follower has an empty cache: every page read is a miss, then put into cache.
        self.populate()
        self.disagg_advance_checkpoint(self.conn_follow)

        cursor = self.scan_all(self.session_follow)
        cursor.close()

        miss = self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_miss, self.session_follow)
        hit  = self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_hit,  self.session_follow)
        self.assertGreater(miss, 0)
        self.assertEqual(hit, 0)

    def test_get_hit_across_checkpoints(self):
        # Scan at checkpoint N — all misses, pages enter cache.
        self.populate()
        self.disagg_advance_checkpoint(self.conn_follow)
        cursor_ckpt_n = self.scan_all(self.session_follow)
        self.assertGreater(
            self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_miss, self.session_follow), 0)
        self.assertEqual(
            self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_hit, self.session_follow), 0)

        # Leader makes checkpoint N+1 without modifying data; block addresses are unchanged.
        self.session.checkpoint()

        # Follower advances to N+1 and reads again. The new stable dhandle has fresh WT_REFs in
        # WT_REF_DISK state; reading them finds the same block addresses already in cache  hits.
        self.disagg_advance_checkpoint(self.conn_follow)
        cursor_ckpt_n1 = self.scan_all(self.session_follow)
        cursor_ckpt_n1.close()
        self.assertGreater(
            self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_hit, self.session_follow), 0)

        # Close the checkpoint-N cursor, exercising release().
        cursor_ckpt_n.close()

    def test_release(self):
        # Scan populates the cache, then closing the cursor triggers release() per page.
        self.populate()
        self.disagg_advance_checkpoint(self.conn_follow)
        cursor = self.scan_all(self.session_follow)
        self.assertGreater(
            self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_miss, self.session_follow), 0)
        cursor.close()

        # Reopen the follower to get a guaranteed-empty cache, then read again.
        # If release() leaked any entries, the second scan would produce hits instead of misses.
        self.session_follow.close()
        self.conn_follow.close()
        self.session.checkpoint()

        self.conn_follow = self.wiredtiger_open(
            'follower',
            self.extensionsConfig() + self.conn_base_config + 'disaggregated=(role="follower")')
        self.session_follow = self.conn_follow.open_session('')
        self.session_follow.create(self.uri, 'key_format=S,value_format=S')
        self.disagg_advance_checkpoint(self.conn_follow)

        cursor = self.scan_all(self.session_follow)
        cursor.close()
        self.assertGreater(
            self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_miss, self.session_follow), 0)
        self.assertEqual(
            self.get_stat(wiredtiger.stat.conn.cache_shared_dsk_hit, self.session_follow), 0)
