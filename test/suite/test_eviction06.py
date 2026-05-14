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

import wiredtiger
import wttest
from wiredtiger import stat

# test_eviction06.py
# Verify the per-btree dirty-index ring is exercised end-to-end.
#
# The ring collects dirty WT_REF pointers on every leaf-page modify so the
# eviction walker can drain them without walking the B-tree.  Two stat groups
# are checked:
#
#   Produce side  cache_eviction_dirty_index_insert  -- ref entered the ring
#   Consume side  cache_eviction_dirty_index_drain_scanned -- ring slots examined
#                 cache_eviction_dirty_index_drain_queued     -- ref pushed to LRU queue
#                 cache_eviction_dirty_index_drain_stale   -- ref skipped (not evictable)
@wttest.skip_for_hook("disagg", "Disagg followers use a different eviction model.")
class test_eviction06(wttest.WiredTigerTestCase):
    # Small cache + low dirty trigger so eviction kicks in well before all data
    # is written, guaranteeing the drain path runs during the test.
    conn_config = 'cache_size=20MB,statistics=(all),eviction_dirty_target=2,eviction_dirty_trigger=5'

    nrows = 20000       # ~30 MB at 1500 B/value, well above the 20 MB cache
    value_size = 1500
    batch_size = 50     # rows per transaction; small to keep the txn from
                        # becoming oldest-pinned under app-eviction pressure

    def get_stat(self, stat_key):
        stat_cursor = self.session.open_cursor('statistics:')
        val = stat_cursor[stat_key][2]
        stat_cursor.close()
        return val

    def _write_batch(self, cursor, batch_start, batch_end, value):
        # App-eviction may roll the writer back if it becomes the oldest pinned
        # txn while the cache is over the dirty trigger; retry the batch.
        while True:
            self.session.begin_transaction()
            try:
                for i in range(batch_start, batch_end):
                    cursor[i] = value
                self.session.commit_transaction()
                return
            except wiredtiger.WiredTigerError as e:
                self.session.rollback_transaction()
                if 'WT_ROLLBACK' not in str(e):
                    raise

    def _write_rows(self, uri, start, count, value):
        cursor = self.session.open_cursor(uri)
        for batch_start in range(start, start + count, self.batch_size):
            self._write_batch(cursor,
                              batch_start,
                              min(batch_start + self.batch_size, start + count),
                              value)
        cursor.close()

    def test_dirty_index_insert_and_drain(self):
        # Verify both the produce side (insert) and the consume side (scanned)
        # of the dirty-index ring are active when the cache fills with dirty data.
        uri = 'table:test_eviction06'
        self.session.create(uri, 'key_format=i,value_format=S,leaf_page_max=4KB')

        self._write_rows(uri, 0, self.nrows, 'x' * self.value_size)

        self.assertGreater(self.get_stat(stat.conn.cache_eviction_dirty_index_insert), 0)
        self.assertGreater(self.get_stat(stat.conn.cache_eviction_dirty_index_drain_scanned), 0)
        # Every non-NULL slot drained results in a hit, a stale skip, or a skip because the
        # page could not yet be evicted (active transaction / not yet globally visible).
        self.assertGreater(
            self.get_stat(stat.conn.cache_eviction_dirty_index_drain_queued) +
            self.get_stat(stat.conn.cache_eviction_dirty_index_drain_stale) +
            self.get_stat(stat.conn.cache_eviction_dirty_index_drain_filtered), 0)

    def test_dirty_index_reinsertion_after_drain(self):
        # After the drain clears dirty_index_slot on a ref, a subsequent write to
        # the same page must be able to re-insert the ref into the ring.  Verify
        # this by checking that the insert counter grows across two write rounds
        # separated by a checkpoint (which triggers reconciliation and eviction).
        uri = 'table:test_eviction06_reinsert'
        self.session.create(uri, 'key_format=i,value_format=S,leaf_page_max=4KB')

        self._write_rows(uri, 0, self.nrows, 'a' * self.value_size)
        inserts_after_round1 = self.get_stat(stat.conn.cache_eviction_dirty_index_insert)
        self.assertGreater(inserts_after_round1, 0)

        # Checkpoint reconciles dirty pages and triggers additional eviction passes
        # that clear dirty_index_slot on drained refs.
        self.session.checkpoint()

        self._write_rows(uri, 0, self.nrows, 'b' * self.value_size)
        inserts_after_round2 = self.get_stat(stat.conn.cache_eviction_dirty_index_insert)

        # Round 2 overwrites the same pages, which can only contribute new inserts
        # if dirty_index_slot was cleared between rounds (drain worked correctly).
        self.assertGreater(inserts_after_round2, inserts_after_round1)

if __name__ == '__main__':
    wttest.run()
