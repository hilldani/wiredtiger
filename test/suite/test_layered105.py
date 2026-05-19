#!/usr/bin/env python3
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

# test_layered105.py
#   Regression test for the assertion at src/support/modify.c:486
#   ("cbt->slot != UINT32_MAX") seen during test/format with CONFIG.disagg
#   disagg.multi=1 (core dump_t.4591.core).
#
#   Bug shape (before the fix):
#     An ingest-btree row K has on-disk value V1 and an in-memory chain
#         [ STANDARD(V2) -> MODIFY(D1) -> NULL ]
#     layered on top, with the MODIFY's reconstruction base being the
#     on-disk V1. Ingest GC eligibility advances past V1 while the
#     in-memory entries are still not globally visible. Eviction
#     reconciliation drops K from the rebuilt in-memory disk image (in
#     rec_visibility.c upd_select->upd is cleared via the
#     WT_REC_HAS_ON_DISK + !found_last_upd_to_keep + !first_pruned_update
#     path, then rec_row.c's GARBAGE_COLLECT path writes the tombstone
#     and skips the cell). The saved chain survives via supd_restore but
#     lands on the insert list of the rebuilt page (K no longer has a
#     row, cbt->ins != NULL, cbt->slot == UINT32_MAX). A reader whose
#     read_timestamp can see the MODIFY but not the newer STANDARD walks
#     modify->next == NULL with no on-page fallback and aborts the
#     process at the assertion.
#
#   With the bug present this test aborts during step 8 (the
#   read_timestamp=25 iteration) with:
#       __wt_modify_reconstruct_from_upd_list, 486: WiredTiger assertion
#       failed: 'cbt->slot != (4294967295U)'
#   After the fix it should return V1 with D1 applied.

import wiredtiger, wttest
from helper_disagg import disagg_test_class, gen_disagg_storages
from wtscenario import make_scenarios
from wiredtiger import stat

