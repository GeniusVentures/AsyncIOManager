# Phase 2: LocalFileCommon Platform Split - Context

**Gathered:** 2026-09-03
**Status:** Ready for planning

<domain>
## Phase Boundary

The local-file device layer (`LocalFileDevice`, currently `include/LocalFileCommon.hpp` + `src/LocalFileCommon.cpp` with both halves behind `#ifndef _WIN32`) is split into separate Windows and POSIX source+header pairs (`LocalFileCommon.win.*` / `LocalFileCommon.posix.*`), selected by `if(WIN32)/elseif(UNIX)` in `src/CMakeLists.txt`, with zero platform branching inside any local-file source file. The Windows build + full test suite remain green.

**In scope:** the platform file split (PLAT-01, PLAT-02), CMake platform selection mechanism (PLAT-03), platform-neutral shared header (PLAT-04), targeted POSIX latent-bug fixes made while splitting (O_TRUNC, destructor close), and a permanent zero-#ifdef test guard.

**Out of scope (by roadmap design):** parse-parameter/FileParser removal (Phase 3); CMake MNN purge (Phase 4); example rewrite (Phase 4); any change to `LocalFileLoader`/`LocalFileSaver` behavior or their includes (they keep including `LocalFileCommon.hpp` unchanged); Linux CI (POSIX half verified structurally only — compile-correct on paper per PROJECT.md Out of Scope).

</domain>

<decisions>
## Implementation Decisions

### Header dispatch mechanism (how neutral callers get the platform class)
- **D-08:** Umbrella header + CMake-injected compile definition. `include/LocalFileCommon.hpp` remains the single include for callers (`LocalFileLoader.cpp`, `LocalFileSaver.cpp` — zero include-line changes). CMake passes the platform header name; the umbrella includes it. No `#ifdef` anywhere; no include-path tricks.
- **D-09:** File layout: platform headers in `include/` (`LocalFileCommon.win.hpp`, `LocalFileCommon.posix.hpp`), platform sources in `src/` (`LocalFileCommon.win.cpp`, `LocalFileCommon.posix.cpp`) — matching the repo's existing include/ vs src/ split and the root `install include/*.h*` glob.
- **D-10:** Macro shape: CMake defines the FULL header token via `target_compile_definitions`, e.g. `ASIOMGR_LOCALFILE_HEADER="LocalFileCommon.win.hpp"` (PRIVATE on the `AsyncIOManager` target; test targets that include `LocalFileCommon.hpp` need it too). Umbrella body is `#include ASIOMGR_LOCALFILE_HEADER`. One knob, no string-pasting macros.

### Split fidelity & semantics
- **D-11:** Fidelity level: Windows half is a pure mechanical move (behavior byte-identical). POSIX half is moved AND its obvious latent bugs are fixed in-phase (D-12, D-13). No unification rewrite — the halves stay on their current transports (POSIX: `posix::stream_descriptor` + `::open`; Windows: `boost::asio::stream_file`).
- **D-12:** POSIX write mode becomes `O_WRONLY | O_CREAT | O_TRUNC` so overwrite semantics match the Windows `stream_file::create` path (saver overwrites existing files identically on both platforms).
- **D-13:** POSIX destructor uses a single close path: `file_.close(ec_)` only. Drop the extra `::close(fd_)` — `stream_descriptor::close()` already closes the underlying descriptor; the second close is a latent EBADF/double-close.
- **D-14:** Logger tags: both halves keep `createLogger("LocalFileCommon")` — identical tag to today, no log-filtering change for consumers.

### Neutral header contents (PLAT-04)
- **D-15:** `include/LocalFileCommon.hpp` = shared declarations (doc banner, `namespace sgns` with its `using namespace boost::asio`, the `outcome` re-export block) + the `#include ASIOMGR_LOCALFILE_HEADER` selector line. It remains the single public include and is the file that satisfies PLAT-04 (platform-neutral, no `#ifdef _WIN32`).
- **D-16:** Each platform header fully declares its own `LocalFileDevice` — including its platform-specific `getFile()` return type (`posix::stream_descriptor&` vs `stream_file&`) and private members. Self-contained halves; no split-across-files class declaration.

