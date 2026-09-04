# Phase 4: Build Purge & Generic Example - Pattern Map

**Mapped:** 2026-09-04
**Files analyzed:** 12 (8 created/modified, 4 deleted)
**Analogs found:** 8 / 8 editable files (deletions need no analog)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `example/FileExample.cpp` (NEW) | example app (entry point, library consumer) | async file I/O roundtrip (load → save) | `example/MNNExample.cpp` lines 208-210, 262-278 + `test/testutil/asio_helpers.hpp` `IOContextRunner::restart()` | role-match (old example is the API-consumer template; two-phase ioc pattern is new — see RESEARCH D4) |
| `example/CMakeLists.txt` (MOD) | config (build target) | N/A (build wiring) | itself (rename-only edit; current content is the verbatim pattern) | exact |
| `CMakeLists.txt` (root, MOD) | config (dependency discovery) | N/A | itself lines 33-36 (delete-only); adjacent dep blocks (`find_package(OpenSSL)` + `include_directories`) show the block shape | exact |
| `build/CommonBuildParameters.cmake` (MOD) | config (super-build params) | N/A | itself lines 273-278 (delete block); adjacent `xxhash` block (265-270) and `Libssh2` block (281-285) show the canonical dep-block layout | exact |
| `build/Windows/CMakeLists.txt` (MOD) | config (platform wrapper) | N/A | itself lines 85-87 (one-token rename) | exact |
| `build/Linux/CMakeLists.txt` (MOD) | config (platform wrapper) | N/A | itself line 54 (comment scrub) | exact |
| `build/OSX/CMakeLists.txt` (MOD) | config (platform wrapper) | N/A | itself line 70 (comment scrub) | exact |
| `test/src/filemanager_test.cpp` (MOD) | test (GTest exception-path) | request-response (dispatch error) | adjacent `SaveASync_UnregisteredPrefixThrows` in the same file (~lines 106-115) | exact |
| `example/example_data.bin` (NEW) | example asset (binary fixture) | file I/O input | none (first committed binary fixture) | none |
| `test/fixture.bin` (NEW) | test asset (binary fixture) | file I/O | none (first committed binary fixture) | none |
| `cmake/common.cmake` (DELETE, D2-recommended) | config (dead legacy helper) | N/A | N/A — deletion, zero includers/callers | N/A |
| `example/MNNExample.cpp`, `MNNExample.cpp` (root), `test/1.mnn`, `test/2.mnn` (DELETE) | legacy sources/assets | N/A | N/A — deletions | N/A |

---

## Pattern Assignments

### `example/FileExample.cpp` (example app, async file I/O roundtrip)

**Analog:** `example/MNNExample.cpp` (the only existing `FileManager` consumer) + RESEARCH D4 two-phase sketch

**Includes pattern** — strip to the minimum (D-25: no libp2p/bitswap/soralog). The old example's include block (`example/MNNExample.cpp:1-20`) shows what to *omit*; the new file needs only:

```cpp
// Old (lines 10-20) — ALL of this goes away:
#include "FileManager.hpp"          // ← KEEP this one
//#include "MNNLoader.hpp"           // ← dead since Phase 1
//#include "IPFSLoader.hpp"
//#include "HTTPLoader.hpp"
#include "URLStringUtil.h"          // ← not needed (no URL parsing in new example)
#include <libp2p/injector/host_injector.hpp>   // ← drop (D-25)
#include <bitswap.hpp>                         // ← drop (D-25)
#include <libp2p/log/configurator.hpp>         // ← drop (soralog, D-25)
```

New includes: `<iostream>`, `<memory>`, `<string>`, `"FileManager.hpp"` — nothing else. `boost/asio/io_context.hpp` comes transitively via `FileManager.hpp` (verified: it declares `std::shared_ptr<boost::asio::io_context>` in every async signature).

**Initialization pattern** (`example/MNNExample.cpp:222-224` — the only mandatory setup call):

