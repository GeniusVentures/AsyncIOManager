---
plan: 02-02
phase: 02-localfilecommon-platform-split
completed: 2026-09-03
requirements: [PLAT-01, PLAT-02]
---

# Plan 02-02 Summary: Zero-#ifdef Enforcement + Phase Gate

## What Was Built

- **`test/src/no_ifdef_test.cpp`** — the permanent guard (D-17): `LocalFileLayerHasNoPlatformBranching` scans exactly the five D-18 local-file files (built from compile-time `LOCALFILE_INCLUDE_DIR`/`LOCALFILE_SRC_DIR` defines) and fails on `#ifdef`, `#ifndef _WIN32`, or `#if defined( _WIN32 )` after whitespace-stripping; unopenable files fail loud via `ADD_FAILURE` (renames/deletes cannot silently skip). `ScannerDetectsViolations` is the anti-vacuity self-check: a temp probe file containing all three banned forms plus a benign `#pragma once` must be detected; the probe is removed even on the failure path.
- **`test/src/CMakeLists.txt`** — `addtest(no_ifdef_test ...)` registered in the always-built Phase 2 file-based section (NOT behind `ASYNC_IO_MANAGER_NETWORK_TESTS`), with the two quoted absolute-path compile definitions (TEST_CERT_DIR form).

No production symbols touched.

## Key Files

### created
- test/src/no_ifdef_test.cpp

### modified
- test/src/CMakeLists.txt (one added block — `git diff` shows no other change)

## Commits

- `cab0ba0` test(02-02): add permanent no_ifdef_test guard for local-file layer (D-17, D-18)

## Verification Evidence

### Gate 1 — one-shot zero-#ifdef grep (D-17)

```
git grep -nE '#[ \t]*(ifdef|ifndef[ \t]+_WIN32|if[ \t]+defined\([ \t]*_WIN32[ \t]*\))' -- 'include/LocalFileCommon*.hpp' 'src/LocalFileCommon*.cpp'
GATE-1 grep exit: 1 (1 = PASS / no matches)
```

### Gate 2 — full suite on the Windows wrapper build

```
ctest --test-dir build/Windows/Debug -C Debug --output-on-failure

100% tests passed, 0 tests failed out of 9

Total Test time (real) =  75.79 sec
```

Test count is the Plan 01 end state (8) + 1 (`no_ifdef_test`, containing 2 gtest cases) = 9, zero failures. Prior suite (http/sftp/ipfs/localfile/filemanager) all green.

### Gate 3 — WSL stretch (A3): real-POSIX-compiler syntax check — PASS

`wsl -d Ubuntu-22 -- g++ -fsyntax-only -std=c++17 -DASIOMGR_LOCALFILE_HEADER='"LocalFileCommon.posix.hpp"' -DSPDLOG_FMT_EXTERNAL -DSPDLOG_COMPILED_LIB -I<repo>/include -I<boost-1_85> -I<spdlog> -I<libp2p> -I<fmt> src/LocalFileCommon.posix.cpp`

Result: **`SYNTAX_EXIT:0`** — no errors, no warnings emitted. This upgrades PLAT-02 from "structurally correct on paper" to **"syntax-checked with a real POSIX compiler (g++ on Ubuntu-22, C++17)"**. Notes: the repo's spdlog is built with external fmt, so `SPDLOG_FMT_EXTERNAL`/`SPDLOG_COMPILED_LIB` had to be defined for the header-side include selection — a probe-environment detail, not a defect in the POSIX files. The temporary check script was removed after the run (not part of the plan's file set).

### File inventory (all zero-#ifdef, verified by grep + guard test)

| File | Status |
|------|--------|
| include/LocalFileCommon.hpp | neutral umbrella, branch-free |
| include/LocalFileCommon.win.hpp | branch-free |
| include/LocalFileCommon.posix.hpp | branch-free, g++-syntax-checked |
| src/LocalFileCommon.win.cpp | branch-free, MSVC-compiled (in suite) |
| src/LocalFileCommon.posix.cpp | branch-free, g++-syntax-checked |

## Self-Check: PASSED

`no_ifdef_test` listed by ctest and passing (both gtest cases: the five-file scan AND the scanner self-check proving non-vacuity); phase grep gate empty; full suite green at 9/9; stretch outcome recorded with disposition rules applied (success path).

## Deviations

None blocking. One environment note: the first stretch-check attempt failed on spdlog's bundled-fmt header selection; resolved by defining the same macros the real build uses (`SPDLOG_FMT_EXTERNAL`, `SPDLOG_COMPILED_LIB` per `build/CommonBuildParameters.cmake`). No source changes were needed — the POSIX files compiled cleanly on the first correct invocation.

## Known Limitation (carried forward from Plan 01)

`ASIOMGR_LOCALFILE_HEADER` is PRIVATE on the `AsyncIOManager` target (D-10) — installed-package consumers compiling the umbrella must define the macro themselves. Accepted for this milestone; PUBLIC-on-target is the future fix if export consumers appear (RESEARCH Q1).
