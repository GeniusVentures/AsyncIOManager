# Phase 2: LocalFileCommon Platform Split - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-03
**Phase:** 2-LocalFileCommon Platform Split
**Areas discussed:** Header dispatch mechanism, Split fidelity & semantics, Neutral header contents, Zero-#ifdef enforcement

---

## Header dispatch mechanism

| Option | Description | Selected |
|--------|-------------|----------|
| Umbrella + macro | Keep LocalFileCommon.hpp as the single include; CMake passes a compile definition naming the platform header (e.g. ASIOMGR_PLATFORM_HEADER=LocalFileCommon.win.hpp); umbrella includes it. Zero #ifdef, zero caller changes. | ✓ |
| CMake include dir | src/CMakeLists.txt appends include/LocalFileCommon.win (or .posix) directory to include path so #include "LocalFileCommon.hpp" resolves to the platform file. No macro, but adds include-dir trick. | |
| Per-platform callers | LocalFileLoader/Saver get per-platform .cpp variants compiled per platform. Splits more files; more churn. | |

**File layout sub-question:**

| Option | Description | Selected |
|--------|-------------|----------|
| Headers in include/ | Win/POSIX pairs: .cpp in src/, headers in include/. Matches current split. | ✓ |
| All in src/ | Both platform headers and sources under src/; keeps platform headers out of installed public includes. | |
| Subfolder | include/LocalFileCommon/ subdirectory — changes install glob layout. | |

**Macro shape sub-question:**

| Option | Description | Selected |
|--------|-------------|----------|
| Define full name | ASIOMGR_LOCALFILE_HEADER="LocalFileCommon.win.hpp"; umbrella does #include ASIOMGR_LOCALFILE_HEADER. One knob. | ✓ |
| Platform token | ASIOMGR_PLATFORM=WIN/POSIX + string-paste macros. Two tokens to keep in sync. | |
| Claude's discretion | Planner picks the naming convention. | |

**User's choice:** Umbrella + macro; headers in include/; define-full-name macro
**Notes:** Callers (`LocalFileLoader.cpp`, `LocalFileSaver.cpp`) keep their existing `#include "LocalFileCommon.hpp"` lines untouched.

---

## Split fidelity & semantics

| Option | Description | Selected |
|--------|-------------|----------|
| Pure mechanical | Move each half verbatim; no behavior changes; POSIX quirks preserved. Smallest diff. | |
| Fix POSIX bugs | Move code but fix obvious latent POSIX bugs (missing O_TRUNC, destructor double-close). Windows unchanged. | ✓ |
| Unify on stream_file | Rewrite both halves on boost::asio::stream_file (supports POSIX since 1.70). Bigger rewrite; Windows path changes more. | |

**O_TRUNC sub-question:** Add `O_TRUNC` to POSIX write mode to match Windows overwrite semantics — ✓ selected (vs. keep as-is).

**Destructor sub-question:** Single close path via `file_.close(ec_)` only; drop the extra `::close(fd_)` — ✓ selected (vs. keep double close / Claude's discretion).

**Logger tag sub-question:** Both halves keep `createLogger("LocalFileCommon")` — ✓ selected (vs. per-platform `.win`/`.posix` tags).

**User's choice:** Fix POSIX bugs; add O_TRUNC; single close path; shared logger tag
**Notes:** Divergent semantics between halves were surfaced during discussion (no O_TRUNC on POSIX vs CREATE_ALWAYS-like behavior on Windows; latent double-close on POSIX). User opted to align semantics via targeted fixes rather than preserve divergence or unify transports.

---

## Neutral header contents

| Option | Description | Selected |
|--------|-------------|----------|
| Decls + selector | LocalFileCommon.hpp = shared declarations (doc, namespace, outcome re-exports) + `#include ASIOMGR_LOCALFILE_HEADER`. Single public include; satisfies PLAT-04. | ✓ |
| Pure selector | Umbrella is nothing but the selector include; no shared decls. | |
| New neutral name | Delete LocalFileCommon.hpp; introduce e.g. LocalFileDevice.hpp for shared decls. | |

**Class declaration organization sub-question:**

| Option | Description | Selected |
|--------|-------------|----------|
| Self-contained halves | Each platform header fully declares its own LocalFileDevice incl. getFile() return type and private members. | ✓ |
| Split declaration | Neutral header declares common ctor/Open(); platform headers add the rest. More coupling across 3 files. | |

**User's choice:** Decls + selector; self-contained halves
**Notes:** PLAT-04's "if any remain" clause resolved: shared declarations DO remain (namespace, outcome re-exports, doc banner) and live in the neutral umbrella.

---

## Zero-#ifdef enforcement

| Option | Description | Selected |
|--------|-------------|----------|
| Phase-end grep | One-time grep gate at phase end (same style as Phase 1 zero-mnn gate). No permanent test. | |
| Grep + ctest guard | Phase-end grep gate PLUS permanent ctest scanning local-file sources at test runtime; guards future regressions. | ✓ |
| CMake configure check | file(STRINGS) scan in src/CMakeLists.txt erroring at configure time instead of test time. | |

**Scan scope sub-question:**

| Option | Description | Selected |
|--------|-------------|----------|
| Local-file files only | Scan exactly LocalFileCommon.{hpp,win.hpp,win.cpp,posix.hpp,posix.cpp}. Matches roadmap criteria literally. | ✓ |
| Whole library | Scan all of include/ + src/ — stricter but fails on other devices today; out of scope. | |

**User's choice:** Grep + ctest guard; local-file files only
**Notes:** Clarified during discussion: `#pragma once` and own-name include guards are not platform branching and must not trip the guard; only `#ifdef` / `#ifndef _WIN32` / `#if defined(_WIN32)` count.

---

## Claude's Discretion

- Exact CMake block placement/syntax (variable-per-platform vs inline list append) in `src/CMakeLists.txt`
- Whether `LocalFileCommon.cpp` is deleted outright or reduced to a pointer stub (deletion preferred)
- `no_ifdef_test` implementation details (path-define mechanism mirrors `TEST_CERT_DIR`)
- POSIX `::open` mode argument (0666) and umask notes — kept as today unless structurally wrong

## Deferred Ideas

- Whole-library `#ifdef` cleanup — future hardening work (other device headers still branch)
- Unifying platforms on `boost::asio::stream_file` — rejected this phase; revisit only if POSIX half proves problematic on real Linux CI
- Linux CI for the POSIX half — PROJECT.md Out of Scope for this milestone
- Parse-parameter removal on same call sites — Phase 3 by design
