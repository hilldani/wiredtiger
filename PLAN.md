# Plan: Catch2 + FFF tests for cur_layered.c

## Context

`cur_layered.c` implements WiredTiger's layered cursor (disaggregated storage). It merges reads from an *ingest* constituent (ephemeral, follower-written) and a *stable* constituent (checkpoint-based). The file has ~3000 lines and many static helpers with non-trivial branching logic—leader vs. follower path, truncate conflict detection, positioned vs. unpositioned state—that are hard to exercise through real-database integration tests. The goal is to demonstrate that Catch2 + FFF lets us test these helpers cheaply: mock the expensive callees, set return values, assert call counts, write BDD scenarios that read as spec.

## What to expose

Three static helpers deserve to be non-static because their logic is self-contained and worth testing directly:

| Function | Remove `static` from | Interesting callees |
|---|---|---|
| `__clayered_lookup_constituent` | `src/cursor/cur_layered.c:1616` | cursor vtable: `set_key`, `search`, `get_value` |
| `__clayered_put` | `src/cursor/cur_layered.c:2049` | FFF target: `__wt_layered_table_truncate_detect_write_conflict`; cursor vtable: `set_key`, `set_value`, `insert`, `update` |
| `__clayered_remove_leader` | `src/cursor/cur_layered.c:2159` | cursor vtable: `set_key`, `remove` |
| `__clayered_reset_cursors` | `src/cursor/cur_layered.c:1338` | cursor vtable: `reset` ×2; flag macros `F_CLR`, `F_ISSET` |
| `__clayered_lookup` | `src/cursor/cur_layered.c:1636` | `__clayered_lookup_constituent` ×2, `__wt_clayered_deleted`, `__wt_truncate_delete_visible_check`, `__clayered_reset_cursors` |

Also move `WT_CLAYERED_PUT_OP` enum (currently defined locally in `cur_layered.c` around line 18) to `src/include/cursor.h`, just before the `WT_CURSOR_LAYERED` struct at line 523. This makes the enum available in the test's `extern "C"` declarations without re-defining values.

These three functions have no other changes—keeping the `__clayered_` name is intentional (internal, not a public API).

## New files

### `test/catch2/cursors/cur_layered/CMakeLists.txt`

Separate executable (like `my_fff_test`) so FFF globals don't bleed into `catch2-unittests`:

```cmake
create_test_executable(catch2-cur-layered-tests
    SOURCES
        test_cur_layered.cpp
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/test/catch2
    FLAGS
        ${COMPILER_DIAGNOSTIC_CXX_FLAGS}
    LIBS
        Catch2::Catch2
        fff::fff
    CXX
)
add_test(NAME cur_layered_fff_test COMMAND $<TARGET_FILE:catch2-cur-layered-tests>)
set_tests_properties(cur_layered_fff_test PROPERTIES LABELS "check;unittest")
```

### `test/catch2/cursors/cur_layered/test_cur_layered.cpp`

Full BDD test file. Structure:

```
#include "wt_internal.h"
#include "fff.h"
#include "wrappers/mock_session.h"
DEFINE_FFF_GLOBALS;

// FFF fake — intercepts __wt_layered_table_truncate_detect_write_conflict
// before the archive provides the real one
extern "C" {
FAKE_VALUE_FUNC(int, __wt_layered_table_truncate_detect_write_conflict,
    WT_SESSION_IMPL *, WT_LAYERED_TABLE *, const WT_ITEM *);
}

// Forward-declare the now-non-static helpers
extern "C" {
int __clayered_lookup_constituent(WT_CURSOR *, WT_CURSOR_LAYERED *, WT_ITEM *);
int __clayered_put(WT_SESSION_IMPL *, WT_CURSOR_LAYERED *,
                   const WT_ITEM *, const WT_ITEM *, WT_CLAYERED_PUT_OP);
int __clayered_remove_leader(WT_SESSION_IMPL *, WT_CURSOR_LAYERED *,
                              const WT_ITEM *, bool);
}
```

#### Fixture: `layered_cursor_fixture`

Builds a minimal `WT_CURSOR_LAYERED` with two mock constituent cursors; no real btree, no disk. Sets `cursor->session` so `CUR2S()` works. Sets `conn->layered_table_manager.leader` to configure leader/follower. Resets all FFF fakes and call counters in the constructor so scenarios are isolated.