```cpp
    // Initialize AsyncIOManager with bitswap
    FileManager::GetInstance().InitializeSingletons();
    FileManager::GetInstance().setBitswap( bitswap );   // ← DROP: setBitswap is IPFS-only;
                                                         //    filemanager_test proves InitializeSingletons()
                                                         //    alone suffices for file:// roundtrips
```

New example: `FileManager::GetInstance().InitializeSingletons();` on one line, no bitswap.

**Core LoadASync call pattern** (`example/MNNExample.cpp:262-278` — the post-Phase-3 signature, the closest existing template):

```cpp
        FileManager::GetInstance().LoadASync(
            file_names[i],
            true,  // save                                    // ← new example passes false (D-27)
            ioc,
            [file_name = file_names[i]]( auto buffers )
            {
                if ( buffers )
                {
                    std::cout << "Successfully loaded: " << file_name << std::endl;
                }
                else
                {
                    std::cout << "Failed to load " << file_name << ": " << buffers.error().message() << std::endl;
                }
            },
            "ipfs" // use file handler, not ipfs saver        // ← new example passes "" (irrelevant when save=false)
        );
```

Copy verbatim with three token changes: `false` for save, `""` for savetype, and the callback additionally captures the result (`loaded = result;`) plus byte counts (`result.value()->second[0].size()`, name `result.value()->first[0]`) per D-27.

**Error handling pattern** — outcome-checking in the callback, never exceptions (`example/MNNExample.cpp:268-276`): `if ( buffers ) { ... } else { ... buffers.error().message() ... }`. Exit code from `main` (`example/MNNExample.cpp:289-291`): `return 0;` after `ioc->run()` — extend to `return saved.has_value() ? 0 : 1;`.

**ioc lifecycle pattern** — the critical divergence from the analog. Old example (`example/MNNExample.cpp:208-210` + `289`) uses a work guard + single `run()`:

```cpp
    // OLD — work guard keeps run() alive; fine for fire-and-forget loads, but a work guard
    // CANNOT rescue the stop()-abandoned-handler problem when chaining save after load:
    auto workGuard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
        ioc->get_executor() );
    ...
    ioc->run();
```

New example MUST use the two-phase pattern (RESEARCH Discovery D4; `restart()` idiom proven in `test/testutil/asio_helpers.hpp:60-68`):

```cpp
    // Phase 1: load — run() returns when FileManager's counter drains (ioc->stop())
    FileManager::GetInstance().LoadASync( inputUrl, false, ioc, loadCb, "" );
    ioc->run();

    // Phase 2: save — restart() clears the stopped flag (asio requirement before any 2nd run())
    FileManager::GetInstance().SaveASync( saveUrl, loaded, ioc, saveCb );
    ioc->restart();
    ioc->run();
```

`IOContextRunner::restart()` (`test/testutil/asio_helpers.hpp:60-68`) is the in-repo precedent that `ioc_->restart()` between runs is the sanctioned way to reuse a stopped context:

```cpp
    void restart()
    {
        stop();
        ioc_->restart();    // ← the exact call the example needs, already proven in-repo
        ...
    }
```

**Save URL semantics** (Discovery D3) — the save target must be a directory ending in `/`. The suite-proven idiom is `test/src/filemanager_test.cpp:174-177`:

```cpp
    // Pass directory as URL; LocalFileSaver appends the data filename
    FileManager::GetInstance().SaveASync(
        "file://" + dir.pathString() + "/", data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );
```

New example uses literal `"file://example_output/"` → produces `example_output/example_data.bin`. NOT `"file://example_output.bin"` (concatenation garbage per D3).

**Result plumbing** — capture-by-reference lambda into a stack `ResultType` (race-free single-threaded; callback runs on the io thread before `run()` returns). Template from `filemanager_test.cpp:158-160`:

```cpp
    bool completed = false;
    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(), false, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; },
        "" );
```

**Reference skeleton:** RESEARCH.md §Code Examples contains a complete ~60-line `FileExample.cpp` already conformed to all of the above — use it as the starting draft, adjust wording at executor discretion.

