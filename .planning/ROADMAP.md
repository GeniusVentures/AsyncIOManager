---
created: 2026-09-03
milestone: AsyncIOManager Modernization
granularity: standard
total_phases: 5
---

# Roadmap: AsyncIOManager Modernization

**Core Value:** Reliable async load/save of data across local and remote protocols behind one URL-dispatched `FileManager` API.

**Milestone outcome:** The library is a pure async I/O library — zero MNN, platform-split local-file layer, no dead parser layer, generic example, fully green Windows test suite.

**Vertical-slice rule:** every phase ends with the repo building and the test suite passing on Windows (`BUILD_TESTING=ON`).

## Phases

- [ ] **Phase 1: LocalFile Rename & MNN Code Purge** - `MNNLoader`/`MNNSaver`/`FILECommon` become `LocalFileLoader`/`LocalFileSaver`/`LocalFileCommon`, MNN code files deleted, `mnn://` prefix removed, tests renamed in-phase
- [x] **Phase 2: LocalFileCommon Platform Split** - Local-file device split into Windows/POSIX file pairs selected by CMake, zero `#ifdef` (completed 2026-09-04)
- [x] **Phase 3: Parser Layer Removal** - `parse` bool stripped from every API and callback; `FileParser` deleted; `save`-driven auto-save preserved (completed 2026-09-04)
- [ ] **Phase 4: Build Purge & Generic Example** - MNN removed from all CMake; `MNNExample` replaced by generic `file://` example; `.mnn` fixtures become generic binaries
- [ ] **Phase 5: Zero-MNN Verification & Green Suite** - Acceptance gate: zero-MNN grep, full suite green on Windows, clean-tree end-to-end build

## Phase Details

### Phase 1: LocalFile Rename & MNN Code Purge

**Goal:** Local file load/save is delivered end-to-end by `LocalFileLoader`/`LocalFileSaver` registering for `file://` only, with every MNN code file deleted, doc comments scrubbed, and the renamed test suite passing on Windows
**Mode:** mvp
**Depends on:** Nothing (first phase)
**Requirements:** FILE-01, FILE-02, FILE-03, FILE-04, MNN-02, MNN-04, MNN-05, TEST-01, TEST-02
**Success Criteria** (what must be TRUE):

  1. `FileManager::LoadASync("file://...")` dispatches to `LocalFileLoader` and `SaveASync("file://...")` dispatches to `LocalFileSaver`; a save to `mnn://...` throws "No saver registered for prefix mnn" (verified by `localfile_saver_test` / `filemanager_test`)
  2. `include/MNNCommon.hpp`, `include/MNNLoader.hpp`, `include/MNNSaver.hpp`, `src/MNNLoader.cpp`, `src/MNNSaver.cpp`, `test/base_mnn_test.hpp` no longer exist on disk
  3. `localfile_loader_test` and `localfile_saver_test` targets exist (covering sync load, async load, nonexistent-file error, sync save, null-data error, async save) and pass; `filemanager_test` expects `LocalFileLoader`/`LocalFileSaver` dispatch and passes
  4. Windows build with `BUILD_TESTING=ON` configures, compiles, and `ctest` is green
  5. `grep -ri mnn include/ src/ test/` returns no matches (doc comments scrubbed; root `CMakeLists.txt` MNN purge deferred to Phase 4 by design)

**Plans:** 2 plans

Plans:
**Wave 1**

- [ ] 01-01-PLAN.md — Rename transaction: LocalFile triad + MNN code purge + mnn:// rejection test (red→green vertical slice)

**Wave 2** *(blocked on Wave 1 completion)*

- [ ] 01-02-PLAN.md — MNN doc-comment scrub of surviving headers + full Phase-1 verification gate (zero-grep + ctest)

### Phase 2: LocalFileCommon Platform Split

