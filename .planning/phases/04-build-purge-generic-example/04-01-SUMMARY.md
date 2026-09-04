---
plan: 04-01
status: complete
started: 2026-09-04
completed: 2026-09-04
---

# Plan 04-01 Summary: Build Purge & Generic FileExample

## What Was Built

Wave 1 of Phase 4 — MNN fully purged from the build system and `MNNExample` replaced by a minimal generic `FileExample`:

1. **`example/FileExample.cpp`** (new, 70 lines): minimal `file://`-only load→save roundtrip — `InitializeSingletons()` → `LoadASync(input, save=false, ...)` → `ioc->run()` → `SaveASync("file://example_output/", ...)` → `ioc->restart()` → `ioc->run()`. Zero MNN/libp2p/bitswap/soralog symbols (MNN-06, D-25).
2. **`example/example_data.bin`** (new, 1024 bytes): deterministic ramp `byte[i] = (i*7+13) % 256`, committed directly, no generator (D-28).
3. **CMake purge (MNN-01, D-31)** — one atomic transaction (commit `1f0af45`):
   - root `CMakeLists.txt`: `find_package(MNN CONFIG REQUIRED)` + `include_directories(${MNN_INCLUDE_DIR})` deleted
   - `example/CMakeLists.txt`: `MNN_LIBS` glob deleted; target renamed to `FileExample`; `include_directories(../include)` kept verbatim (P4)
   - `build/CommonBuildParameters.cmake`: whole MNN block (separator + comment + 4 lines) deleted
   - `build/Windows/CMakeLists.txt`: `SET(TESTAPP FileExample )` — the live install-target rename (D1)
   - `build/Linux/CMakeLists.txt` + `build/OSX/CMakeLists.txt`: commented `#SET(TESTAPP MNNExample )` lines deleted
   - `cmake/common.cmake`: deleted outright (dead 31-line helper, zero includers — D2)
   - `example/MNNExample.cpp` (291 lines) and root `MNNExample.cpp` (117 lines) deleted

## Deviations from Plan

**One deviation (commit `10bf1f7`):** the RESEARCH skeleton declared `FileManager::ResultType loaded;` / `saved;` as stack variables, but `outcome::result<T>` has a **deleted default constructor** — this does not compile (MSVC C2280 at lines 20/49, first build attempt). Fix: capture results via `std::optional<FileManager::ResultType>` (assigned only on success; failure leaves it empty and is reported via `result.error().message()` in the callback). This required adding `#include <optional>` to the include set (plan's include-list acceptance criterion said "only `<iostream>`, `<memory>`, `<string>`, `FileManager.hpp`" — the plan text was written against the RESEARCH skeleton and is provably un-compilable as specified). The optional-empty check `if (!loaded.has_value()) return 1;` preserves the plan's exact exit-code contract; `loaded.value()` passed to `SaveASync` is guarded by that check. Deviation is mechanical, minimal, and preserves every locked decision (D-25/D-27/D-28 structure, two-phase ioc, no forbidden symbols).

## Task Completion

| Task | Status | Commit |
|------|--------|--------|
| 1: FileExample.cpp + example_data.bin | ✓ | `53b14e5` |
| 2: Atomic CMake purge + rename transaction | ✓ | `1f0af45` (+fix `10bf1f7`) |
| 3: Wave-1 gate (reconfigure/build/grep/ctest/smoke) | ✓ | no code changes |

## Gate Evidence (Task 3)

```
(1) cmake -S build/Windows -B build/Windows/Debug
    → exit 0 ("Configuring done / Generating done") — rename pair resolves,
      MNN block deletion clean for all wrappers

(2) cmake --build build/Windows/Debug --config Debug
    → exit 0; FileExample.cpp compiled (1 hit), FileExample.exe linked (1 hit),
      zero MNNExample mentions in the build log

(3) git grep -iI mnn -- CMakeLists.txt '*CMakeLists.txt' '*.cmake'
    → exit 1 (no matches) — recursive CMake gate PASSES

(4) ctest --test-dir build/Windows/Debug -C Debug --output-on-failure
    → "100% tests passed, 0 tests failed out of 9" (93.18 sec)

(5) SMOKE RUN (from example/ CWD):
    [2026-09-04 18:58:06][info][LocalFileLoader] LOCAL File Finished
    Loaded "example_data.bin" (1024 bytes) from file://example_data.bin
    Saved "example_data.bin" (1024 bytes) to file://example_output/
    → exit 0; example\example_output\example_data.bin created, Length 1024
    (BUILD-01 live proof + RESEARCH assumption A2 confirmed)

    Failure-path spot check (nonexistent input):
    [2026-09-04 18:58:11][error][LocalFileCommon] Failed to open file (Windows): The system cannot find the file specified
    Load failed: File could not be opened
    → exit 1 (error delivered via callback, never thrown)
```

## Self-Check: PASSED

- All Task 1-3 acceptance criteria hold (with the documented `std::optional` deviation)
- `git grep -iI mnn -- CMakeLists.txt '*CMakeLists.txt' '*.cmake'` empty
- `example/` contains `FileExample.cpp` + `example_data.bin`, no `MNNExample.cpp`
- Root `MNNExample.cpp`, `cmake/common.cmake` gone
- D-26 verified negatively: root `CMakeLists.txt` gained no `add_subdirectory(example)`
- MNN-01, MNN-06, BUILD-01 discharged; D-25/D-26/D-27/D-28/D-31 honored

## Notes for Next Wave

- 04-02 clears the deliberately-left test-side residue: `test/1.mnn` + `test/2.mnn` deletion, `test/fixture.bin` commit, D-32 test neutralization, then the consolidated phase-end gate.