---

### `example/CMakeLists.txt` (config, rename-only)

**Analog:** itself — the current 7-line file is the verbatim pattern (super-build-only wiring per D-26)

**Current content (whole file, lines 1-7):**

```cmake
#add_subdirectory(application)
FILE(GLOB MNN_LIBS "${MNN_LIBRARY_DIR}/*")
add_executable(MNNExample MNNExample.cpp)

add_dependencies(MNNExample AsyncIOManager)
target_link_libraries(MNNExample PRIVATE AsyncIOManager spdlog::spdlog protobuf::libprotobuf)
include_directories(../include)
```

**Edit pattern:** delete line 2 (the glob — dead weight today; `MNN_LIBS` never appears on a link line, and `MNN_LIBRARY_DIR` dies with D-31), rename the target/source tokens on lines 3-6. Target after edit:

```cmake
#add_subdirectory(application)
add_executable(FileExample FileExample.cpp)

add_dependencies(FileExample AsyncIOManager)
target_link_libraries(FileExample PRIVATE AsyncIOManager spdlog::spdlog protobuf::libprotobuf)
include_directories(../include)
```

**Do NOT touch** `include_directories(../include)` — Pitfall P4: in wrapper mode the wrapper's own include_directories resolve against `build/Windows/`, not repo root; this per-subdirectory line is what makes `#include "FileManager.hpp"` resolve. Keep link shape verbatim (discretion allows change but RESEARCH recommends minimal diff).

---

### `CMakeLists.txt` root (config, delete-only)

**Analog:** itself; the adjacent dependency blocks show the block shape being deleted

**Lines to delete (33-36 region):**

```cmake
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${GSL_INCLUDE_DIR})
find_package(MNN CONFIG REQUIRED)           # ← DELETE
include_directories(${MNN_INCLUDE_DIR})     # ← DELETE
find_package(fmt CONFIG REQUIRED)
```

Pure 2-line deletion; every neighboring `find_package(X) + include_directories(${X_INCLUDE_DIR})` pair (e.g. `OpenSSL` above, `ipfs-lite-cpp` below) demonstrates the canonical form that remains untouched. No replacement lines, no reordering.

---

### `build/CommonBuildParameters.cmake` (config, block delete)

**Analog:** itself lines 273-278; adjacent `xxhash` (265-270) and `Libssh2` (281-285) blocks show the dep-block layout

**Block to delete (lines 273-278, including its comment header):**

```cmake
# --------------------------------------------------------
# Set config of MNN
set(MNN_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/include")
set(MNN_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/lib")
include_directories(${MNN_INCLUDE_DIR})
set(MNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../MNN/include")
```

**Boundary pattern** — what remains on either side (verified lines 265-271 and 281-285):

```cmake
# --------------------------------------------------------
# Set config of xxhash
set(xxhash_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/include")
set(xxhash_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/lib")
set(xxhash_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/lib/cmake/xxhash")
find_package(xxhash CONFIG REQUIRED)
include_directories(${xxhash_INCLUDE_DIR})
```

```cmake
# ----------------------BUILD EXTERNAL PROJECT------------------
# Set config of LIBSSH2
set(Libssh2_DIR "${_THIRDPARTY_BUILD_DIR}/libssh2/lib/cmake/libssh2")
```

Delete the whole MNN chunk including the `# Set config of MNN` comment and its `# ----` separator line; the file remains a sequence of independent `set`/`find_package`/`include_directories` blocks — trivially syntactically valid for all three wrappers that `include(../CommonBuildParameters.cmake)` it.

---

### `build/Windows/CMakeLists.txt` (config, one-token rename — MANDATORY, Discovery D1)

**Analog:** itself lines 85-87

**Current:**

```cmake
# install example binary to bin
SET(TESTAPP MNNExample )

install(TARGETS ${TESTAPP} DESTINATION "${BUILD_FILELOADER_DIR}/bin")
```