**Goal:** The local-file device layer exists as separate Windows and POSIX source+header pairs selected by CMake, with zero platform branching inside any local-file source file
**Mode:** mvp
**Depends on:** Phase 1 (splits the renamed `LocalFileCommon`)
**Requirements:** PLAT-01, PLAT-02, PLAT-03, PLAT-04
**Success Criteria** (what must be TRUE):

  1. `LocalFileCommon.win.cpp`/`LocalFileCommon.win.hpp` exist and contain zero `#ifdef`/`#ifndef _WIN32`
  2. `LocalFileCommon.posix.cpp`/`LocalFileCommon.posix.hpp` exist, are structurally valid POSIX implementations (compile-correct on paper; no Linux CI in this environment), and contain zero `#ifdef`
  3. `src/CMakeLists.txt` selects the platform pair via `if(WIN32)/elseif(UNIX)` — no other mechanism
  4. Any shared declarations live in a platform-neutral header with no `#ifdef _WIN32`
  5. Windows build + full test suite remain green after the split

**Plans:** 2/2 plans complete

Plans:
**Wave 1**

- [x] 02-01-PLAN.md — Platform-split transaction: win/posix pairs (verbatim move + D-12/D-13 POSIX fixes), neutral umbrella (D-15), CMake `if(WIN32)/elseif(UNIX)` selection + `ASIOMGR_LOCALFILE_HEADER` define (D-10), green-build gate

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 02-02-PLAN.md — Permanent zero-#ifdef guard (`no_ifdef_test` via `addtest()`, D-17/D-18) + phase-end grep gate + full-suite verification + optional WSL POSIX syntax stretch

### Phase 3: Parser Layer Removal

**Goal:** The public API carries no parse concept anywhere — `FileParser` is deleted, the `parse` bool is gone from every signature and callback, and the live `save`-driven auto-save-after-load flow keeps working
**Mode:** mvp
**Depends on:** Phase 1 (renamed classes; touches all loaders/devices/tests)
**Requirements:** PARSE-01, PARSE-02, PARSE-03, PARSE-04
**Success Criteria** (what must be TRUE):

  1. `include/FileParser.hpp` is deleted; `RegisterParser`, `ParseData`, and the `parsers` map no longer exist in `FileManager`
  2. `CompletionCallback` is `(ioc, ResultType, bool save)`; `FileLoader::LoadASync`, `FileManager::LoadASync`, `HTTPLoader`, `IPFSLoader`, `SFTPLoader`, `WSLoader`, all `*Common` devices, and every call site (tests + example) compile without a `parse` parameter
  3. Loading with `save=true` still triggers auto-save of loaded data via the registered saver (verified by a passing test exercising the auto-save chain)
  4. No behavioral change to load/save paths beyond signature cleanup — callbacks still receive `ioc`, `ResultType`, `save`
  5. Windows build + full test suite green after the signature change

**Plans:** 2/2 plans complete

Plans:
**Wave 1**

- [x] 03-01-PLAN.md — PARSE-01 registry deletion (FileParser.hpp, RegisterParser/ParseData/parsers, LoadFile 1-arg) + D-20 savers.find UB guard + D-19 auto-save chain test — independently green

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 03-02-PLAN.md — PARSE-02 atomic parse-bool strip (12 headers + 11 sources + 5 test files + D-22 example line) + D-23/D-24 phase-end gate (word-boundary grep + build + ctest)

### Phase 4: Build Purge & Generic Example

**Goal:** The build system carries zero MNN dependency and a generic example demonstrates `file://` load and save via `FileManager` using generic binary fixtures
**Mode:** mvp
**Depends on:** Phases 1-3 (example is written against the final post-parse-removal API)
**Requirements:** MNN-01, MNN-03, MNN-06, BUILD-01
**Success Criteria** (what must be TRUE):

  1. `find_package(MNN)`, `${MNN_INCLUDE_DIR}` include dirs, and the `MNN_LIBS` glob are gone from root `CMakeLists.txt`, `src/CMakeLists.txt`, and `example/CMakeLists.txt`; `grep -ri mnn CMakeLists.txt */CMakeLists.txt` returns no matches
  2. A generic example target (e.g. `FileExample`) builds and demonstrates `file://` load and save through `FileManager` with no MNN inference code or library references; the stale root-level `MNNExample.cpp` legacy copy is removed
  3. `test/1.mnn` and `test/2.mnn` are replaced by generic binary fixtures (e.g. `test/*.bin`); no `.mnn` files remain under `test/`
  4. Windows build + full test suite green with the MNN-free build system