### Zero-#ifdef enforcement
- **D-17:** Enforcement = phase-end grep gate (same style as Phase 1's zero-mnn gate) PLUS a permanent ctest (e.g. `no_ifdef_test`) that scans the local-file sources at test runtime and fails on `#ifdef`, `#ifndef _WIN32`, or `#if defined(_WIN32)`. The permanent guard protects future phases and contributors from regressing the boss requirement. Note: `#pragma once` and a header's own-name include guard are NOT violations — only platform branching is.
- **D-18:** Scan scope = exactly the five local-file files: `include/LocalFileCommon.hpp`, `include/LocalFileCommon.win.hpp`, `include/LocalFileCommon.posix.hpp`, `src/LocalFileCommon.win.cpp`, `src/LocalFileCommon.posix.cpp`. Whole-library `#ifdef` cleanup is out of phase scope (other devices still branch; untouched).

### Claude's Discretion
- Exact CMake block placement/syntax in `src/CMakeLists.txt` (variable-per-platform vs `if(WIN32) list(APPEND ...)` inline) as long as selection is `if(WIN32)/elseif(UNIX)` and the definition reaches every target that compiles `LocalFileCommon.hpp`.
- Whether `LocalFileCommon.cpp` is deleted outright or reduced to a comment stub pointing at the platform files (deletion preferred if nothing shared remains to compile).
- The `no_ifdef_test` implementation details (registered via `addtest()`, file paths resolved relative to source dir via `target_compile_definitions` path defines — mirroring `TEST_CERT_DIR` pattern).
- POSIX `::open` mode argument (`0666`) and any umask notes — kept as today unless structurally wrong.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project planning
- `.planning/ROADMAP.md` — Phase 2 goal, 5 success criteria (file existence, zero `#ifdef`, CMake-only selection, neutral shared header, green Windows suite), PLAT-01..04 mapping
- `.planning/REQUIREMENTS.md` — Platform Split section: PLAT-01 (win pair, zero #ifdef), PLAT-02 (posix pair, zero #ifdef), PLAT-03 (CMake `if(WIN32)/elseif(UNIX)` selection), PLAT-04 (shared decls in platform-neutral header)
- `.planning/PROJECT.md` — Constraints: no platform `#ifdef`s in local-file layer (boss requirement); POSIX structurally-correct-only (no Linux CI); Windows build+tests as the verification gate

### Prior phase context
- `.planning/phases/01-localfile-rename-mnn-code-purge/01-CONTEXT.md` — D-02 (class keeps `LocalFileDevice` name through the split — no rename churn in Phase 2); triad naming context

### Codebase maps (predate Phase 1 rename — verify against live sources)
- `.planning/codebase/CONVENTIONS.md` — Allman style, padded parens, 4-space indent, triad file naming, trailing-underscore preference for new members
- `.planning/codebase/ARCHITECTURE.md` — Device-layer position of `FILEDevice`/`LocalFileDevice` in the layer stack; registry dispatch context
- `.planning/codebase/TESTING.md` — `addtest()` registration pattern, `TEST_CERT_DIR` path-define pattern for the new `no_ifdef_test`, Windows ctest invocation

### Key source touchpoints (verified live during this discussion)
- `include/LocalFileCommon.hpp` — current dual-declaration file to split; both class variants + the `#ifndef _WIN32` blocks
- `src/LocalFileCommon.cpp` — current dual-implementation file to split; POSIX `::open`/`assign(fd)` path and Windows `stream_file::open` path
- `src/LocalFileLoader.cpp` / `src/LocalFileSaver.cpp` — the only construction sites (`std::make_shared<LocalFileDevice>`); their `#include "LocalFileCommon.hpp"` lines must keep working unchanged
- `src/CMakeLists.txt` — `add_library(AsyncIOManager STATIC LocalFileCommon.cpp ...)` source list where platform selection lands

No external specs/ADRs exist — requirements fully captured in ROADMAP.md, REQUIREMENTS.md, PROJECT.md, and decisions above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `include/asiomgr-logger.hpp` — `sgns::asiomgr::createLogger(tag)` stays the per-class logger pattern for both halves (D-14 shared tag)
- `test/src/CMakeLists.txt` `TEST_CERT_DIR` compile-definition pattern — the mechanism `no_ifdef_test` should reuse to locate the local-file sources at runtime
- `cmake/functions.cmake` `addtest()` — the only sanctioned way to register the new guard test

### Established Patterns
- Per-protocol `*Common` device layer with `enable_shared_from_this` — the split preserves the class shape exactly, only relocating declarations/definitions
- CMake `target_compile_definitions(... PRIVATE "NAME=\"value\"")` quoting convention (MSVC-friendly) — required for the `ASIOMGR_LOCALFILE_HEADER` string define
- `boost::system::error_code` return from `Open()`; errors logged via member logger — unchanged in both halves

### Integration Points
- `src/CMakeLists.txt` `add_library` source list: `LocalFileCommon.cpp` entry is replaced by the CMake-selected platform `.cpp`; the `ASIOMGR_LOCALFILE_HEADER` definition is added on the library target (and any test target that includes the umbrella — `localfile_loader_test`/`localfile_saver_test` do via the loader/saver headers)
- Root `CMakeLists.txt` `install include/*.h*` glob — the new `LocalFileCommon.win.hpp`/`.posix.hpp` files are automatically distributed; no install changes needed (D-09)
- `FileManager::InitializeSingletons()` — untouched; `LocalFileLoader`/`LocalFileSaver` registration and dispatch are unaffected by the device split

</code_context>

<specifics>
## Specific Ideas

- The umbrella's selector line is literally `#include ASIOMGR_LOCALFILE_HEADER` — the macro must be defined by CMake before any TU including the umbrella compiles; a `#error` guard when the macro is undefined is acceptable (it is not platform branching) but optional.
- The `no_ifdef_test` should read the five files as text at runtime (std::ifstream) and assert no line matches `#ifdef`, `#ifndef _WIN32`, `#if defined(_WIN32)` — with the explicit carve-out that `#pragma once` / own-name include guards don't count. Exact pattern list at planner's discretion (D-17 intent: catch platform branching).
- Windows `Open()` keeps the exact `read_only` / `write_only|create` flag mapping it has today — do not "improve" it.

</specifics>

<deferred>
## Deferred Ideas

- Whole-library `#ifdef` cleanup (IPFS/HTTP/WS/SFTP device headers still contain platform or feature branching) — future hardening work, explicitly out of scope (D-18).
- Unifying both platforms on `boost::asio::stream_file` (which supports POSIX since Boost 1.70) — rejected for this phase (D-11); could be revisited if the POSIX half proves problematic on real Linux CI.
- Linux CI to actually compile/execute the POSIX half — PROJECT.md Out of Scope for this milestone; structural verification only.
- Parse-parameter removal touching the same loader/saver call sites — Phase 3 by design.

None of these block Phase 2.

</deferred>

---

*Phase: 2-LocalFileCommon Platform Split*
*Context gathered: 2026-09-03*