**After:** `SET(TESTAPP FileExample )` — nothing else changes. The `install(TARGETS ${TESTAPP} ...)` line resolves at generate time after `CommonBuildParameters.cmake:327` adds `example/`; leaving `MNNExample` here breaks the wrapper configure the moment the target renames. This edit MUST land in the same wave as `example/CMakeLists.txt`.

---

### `build/Linux/CMakeLists.txt` + `build/OSX/CMakeLists.txt` (config, comment scrub)

**Analog:** `build/Windows/CMakeLists.txt:85-87` (the live form these comments echo)

Both files carry the identical commented block at their tails (Linux line 54, OSX line 70 — both verified):

```cmake
# install example binary to bin
#SET(TESTAPP MNNExample )

install(TARGETS ${TESTAPP} DESTINATION "${BUILD_FILELOADER_DIR}/bin")
```

**Note:** `install(TARGETS ${TESTAPP} ...)` with `TESTAPP` undefined is a pre-existing latent no-op on these platforms — not this phase's concern. Scrub only the commented `#SET(TESTAPP MNNExample )` line (delete or update the comment to `FileExample`) so the recursive grep gate (`git grep -iI mnn -- '*CMakeLists.txt'`) returns empty.

---

### `cmake/common.cmake` (config — DELETE, Discovery D2 recommendation)

**Analog:** none needed — repo-wide grep shows zero `include(common.cmake)` and zero callers of `add_fileloader_library`/`add_fileloader_executable`. The 31-line file is dead and holds the last three MNN references (`MNN_LIBS` glob line 11, link lines 19 and 30):

```cmake
   FILE(GLOB MNN_LIBS "${MNN_LIBRARY_DIR}/*")
   ...
   target_link_libraries(${FileLoader_LIB} ${MNN_LIBS})
   ...
    target_link_libraries(${executable_name} ${FileLoader_LIB} ${MNN_LIBS})
```

Delete the file outright (planner option: scrub the 3 lines if deletion feels too aggressive). Consistent with D-31's rationale — leaves Phase 5's stronger `*.cmake` grep clean.

---

### `test/src/filemanager_test.cpp` (test, D-32 neutralization)

**Analog:** the immediately adjacent `SaveASync_UnregisteredPrefixThrows` in the same file (~lines 106-115)

**Template (adjacent test, exact shape to preserve):**

```cpp
TEST_F( FileManagerIntegrationTest, SaveASync_UnregisteredPrefixThrows )
{
    IOContextRunner runner;
    auto            data = makeSingleFileResult( "test.bin", "content" );

    EXPECT_THROW(
        {
            FileManager::GetInstance().SaveASync(
                "unknown://some/path", data, runner.ioc(), []( FileManager::ResultType ) {} );
        },
        std::range_error );
}
```

**Target (current, lines ~124-133):**

```cpp
TEST_F( FileManagerIntegrationTest, SaveASync_MnnPrefixThrows )
{
    IOContextRunner runner;
    auto            data = makeSingleFileResult( "test.bin", "content" );

    EXPECT_THROW(
        {
            FileManager::GetInstance().SaveASync(
                "mnn://some/path", data, runner.ioc(), []( FileManager::ResultType ) {} );
        },
        std::range_error );
}
```

