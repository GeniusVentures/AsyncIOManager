---
plan: 02-01
phase: 02-localfilecommon-platform-split
completed: 2026-09-03
requirements: [PLAT-01, PLAT-02, PLAT-03, PLAT-04]
---

# Plan 02-01 Summary: LocalFileCommon Platform-Split Transaction

## What Was Built

Split the dual-branch `LocalFileDevice` into two CMake-selected, platform-pure file pairs and cut the tree over to them in one atomic transaction:

- **`include/LocalFileCommon.win.hpp`** — Windows `LocalFileDevice` declaration on `boost::asio::stream_file`, verbatim move from the old `#else` branch (D-11). No `fd_` member, logger tag `"LocalFileCommon"` (D-14).
- **`src/LocalFileCommon.win.cpp`** — Windows ctor + `Open()` moved verbatim: raw `writemode` in `flags_`, `read_only`/`write_only|create` mapping, string-concat logging kept, no `truncate` added.
- **`include/LocalFileCommon.posix.hpp`** — POSIX declaration on `boost::asio::posix::stream_descriptor` with unconditional `<fcntl.h>` + stream_descriptor includes, `fd_ = -1` retained, and the D-13 fix: destructor's only close is `file_.close( ec_ )` (old `close( fd_ )` removed — Boost 1.85 closes the underlying descriptor even on error).
- **`src/LocalFileCommon.posix.cpp`** — POSIX ctor + `Open()` with the D-12 fix: `flags_( writemode == 0 ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC )`. The assign-error path's `::close( fd_ )` preserved (error cleanup, not the double-close); `::open` mode `0666` and `errno`/`system_category()` mapping verbatim.
- **`include/LocalFileCommon.hpp`** — reduced to the neutral umbrella (D-15): neutral prefix, outcome re-export, closed `namespace sgns` block, CMake-injection comment, `#if !defined( ASIOMGR_LOCALFILE_HEADER )` + `#error` guard (not `#ifndef` — stays clean of D-17 scan), selector `#include ASIOMGR_LOCALFILE_HEADER` as the last line, outside any namespace.
- **`src/CMakeLists.txt`** — `if(WIN32)/elseif(UNIX)` block sets `ASIOMGR_LOCALFILE_SOURCE` + `ASIOMGR_LOCALFILE_HEADER_NAME` (PLAT-03); `add_library` first entry is now `${ASIOMGR_LOCALFILE_SOURCE}`; `target_compile_definitions(AsyncIOManager PRIVATE ASIOMGR_LOCALFILE_HEADER="${ASIOMGR_LOCALFILE_HEADER_NAME}")` with literal quotes (TEST_CERT_DIR form, D-10).
- **`src/LocalFileCommon.cpp`** — deleted (`git rm`), atomic with the CMake edit.

`src/LocalFileLoader.cpp` and `src/LocalFileSaver.cpp` untouched (D-08) — `git diff --stat` empty for both.

## Key Files

### created
- include/LocalFileCommon.win.hpp
- src/LocalFileCommon.win.cpp
- include/LocalFileCommon.posix.hpp
- src/LocalFileCommon.posix.cpp

### modified
- include/LocalFileCommon.hpp (reduced to neutral umbrella)
- src/CMakeLists.txt (platform selection + compile definition)

### deleted
- src/LocalFileCommon.cpp

## Commits

- `3b4a667` feat(02-01): add Windows LocalFileDevice pair (verbatim move, D-11)
- `8d5eba6` feat(02-01): add POSIX LocalFileDevice pair with O_TRUNC + single-close fixes (D-12, D-13)
- `2d92579` feat(02-01): cutover to CMake platform selection, neutral umbrella, delete dual-branch source (PLAT-03/04, D-10, D-15)

## Verification Evidence

- Zero-#ifdef grep over `include/LocalFileCommon*.hpp` + `src/LocalFileCommon*.cpp`: empty, git grep exit code 1 (PASS)
- `Test-Path src/LocalFileCommon.cpp` → False; no CMake reference to `LocalFileCommon.cpp` remains
- `cmake -S build/Windows -B build/Windows/Debug` → exit 0 (configure clean)
- `cmake --build build/Windows/Debug --config Debug` → exit 0, compile log shows `LocalFileCommon.win.cpp` (macro-expanded selector proven working)
- `ctest --test-dir build/Windows/Debug -C Debug --output-on-failure` → **"100% tests passed, 0 tests failed out of 8"** (same test count as Phase 1 end state — none added or lost)
- `git diff --stat src/LocalFileLoader.cpp src/LocalFileSaver.cpp` → empty (D-08 honored)

## Self-Check: PASSED

All must_haves verified: platform pairs exist and are branch-free; Windows pair byte-faithful (no truncate, concat logging, raw `flags_`); POSIX pair carries exactly the two locked fixes (O_TRUNC added, destructor single-close, error-path `::close` retained); old source deleted with no dangling CMake reference; Windows build + full suite green with `LocalFileCommon.win.cpp` in the compile log.

## Deviations

None. Tasks executed as planned in the planned order (inert pairs first, cutover last).

## Known Limitation (carried forward)

`ASIOMGR_LOCALFILE_HEADER` is PRIVATE on the `AsyncIOManager` target (D-10), so installed-package consumers that compile the umbrella header must define the macro themselves. Accepted for this milestone; moving to PUBLIC-on-target is the future fix if export consumers appear (RESEARCH Q1).
