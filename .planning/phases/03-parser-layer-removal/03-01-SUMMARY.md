---
plan: 03-01
phase: 03-parser-layer-removal
status: complete
completed: 2026-09-04
commits:
  - "601f83f feat(03-01): delete parser registry (PARSE-01), drop LoadFile parse param, guard savers.find UB (D-20)"
  - "4e14301 test(03-01): auto-save chain end-to-end test via file:// save=true (D-19)"
requirements: [PARSE-01, PARSE-03]
---

# Plan 03-01 Summary: Parser Layer Deletion

## What Was Built

**PARSE-01 — dead parser layer deleted:**
- `include/FileParser.hpp` deleted from disk (16-line dead interface; only includer was `FileManager.hpp`)
- `#include "FileParser.hpp"` removed from `include/FileManager.hpp`
- `map<std::string, FileParser *> parsers` member + its doc comment deleted
- `RegisterParser` declaration + 3-line doc block deleted from header; implementation deleted from `src/FileManager.cpp`
- `ParseData` declaration + doc block deleted from header; full implementation (~13 lines incl. the `parsers.find` throw path) deleted from `src/FileManager.cpp`
- `FileManager::LoadFile` dropped `bool parse = false` — now `LoadFile( const std::string &url )`; the `if (parse) ParseData(...)` branch died with it; `suffix` local retained (required out-param of `getURLComponents`)
- Verified: `git grep -nE 'FileParser|ParseData|RegisterParser|parsers\[|parsers\.find' -- include/ src/ test/` → exit 1 (zero matches)

**D-20 — latent UB fixed in auto-save wrapper:**
- `savers.find(savetype)` inside `FileManager::LoadASync`'s `handle_read` lambda was dereferenced unchecked
- Guard added with strict ordering: `m_logger->error` → `DecrementOutstandingOperations( ioc )` → `finalcall( outcome::failure( std::make_error_code( std::errc::operation_not_supported ) ) )` → `return`
- No new Error enum minted (per research "Don't Hand-Roll" table) — `std::error_code` used directly
- `return` is mandatory — prevents the trailing `finalcall(buffers)` double-fire (Pitfall 1); decrement precedes failure delivery so the io_context drains (Pitfall 2)

**D-19 — auto-save chain test (PARSE-03 baseline):**
- `FileManagerIntegrationTest.LoadASync_SaveTrueAutoSavesLoadedDataToDisk` added to `test/src/filemanager_test.cpp` (existing target — no new test target, per D-19)
- First test ever to exercise the full `FileManager::LoadASync(save=true)` → `LocalFileSaver` UUID-write chain
- `CurrentPathGuard` RAII struct (file-local): swaps `std::filesystem::current_path()` via `error_code` overloads (no throwing variants), restores in dtor — survives ASSERT failures; zero CWD pollution confirmed via `git status`
- Completion detected by polling `directory_iterator` for a 36-char/4-dash UUID subdirectory containing the original filename — NOT the finalcall flag (which fires at load completion, before the save lands — Pitfall 6)
- Read-back assertion: `EXPECT_EQ( readBack, "auto-save chain content" )` proves loaded bytes flowed through `SaveASync`
- Written against the CURRENT 6-arg `LoadASync` signature — the parse arg dies in Plan 02 with every other call site

## Deviations

None. All edits followed 03-PATTERNS.md sections 6, 7, and 8 verbatim.

## Self-Check: PASSED

- [x] `Test-Path include/FileParser.hpp` → False
- [x] Parser grep over include/src/test → exit 1
- [x] D-20 guard ordering: log → decrement → failure → return, before the `saverIter->second` deref
- [x] `loader->LoadASync( filePath, parse, save, ioc, handle_read )` and the 4-param `handle_read` lambda untouched (async strip deferred to Plan 02)
- [x] Windows build green (`cmake --build build/Windows/Debug --config Debug`)
- [x] Full ctest green: 9 tests, 0 failures (ran twice — after Task 1 and after Task 2)
- [x] D-19 test direct-run: OK in 62 ms; no UUID dirs in repo/build tree after runs
- [x] `LoadFile` callers (already 1-arg) compile unchanged — suite proves caller-neutrality

## Key Files

### created
- (none — deletion plan; test added to existing file)

### modified
- `include/FileManager.hpp` — parser-free registry: no FileParser include, no parsers map, no RegisterParser/ParseData declarations; `LoadFile(const std::string &url)`
- `src/FileManager.cpp` — RegisterParser/ParseData impls deleted; LoadFile 1-arg; D-20 `saverIter == savers.end()` guard in the LoadASync handle_read lambda
- `test/src/filemanager_test.cpp` — `CurrentPathGuard` RAII struct + `LoadASync_SaveTrueAutoSavesLoadedDataToDisk` test

### deleted
- `include/FileParser.hpp`

## Notes for Plan 03-02

- The async surface still carries `parse`: `FileManager::LoadASync`, the `handle_read` lambda (4-param), `FileLoader.hpp` base virtual, all 5 loader headers, 4 device headers, ~24 posted-failure tails, 5 test files' call sites, and the example call line
- The D-19 test's call site (`false, true, runner.ioc(), ...`) drops its `false` parse arg in Plan 02 along with all other call sites — it is the PARSE-03 behavioral baseline that must stay green
