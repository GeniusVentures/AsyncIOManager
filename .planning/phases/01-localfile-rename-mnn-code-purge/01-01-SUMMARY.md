---
plan: 01
status: completed
started: 2026-09-03
completed: 2026-09-03
duration_minutes: 45
---

# Plan 01-01 Summary: LocalFile Rename & MNN Code Purge

## What Was Done

Executed the Phase 1 vertical slice as a red→green transaction:

1. **RED (Task 1)**: Planted `FileManagerIntegrationTest.SaveASync_MnnPrefixThrows` in `test/src/filemanager_test.cpp` — mirrors `SaveASync_UnregisteredPrefixThrows` with the `mnn://` prefix. Confirmed failing in isolation ("Actual: it throws nothing") because MNNSaver still registered `"mnn"`. All 8 other tests green. Commit `456c585`.
2. **GREEN (Task 2)**: The full rename transaction — commit `1955711`:
   - `git mv` 8 files (MNNLoader/MNNSaver/FILECommon pairs + 2 test files → LocalFile* names)
   - `git rm` `include/MNNCommon.hpp` + `test/base_mnn_test.hpp` (verified zero live includers)
   - Renamed classes: `sgns::LocalFileLoader`, `sgns::LocalFileSaver`, `sgns::LocalFileDevice` (both `#ifndef _WIN32` branches — D-01)
   - Error categories, singleton boilerplate, logger tags (`"LocalFileLoader"`/`"LocalFileSaver"`/`"LocalFileCommon"` ×2 — D-03) renamed
   - **MNN-04 behavioral delta**: `RegisterSaver( "mnn", this )` line deleted — LocalFileSaver registers only `"file"`
   - `src/FileManager.cpp`: includes swapped; `InitializeSingletons()` rewritten (LocalFileLoader/LocalFileSaver; `MNNParser` comment deleted; SFTPLoader/WSLoader comments kept); dead `if ( parse ) { parsers.find("mnn") ... }` block deleted (D-04) while parse machinery (parsers map, RegisterParser, ParseData, parse params) survives intact (D-05)
   - `src/CMakeLists.txt`: 3 source-list swaps; `test/src/CMakeLists.txt`: 3-part test renames (source + addtest + target_link_libraries) + MNNExample comment scrub
   - Test renames: `LocalFileLoaderTest` (4 tests), `LocalFileSaverTest` (5 tests), filemanager dispatch renames ×4 (TEST-01/TEST-02)

## Requirements Delivered

- **FILE-01**: LocalFileLoader strategy singleton, sole `"file"` load handler
- **FILE-02**: LocalFileSaver strategy singleton
- **FILE-03**: FileManager dispatches to LocalFile* (verified by renamed dispatch tests)
- **FILE-04**: LocalFileDevice (both platform branches renamed in-place, unsplit per D-02)
- **MNN-02**: All 6 MNN code files deleted from disk
- **MNN-04**: `"mnn"` save registration removed — rejection test green
- **MNN-05** (in-file portion): src/, test sources, test CMake comment scrubbed; header doc-scrub is Plan 01-02
- **TEST-01**: loader/saver suites renamed, same behaviors (4+5 tests pass)
- **TEST-02**: filemanager dispatch tests renamed (9 tests pass)

## Verification Evidence

- **RED**: `filemanager_test.exe --gtest_filter=FileManagerIntegrationTest.SaveASync_MnnPrefixThrows` → FAILED (throws nothing), exit 1
- **GREEN**: all three suites exit 0 — localfile_loader_test 4/4, localfile_saver_test 5/5, filemanager_test 9/9 (rejection test included)
- **Deletions**: `Test-Path` on all 6 MNN files → False
- **Rename completeness**: `git grep FILEDevice -- include src` → empty; `LocalFileDevice` present in both platform branches of LocalFileCommon
- **MNN-04**: exactly 1 `RegisterSaver` in src/LocalFileSaver.cpp; `git grep '"mnn"' -- src` → empty
- **Scoped grep**: `git grep -iI mnn -- src test/src ":(exclude)test/src/filemanager_test.cpp"` → empty (exit 1)
- **D-05 boundary**: `parsers` map/RegisterParser/ParseData intact; hardcoded `"mnn"` lookup gone

## Deviations

None. All anti-pattern quirks copied verbatim per plan (EOF `error.value() == 2` idiom, dead ofstream line, string-concat logger calls, unqualified range_error in saver).

## Key Learnings

- The wrapper tree (`build/Windows/Debug`) regenerates cleanly after source renames — one reconfigure + target build sufficed; no stale-target issues
- MSVC reported exit code 1 on a successful build line in this terminal (toolchain noise); the .exe outputs and test exits are the real signals
- The RED-state rejection test writes a stray `some/` dir in cwd (predicted by plan) — removed before GREEN verification

## Files Changed

- Renamed (8): MNNLoader.hpp/.cpp, MNNSaver.hpp/.cpp, FILECommon.hpp/.cpp, mnn_loader_test.cpp, mnn_saver_test.cpp → LocalFile*/localfile_* equivalents
- Deleted (2): include/MNNCommon.hpp, test/base_mnn_test.hpp
- Modified (4): src/FileManager.cpp, src/CMakeLists.txt, test/src/CMakeLists.txt, test/src/filemanager_test.cpp