@disagg_test_class
class test_layered105(wttest.WiredTigerTestCase):
    base_config = 'statistics=(all),precise_checkpoint=true,'
    conn_config = base_config + 'disaggregated=(role="leader")'
    conn_config_follower = base_config + 'disaggregated=(role="follower")'

    uri = 'layered:test_layered105'
    ingest_uri = 'file:test_layered105.wt_ingest'
    create_config = 'key_format=i,value_format=S'

    disagg_storages = gen_disagg_storages('test_layered105', disagg_only=True)
    scenarios = make_scenarios(disagg_storages)

    conn_follow = None
    session_follow = None

    def create_follower(self):
        self.conn_follow = self.wiredtiger_open(
            'follower',
            self.extensionsConfig() + ',create,' + self.conn_config_follower)
        self.session_follow = self.conn_follow.open_session()

    def force_evict(self, conn, uri, key):
        """Force-evict the leaf page that holds `key`."""
        session_evict = conn.open_session('debug=(release_evict_page)')
        evict_cursor = session_evict.open_cursor(uri)
        evict_cursor.set_key(key)
        evict_cursor.search()
        evict_cursor.reset()
        evict_cursor.close()
        session_evict.close()

    def test_modify_survives_ingest_gc_of_base_value(self):
        """
        Timeline:
          ts=10  insert K = V1 on leader and follower
          force-evict follower's ingest page  -> K is on its in-memory
                                                 disk image
          stable_timestamp = 11               -> locked before the higher
                                                 writes so the upcoming
                                                 checkpoint runs at this
                                                 timestamp
          ts=20  modify K (D1) on both sides   -> ingest chain:
                                                 [MODIFY(D1) -> NULL]
                                                 with base = on-disk V1
          ts=30  update K = V2  on both sides  -> ingest chain:
                                                 [STANDARD(V2)
                                                  -> MODIFY(D1) -> NULL]
          checkpoint on leader; advance on follower
                                              -> follower prune_timestamp
                                                 = 11. D1 (ts=20) and V2
                                                 (ts=30) are NOT pruneable;
                                                 on-disk V1 (ts=10) IS
                                                 GC-eligible.
          oldest_timestamp = 11 on follower    -> V1 is visible_all; D1
                                                 and V2 are not.
          force-evict follower's ingest page   -> reconciliation drops K
                                                 from the rebuilt in-memory
                                                 disk image; the saved
                                                 chain lands on the
                                                 insert list of the new
                                                 page.
          begin_transaction(read_timestamp=25) -> sees D1 but not V2.
          iterate ingest cursor                -> reconstructs D1 against
                                                 V1; before the fix this
                                                 aborts at modify.c:486.
        """
        self.create_follower()

        self.session.create(self.uri, self.create_config)
        self.session_follow.create(self.uri, self.create_config)

        # __wt_btcur_modify auto-promotes a MODIFY to a STANDARD when the
        # post-modify value fits in 64 bytes or fewer (see
        # __cursor_chain_needs_full_upd in bt_cursor.c). To put an actual
        # MODIFY on the chain we need values larger than 64 bytes.
        v1 = 'value1' + ('.' * 100)
        v2 = 'value3' + ('.' * 100)

        # Step 1: insert K=1 at ts=10 on both sides.
        c = self.session.open_cursor(self.uri)
        self.session.begin_transaction()
        c[1] = v1
        self.session.commit_transaction(
            f'commit_timestamp={self.timestamp_str(10)}')
        c.close()

        cf = self.session_follow.open_cursor(self.uri)
        self.session_follow.begin_transaction()
        cf[1] = v1
        self.session_follow.commit_transaction(
            f'commit_timestamp={self.timestamp_str(10)}')
        cf.close()

        # Step 2: bake V1 onto the follower's ingest disk image.
        self.force_evict(self.conn_follow, self.uri, 1)

        # Step 2b: lock stable_timestamp at 11 before the higher-timestamp
        # writes. stable_timestamp can only advance and is bounded by the
        # commit timestamps of already-committed transactions, so this
        # has to happen before the ts=20 / ts=30 writes.
        self.conn.set_timestamp(
            f'stable_timestamp={self.timestamp_str(11)}')
        self.conn_follow.set_timestamp(
            f'stable_timestamp={self.timestamp_str(11)}')

        # Step 3: modify K at ts=20 on both sides. v1[6] == '.', so the
        # post-modify value is 'value1' + 'X' + ('.' * 99). Because v1 is
        # > 64 bytes, this MODIFY is preserved as a MODIFY update on the
        # ingest chain (not collapsed to a STANDARD).
        mods = [wiredtiger.Modify('X', 6, 1)]

        c = self.session.open_cursor(self.uri)
        self.session.begin_transaction()
        c.set_key(1)
        self.assertEqual(c.modify(mods), 0)
        self.session.commit_transaction(
            f'commit_timestamp={self.timestamp_str(20)}')
        c.close()

        cf = self.session_follow.open_cursor(self.uri)
        self.session_follow.begin_transaction()
        cf.set_key(1)
        self.assertEqual(cf.modify(mods), 0)
        self.session_follow.commit_transaction(
            f'commit_timestamp={self.timestamp_str(20)}')
        cf.close()

        # Step 4: full update at ts=30 on both sides. Ingest chain becomes
        # [STANDARD(V2) -> MODIFY(D1) -> NULL].
        c = self.session.open_cursor(self.uri)
        self.session.begin_transaction()
        c[1] = v2
        self.session.commit_transaction(
            f'commit_timestamp={self.timestamp_str(30)}')
        c.close()

        cf = self.session_follow.open_cursor(self.uri)
        self.session_follow.begin_transaction()
        cf[1] = v2
        self.session_follow.commit_transaction(
            f'commit_timestamp={self.timestamp_str(30)}')
        cf.close()

        # Step 5: checkpoint while stable is still 11. With last_ckpt == 1
        # and no other session using the layered dhandle, the follower's
        # prune update sets prune_timestamp = checkpoint_timestamp = 11.
        self.session.checkpoint()
        self.disagg_advance_checkpoint(self.conn_follow)

        # Step 6: oldest_timestamp = 11. V1@10 <= 11 -> visible_all;
        # D1@20 and V2@30 > 11 -> not visible_all.
        self.conn_follow.set_timestamp(
            f'oldest_timestamp={self.timestamp_str(11)}')

        # Step 7: force eviction. Reconciliation walks the ingest chain
        # for K. Without the fix it clears upd_select->upd (the on-page
        # value is the only globally visible base, the in-memory entries
        # are not yet visible_all), then rec_row.c's GARBAGE_COLLECT path
        # drops K from the rebuilt in-memory disk image, leaving the
        # chain stranded on the insert list of the new page. With the
        # fix the on-page value is preserved because a MODIFY in the
        # chain depends on it.
        self.force_evict(self.conn_follow, self.uri, 1)

        # Step 8: read at a timestamp that sees the MODIFY but not the
        # newer STANDARD. Iterate so the cursor walks the insert list (the
        # same path the production core dump shows). Before the fix this
        # aborts at modify.c:486 with
        #     'cbt->slot != (4294967295U)'. After the fix the MODIFY is
        # reconstructed against V1 and returns the expected value.
        expected = 'value1' + 'X' + ('.' * 99)

        self.session_follow.begin_transaction(
            f'read_timestamp={self.timestamp_str(25)}')
        cf = self.session_follow.open_cursor(self.ingest_uri)
        # cursor.next walks the insert list; this is the call that aborts
        # the process with the bug present.
        self.assertEqual(cf.next(), 0)
        self.assertEqual(cf.get_key(), 1)
        self.assertEqual(cf.get_value(), expected)
        # No more entries — the ingest btree only has K=1.
        self.assertEqual(cf.next(), wiredtiger.WT_NOTFOUND)
        cf.close()
        self.session_follow.rollback_transaction()

        # Drop the follower before tearDown to avoid a verifyLayered
        # contention on a follower that still has the ingest btree open.
        self.session_follow.close()
        self.conn_follow.close()
        self.session_follow = None
        self.conn_follow = None

        # The stable timestamp was pinned at 11 to control prune_timestamp.
        # That leaves the leader with uncheckpointed dirty data (the ts=20
        # and ts=30 writes), which verifyLayered() in tearDown can't handle.
        # Advance stable and take a final checkpoint to flush everything.
        self.conn.set_timestamp(
            f'stable_timestamp={self.timestamp_str(100)}')
        self.session.checkpoint()