Cursor vtable methods are mocked with plain C function pointers (FFF doesn't support variadic, so `set_key`/`set_value`/`get_value` use stubs that increment counters stored in a per-cursor struct). Non-variadic methods (`search`, `insert`, `update`, `remove`, `reset`) have their return values configurable per-test.

#### Scenarios

**Group 1 — tombstone detection** `[layered_cursor][tombstone]`
Tests `__wt_clayered_deleted` from `cursor_inline.h` (already public, no FFF needed—used as a warm-up/baseline):
- Empty item → not deleted
- Exact tombstone bytes → deleted
- Tombstone prefix + more bytes → not deleted (this is an *encoded* value, different path)

**Group 2 — `__clayered_lookup_constituent`** `[layered_cursor][lookup]`
- GIVEN a constituent cursor whose `search` returns 0 / WHEN lookup_constituent is called / THEN returns 0, `current_cursor` equals the constituent, `get_value` called once
- GIVEN `search` returns `WT_NOTFOUND` / WHEN called / THEN returns `WT_NOTFOUND`, `current_cursor` unchanged
- GIVEN any constituent / WHEN called / THEN `set_key` called exactly once before `search`

**Group 3 — `__clayered_put` (FFF showcase)** `[layered_cursor][put]`

These are the FFF money shots—show `_fake.call_count`, `_fake.return_val`:

- GIVEN a **follower** / WHEN put (INSERT) is called / THEN `__wt_layered_table_truncate_detect_write_conflict` is called exactly once
- GIVEN a follower / AND `__wt_layered_table_truncate_detect_write_conflict` returns `WT_ROLLBACK` / WHEN put is called / THEN returns `WT_ROLLBACK` (error propagation)
- GIVEN a follower / WHEN put (INSERT) is called / THEN ingest cursor's `insert` is called, stable cursor's `insert` is NOT
- GIVEN a **leader** / WHEN put (INSERT) is called / THEN `__wt_layered_table_truncate_detect_write_conflict` call count is 0 (leader skips conflict check)
- GIVEN a leader / WHEN put (INSERT) is called / THEN stable cursor's `insert` is called, ingest cursor's `insert` is NOT
- GIVEN a follower / WHEN put (UPDATE) is called / THEN `current_cursor` is set to the ingest cursor after success

**Group 4 — `__clayered_remove_leader`** `[layered_cursor][remove]`
- GIVEN cursor is NOT positioned (no `WT_CURSTD_KEY_INT` on stable) / WHEN remove_leader is called / THEN `set_key` called once, `remove` called once
- GIVEN cursor IS positioned (`WT_CURSTD_KEY_INT` on stable) / WHEN remove_leader is called / THEN `set_key` NOT called, `remove` called once
- GIVEN any state / WHEN remove_leader succeeds / THEN `current_cursor` is set to the stable cursor

## Files to modify

| File | Change |
|---|---|
| `src/cursor/cur_layered.c` | Remove `static` from `__clayered_lookup_constituent` (line 1616), `__clayered_put` (line 2049), `__clayered_remove_leader` (line 2159) |
| `src/include/cursor.h` | Move `WT_CLAYERED_PUT_OP` enum here (before `WT_CURSOR_LAYERED` at line 523) |
| `test/catch2/CMakeLists.txt` | Add `add_subdirectory(cursors/cur_layered)` |

## Files to create

| File | Purpose |
|---|---|
| `test/catch2/cursors/cur_layered/CMakeLists.txt` | Separate test executable `catch2-cur-layered-tests` |
| `test/catch2/cursors/cur_layered/test_cur_layered.cpp` | ~300-line BDD test file with all scenarios above |

## Verification

```bash
cmake -B build -G Ninja -DHAVE_UNITTEST=1
cmake --build build --target catch2-cur-layered-tests
./build/test/catch2/cursors/cur_layered/catch2-cur-layered-tests         # all
./build/test/catch2/cursors/cur_layered/catch2-cur-layered-tests "[put]"  # put group only
```

Expected: all scenarios pass; `[put]` scenarios show FFF fakes intercepting `__wt_layered_table_truncate_detect_write_conflict` rather than the real implementation.