**Edit pattern (P8 — exactly two tokens):** `"mnn://some/path"` → `"foo://some/path"` (must differ from the adjacent test's `"unknown://"` so the two stay distinct data points), and the test name `SaveASync_MnnPrefixThrows` → e.g. `SaveASync_FooPrefixThrows` (no `mnn` substring). Same fixture call `makeSingleFileResult("test.bin", "content")` (helper at `test/testutil/test_fixture.hpp:20-28`), same `EXPECT_THROW`, same `std::range_error`, same no-op callback. Nothing else in the file changes.

---

### `example/example_data.bin` + `test/fixture.bin` (binary assets)

**Analog:** none — first committed binary fixtures in the repo (see No Analog Found). RESEARCH.md §Code Examples provides the one-shot generation recipe (PowerShell `[IO.File]::WriteAllBytes`, 1024 ramp-pattern bytes `($i * 7 + 13) % 256`). Committed directly, no generator script, no CMake-time generation (D-28). Content requirements: ~1KB, non-uniform varied bytes so byte-count reporting in the example is meaningful.

---

## Shared Patterns

### Async result handling (applies to: FileExample load + save callbacks)

**Source:** `example/MNNExample.cpp:268-276` (outcome check in callback), `test/src/filemanager_test.cpp:158-160` (capture-by-ref stack variable)
**Apply to:** both callbacks in `example/FileExample.cpp`

```cpp
[result-captured]( FileManager::ResultType buffers )
{
    if ( buffers )
    {
        // success: buffers.value()->first[0] (name), buffers.value()->second[0].size() (bytes)
    }
    else
    {
        // failure: buffers.error().message() — never throw across the async boundary
    }
}
```

### Save URL as directory prefix (applies to: every `file://` SaveASync)

**Source:** `test/src/filemanager_test.cpp:174-177`
**Apply to:** `example/FileExample.cpp` save call, any future consumer

```cpp
    // Pass directory as URL; LocalFileSaver appends the data filename
    FileManager::GetInstance().SaveASync(
        "file://" + dir + "/", data, ioc, cb );   // trailing "/" is mandatory (Discovery D3)
```

### CMake edits as pure deletion + one-token renames (applies to: all 6 CMake touchpoints)

**Source:** Phase 1-3 precedent (per RESEARCH); the adjacent untouched dep blocks in `CMakeLists.txt` and `build/CommonBuildParameters.cmake`
**Apply to:** root CMakeLists, example CMakeLists, CommonBuildParameters, all three wrappers

- No new `find_package`, no link-list changes beyond MNN removal, library target `AsyncIOManager` untouched
- The only behavioral change is the `MNNExample` → `FileExample` rename, which must flip in BOTH `example/CMakeLists.txt` AND `build/Windows/CMakeLists.txt:85` in the same wave (Pitfalls P2/P3 — the purge is one atomic wave, verified by one reconfigure)

### EXPECT_THROW dispatch-contract tests (applies to: D-32 edit)

**Source:** `test/src/filemanager_test.cpp` unhappy-path section (~lines 98-133)
**Apply to:** the neutralized `SaveASync_FooPrefixThrows` test

Same skeleton every dispatch test uses: `IOContextRunner runner;` + `makeSingleFileResult(...)` + `EXPECT_THROW({ SaveASync(...); }, std::range_error);` — the unknown-prefix dispatch contract (`src/FileManager.cpp` throws `std::range_error` on unregistered prefix) is the behavior under test; only the prefix literal varies.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `example/example_data.bin` | example asset | file I/O input | No committed binary fixture exists in the repo; generation recipe from RESEARCH.md §Code Examples |
| `test/fixture.bin` | test asset | file I/O | Same — first binary fixture under `test/` (existing tests use `TempFile("arbitrary string content")` created at runtime) |

Note: the **two-phase `run()/restart()/run()` ioc pattern** for `FileExample.cpp` has no complete in-repo analog either — the old example used a work guard + single `run()`, and `IOContextRunner` runs the context on a background thread. The authoritative source is RESEARCH.md Discovery D4 (with `asio_helpers.hpp:60-68` as the in-repo `restart()` precedent). Planner should treat RESEARCH's D4 sketch as the pattern of record for the example's `main` structure.

## Metadata

**Analog search scope:** `example/`, `test/src/`, `test/testutil/`, `include/`, root `CMakeLists.txt`, `build/{CommonBuildParameters,Windows,Linux,OSX}`, `cmake/`
**Files scanned:** ~15 (8 analog reads; deletions verified via RESEARCH's live greps rather than re-read)
**Pattern extraction date:** 2026-09-04
**Verification loop precedent:** Phases 1-3 proven commands in RESEARCH.md §Code Examples (reconfigure wrapper → build → recursive grep gate → ctest)
