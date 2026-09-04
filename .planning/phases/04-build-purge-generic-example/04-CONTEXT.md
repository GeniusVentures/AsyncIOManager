# Phase 4: Build Purge & Generic Example - Context

**Gathered:** 2026-09-04
**Status:** Ready for planning

<domain>
## Phase Boundary

The build system carries zero MNN dependency — `find_package(MNN CONFIG REQUIRED)` + `include_directories(${MNN_INCLUDE_DIR})` gone from root `CMakeLists.txt`, the `FILE(GLOB MNN_LIBS ...)` gone from `example/CMakeLists.txt`, and the super-build MNN block gone from `build/CommonBuildParameters.cmake` — and a generic `FileExample` demonstrates `file://` load and save through `FileManager` using generic binary fixtures. The stale root-level `MNNExample.cpp` legacy copy is deleted, `example/MNNExample.cpp` is replaced, `test/1.mnn`/`test/2.mnn` become generic `.bin` fixtures, and the Windows build + full test suite stay green with the MNN-free build system.

**In scope:** MNN-01 (CMake purge: root + `src` + `example` CMakeLists, plus the super-build `CommonBuildParameters.cmake` MNN block), MNN-03 (`.mnn` fixtures → generic `.bin`), MNN-06 (example contains no MNN inference code or library references), BUILD-01 (`MNNExample` replaced by generic `FileExample` demonstrating `file://` load+save), the `SaveASync_MnnPrefixThrows` test neutralization (`mnn://` → `foo://`), and root `MNNExample.cpp` deletion.

**Out of scope (by roadmap design):** Phase 5's repo-wide zero-mnn grep gate, clean-tree end-to-end build verification, TEST-03/TEST-04/TEST-BUILD-02 acceptance (Phase 5); activating dormant SFTP/WS loaders (v2); ipfs:// example coverage (dropped with the minimal-example decision — see deferred); Linux CI; any library source-code changes (Phases 1–3 already scrubbed `include/`/`src/`/`test/` sources).

</domain>

<decisions>
## Implementation Decisions

### Example scope & shape
- **D-25:** The generic example is **minimal, `file://`-only** (~50-80 lines): `FileManager::GetInstance().InitializeSingletons()` → `LoadASync(file://...)` → `SaveASync(file://...)` → run the io_context. **No libp2p, no bitswap, no soralog** — none of it is needed for `file://`. The old `example/MNNExample.cpp` (291 lines, ~250 of which are libp2p/bitswap/soralog scaffolding for the ipfs:// demo) is deleted outright, not slimmed. No MNN inference code or library references anywhere (MNN-06).
- **D-26:** Example wiring stays **as-is**: `example/CMakeLists.txt` gets its own `add_executable(FileExample ...)` but the root `CMakeLists.txt` does NOT gain `add_subdirectory(example)` — the example remains super-build-only (built via `BUILD_EXAMPLES` in super-build mode), exactly how `MNNExample` was wired. Zero impact on the standard Windows configure/build/test verification loop.
- **D-27:** The example demonstrates a **load → save roundtrip**: take an input path argument (default `file://example_data.bin`), load it via `LoadASync`, then save the loaded buffers to a second path (e.g. `file://example_output.bin`) via `SaveASync`, reporting byte counts in the completion callbacks. This exercises both public APIs a consumer actually calls. It does NOT demo the `save=true` auto-save-after-load flow (D-19 chain) — that stays covered by `filemanager_test`; coupling the example to the UUID-dir-in-CWD saver behavior was rejected.
- **D-28:** The example's default input is a **committed ~1KB binary**: `example/example_data.bin`, opaque varied bytes, produced once with a trivial command and committed (no generator script, no CMake-time generation). The example runs out-of-the-box with zero arguments.

### Fixture replacement
- **D-29:** `test/1.mnn` (1.1MB) and `test/2.mnn` (3.8MB) are deleted. They are referenced by **zero test sources** (verified live — unit tests use `TempFile("arbitrary string content")` because loaders treat files as opaque byte blobs; only the examples bake in `file://../test/1.mnn`). Replaced by small committed `.bin` fixtures (~1KB, pre-made bytes committed directly), satisfying MNN-03's "generic binary fixtures (e.g. `test/*.bin`)" literally and dropping ~5MB from the repo.
- **D-30:** Fixture locations — **both directories**: `example/example_data.bin` (the example's default input, D-28) and `test/fixture.bin` (a general-purpose committed binary available for any current or future test that wants one; today's tests don't consume it, but MNN-03's spirit is a fixture living under `test/`). Slight duplication is accepted over coupling `example/` to `test/` directory structure.

### Super-build MNN block
- **D-31:** The MNN block in `build/CommonBuildParameters.cmake` (lines 274-278: `MNN_INCLUDE_DIR`/`MNN_LIBRARY_DIR` sets + global `include_directories(${MNN_INCLUDE_DIR})`) is **purged in this phase**, alongside the CMakeLists purge. It exists solely to feed this repo's now-removed MNN dependency (it is the source of `MNN_LIBRARY_DIR` consumed by the example's glob). Leaving it would make Phase 5's repo-wide `grep -ri mnn` fail on this file — purging now keeps "MNN becomes consumers' concern" true everywhere in the repo.