**Plans:** 1/2 plans executed

Plans:
**Wave 1**

- [x] 04-01-PLAN.md — MNN CMake purge transaction (root + example + CommonBuildParameters D-31 + wrappers D1 + dead cmake/common.cmake) + minimal FileExample with two-phase ioc roundtrip (D-25/D-27/D-28) + build/grep/ctest/smoke gate (MNN-01, MNN-06, BUILD-01)

**Wave 2** *(blocked on Wave 1 completion)*

- [ ] 04-02-PLAN.md — Fixture swap (.mnn → test/fixture.bin, D-29/D-30) + D-32 test neutralization (mnn:// → foo://) + phase-end consolidated dual-grep gate (MNN-03)

### Phase 5: Zero-MNN Verification & Green Suite

**Goal:** Milestone acceptance gate passes — the repo is verifiably MNN-free end to end and the complete test suite is green from a clean tree on Windows
**Mode:** mvp
**Depends on:** Phases 1-4
**Requirements:** TEST-03, TEST-04, BUILD-02
**Success Criteria** (what must be TRUE):

  1. `grep -ri mnn` over `include/`, `src/`, `test/`, `example/`, and all `CMakeLists.txt` returns zero matches (any stragglers found are fixed in this phase)
  2. No test references `.mnn` files; all fixtures are generic binaries or self-created temp files
  3. Full test suite builds and passes on Windows with `BUILD_TESTING=ON` — `ctest` reports zero failures
  4. A fresh out-of-source configure + build + test from a clean tree succeeds end to end

**Plans:** TBD

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| MNN-01 | Phase 4 | Pending |
| MNN-02 | Phase 1 | Pending |
| MNN-03 | Phase 4 | Pending |
| MNN-04 | Phase 1 | Pending |
| MNN-05 | Phase 1 | Pending |
| MNN-06 | Phase 4 | Pending |
| FILE-01 | Phase 1 | Pending |
| FILE-02 | Phase 1 | Pending |
| FILE-03 | Phase 1 | Pending |
| FILE-04 | Phase 1 | Pending |
| PLAT-01 | Phase 2 | Pending |
| PLAT-02 | Phase 2 | Pending |
| PLAT-03 | Phase 2 | Pending |
| PLAT-04 | Phase 2 | Pending |
| PARSE-01 | Phase 3 | Pending |
| PARSE-02 | Phase 3 | Pending |
| PARSE-03 | Phase 3 | Pending |
| PARSE-04 | Phase 3 | Pending |
| TEST-01 | Phase 1 | Pending |
| TEST-02 | Phase 1 | Pending |
| TEST-03 | Phase 5 | Pending |
| TEST-04 | Phase 5 | Pending |
| BUILD-01 | Phase 4 | Pending |
| BUILD-02 | Phase 5 | Pending |

**Coverage: 24/24 v1 requirements mapped ✓** (no orphans, no duplicates)

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. LocalFile Rename & MNN Code Purge | 0/? | Not started | - |
| 2. LocalFileCommon Platform Split | 2/2 | Complete    | 2026-09-04 |
| 3. Parser Layer Removal | 2/2 | Complete    | 2026-09-04 |
| 4. Build Purge & Generic Example | 1/2 | In Progress|  |
| 5. Zero-MNN Verification & Green Suite | 0/? | Not started | - |

---

*Roadmap created: 2026-09-03*