### mnn:// rejection test fate
- **D-32:** `filemanager_test.cpp:124` `SaveASync_MnnPrefixThrows` is **neutralized in this phase**: the URL `"mnn://some/path"` becomes a neutral unregistered prefix (e.g. `"foo://some/path"`) and the test name drops the MNN reference. The dispatch contract (unknown prefix → `std::range_error`) stays covered with zero behavioral change; the literal `mnn` string disappears now rather than as a Phase-5 grep-gate surprise. The test is kept (not deleted) — it remains a second data point beside the adjacent `SaveASync_UnregisteredPrefixThrows`.

### Claude's Discretion
- Exact example source structure (single `main` vs. small helper functions), console output wording, and error-reporting style in callbacks — provided both `LoadASync` and `SaveASync` are demonstrated and completion paths report success/failure.
- The `.bin` fixtures' exact byte content (~1KB varied opaque bytes; anything non-uniform is fine) and the exact generation command used once to produce them.
- Whether `example/CMakeLists.txt` keeps `add_dependencies`/link shape verbatim beyond the rename `MNNExample` → `FileExample` and the `MNN_LIBS` glob removal.
- The neutral prefix chosen for D-32 (`foo://`, `unknown://`, etc.) and the renamed test's exact name, provided it carries no `mnn` substring.
- Order of edits (CMake-first vs. example-first) and commit slicing, provided the phase ends green.
- Whether `example/README.md`-style usage notes are added (none exist today; optional).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project planning
- `.planning/ROADMAP.md` — Phase 4 goal + 4 success criteria (zero MNN in CMakeLists incl. grep, generic example target + root `MNNExample.cpp` removal, `.mnn` → generic fixtures, green Windows suite with MNN-free build); MNN-01/MNN-03/MNN-06/BUILD-01 mapping; Phase 5 boundary (repo-wide grep is Phase 5's gate — this phase pre-clears the known stragglers)
- `.planning/REQUIREMENTS.md` — MNN Removal section (MNN-01, MNN-03, MNN-06) and Build & Tests section (BUILD-01); "Out of Scope" table (MNN inference → consumers)
- `.planning/PROJECT.md` — Constraints: "MNN must no longer appear in any `find_package`/link/include — MNN becomes consumers' concern"; Key Decisions "Full MNN purge in one milestone"

### Prior phase context
- `.planning/phases/01-localfile-rename-mnn-code-purge/01-CONTEXT.md` — D-05 boundary: CMake MNN purge deliberately deferred to Phase 4 (library never links MNN); deferred-ideas list naming this phase's work
- `.planning/phases/03-parser-layer-removal/03-CONTEXT.md` — D-22: the example received only the one-line parse-arg fix; full rewrite is this phase's. The post-parse API shape the new example compiles against: `LoadASync(url, save, ioc, finalcall, savetype)` / `SaveASync(url, data, ioc, completion)`
- `.planning/phases/02-localfilecommon-platform-split/02-CONTEXT.md` — phase-end grep-gate pattern this phase's MNN-CMake grep mirrors (scoped to `CMakeLists.txt` files per roadmap criterion 1)

### Codebase maps (predate Phase 1 rename — verify against live sources)
- `.planning/codebase/STACK.md` — Build System section: standalone vs. super-build modes; `build/CommonBuildParameters.cmake` role (MNN block at lines 274-278); `example/` referenced via `BUILD_EXAMPLES`
- `.planning/codebase/TESTING.md` — async test recipe (`IOContextRunner` + `pollUntil`) the example's ioc/run pattern mirrors; `addtest()` registration (not needed this phase — no new tests)

### Key source touchpoints (verified live during this discussion)
- `CMakeLists.txt:34-35` — `find_package(MNN CONFIG REQUIRED)` + `include_directories(${MNN_INCLUDE_DIR})` to delete
- `example/CMakeLists.txt:2` — `FILE(GLOB MNN_LIBS "${MNN_LIBRARY_DIR}/*")` to delete; `add_executable(MNNExample MNNExample.cpp)` → `FileExample`; `add_dependencies`/`target_link_libraries` rename
- `build/CommonBuildParameters.cmake:274-278` — super-build MNN block to delete (D-31)
- `example/MNNExample.cpp` — 291-line file to delete-and-replace (D-25); the `LoadASync` call shape at lines 267-272 (post-Phase-3 signature) is the API the new example uses
- `MNNExample.cpp` (repo root, 117 lines) — stale legacy copy, built by nothing; delete (roadmap criterion 2)
- `test/1.mnn`, `test/2.mnn` — fixtures to delete (D-29)
- `test/src/filemanager_test.cpp:124-133` — `SaveASync_MnnPrefixThrows` to neutralize (D-32)
- `example/MNNExample.cpp:57` / root `MNNExample.cpp` usage strings (`file://../test/1.mnn`) — the only `.mnn` path references anywhere; both files are deleted/replaced wholesale

No external specs/ADRs exist — requirements fully captured in ROADMAP.md, REQUIREMENTS.md, PROJECT.md, and decisions above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `example/MNNExample.cpp:267-272` — the post-Phase-3 `LoadASync(file_names[i], true, ioc, callback, "ipfs")` call shape: the closest existing template for the new example's load call (swap `"ipfs"` → `"file"` semantics, drop the save=true flag per D-27's explicit two-call roundtrip)
- Work-guard + `ioc->run()` pattern (`example/MNNExample.cpp:208-210`, `289`; `test/testutil/asio_helpers.hpp` `IOContextRunner`) — how the new example keeps the io_context alive and drains it
- `test/src/filemanager_test.cpp` adjacent tests (`SaveASync_UnregisteredPrefixThrows` at ~line 83, `SaveASync_MnnPrefixThrows` at 124) — the neutralization (D-32) is a prefix-string + name swap in place; fixture and structure untouched

### Established Patterns
- Errors arrive in callbacks as `outcome::result` failures — the new example's callbacks check `buffers`/result and print `error().message()`, never throw
- `IncrementOutstandingOperations()`/`DecrementOutstandingOperations()` drain discipline is internal to `FileManager` — the example just runs the ioc until work completes
- CMake edits are pure deletions plus one rename — no new find_package, no link changes beyond removing MNN entries (library targets untouched: `src/CMakeLists.txt` has zero MNN references today)

### Integration Points
- Root `CMakeLists.txt` install/export flow (`install include/*.h*`, `AsyncIOManagerTargets`) — unaffected; no headers added/removed this phase
- Super-build (`build/{Windows,Linux,OSX}/CMakeLists.txt`) includes `CommonBuildParameters.cmake` — the D-31 deletion must leave that file syntactically valid for all three platform wrappers
- `BUILD_EXAMPLES` super-build hook references `example/` — the renamed `FileExample` target name must stay consistent with whatever the super-build expects (verify how `BUILD_EXAMPLES` consumes `example/CMakeLists.txt` during planning)

</code_context>

<specifics>
## Specific Ideas

- The new example's default input path should be relative-CWD (`file://example_data.bin`), matching how the old example's default worked (`file://../test/1.mnn` was relative to the example's run location) — document in the usage string that the default expects `example_data.bin` next to the executable's CWD.
- The roundtrip's save target should be a distinct literal (e.g. `file://example_output.bin`) so a successful run leaves a visible artifact the user can diff/inspect.
- The committed `.bin` content should be non-uniform varied bytes (not all-zeros) so byte-count/roundtrip checks are meaningful.
- Keep the D-32 rename mechanical: same `EXPECT_THROW` with `std::range_error`, same fixture, only the URL prefix and test name change.

</specifics>

<deferred>
## Deferred Ideas

- ipfs:// example coverage (libp2p host + bitswap provider demo that died with the old `MNNExample`) — the scaffolding was deleted per D-25; if wanted, it returns as a dedicated v2 example under `example/`, not by re-fattening `FileExample`.
- Root-build wiring for the example (`add_subdirectory(example)` behind `BUILD_EXAMPLES` option) — rejected (D-26); revisit only if the example starts rotting from never being compiled in CI.
- Repo-wide zero-mnn grep gate, clean-tree end-to-end build, TEST-03/04/BUILD-02 — Phase 5 by design (this phase pre-clears the known stragglers: super-build block D-31, mnn:// test D-32).
- README updates describing the new example — optional at executor's discretion; not a phase criterion.

None of these block Phase 4.

</deferred>

---

*Phase: 4-Build Purge & Generic Example*
*Context gathered: 2026-09-04*
