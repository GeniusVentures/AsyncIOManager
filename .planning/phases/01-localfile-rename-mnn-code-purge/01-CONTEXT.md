# Phase 1: LocalFile Rename & MNN Code Purge - Context

**Gathered:** 2026-09-03
**Status:** Ready for planning

<domain>
## Phase Boundary

Local file load/save is delivered end-to-end by renamed `LocalFileLoader`/`LocalFileSaver` (registering for `file://` only, `mnn://` save prefix removed), the `FILECommon` device layer is renamed to `LocalFileCommon`, every MNN code file is deleted, MNN doc comments are scrubbed, and the renamed test suite passes on Windows with `BUILD_TESTING=ON`.

**In scope:** rename of local-file classes/files (`MNNLoader`→`LocalFileLoader`, `MNNSaver`→`LocalFileSaver`, `FILECommon.*`→`LocalFileCommon.*`), deletion of MNN code files (MNN-02), removal of the `mnn` save-prefix registration (MNN-04), doc-comment scrub for zero `mnn` grep over `include/ src/ test/` (MNN-05), test renames + dispatch updates (TEST-01, TEST-02).

**Out of scope (by roadmap design):** CMake MNN purge — `find_package(MNN)` etc. stays until Phase 4; platform split of `LocalFileCommon` (Phase 2); parse-parameter/FileParser removal (Phase 3); example rewrite and `.mnn` fixture replacement (Phase 4); Linux CI (structural-only POSIX verification).

</domain>

<decisions>
## Implementation Decisions

### FILEDevice class naming
- **D-01:** Rename the device class `FILEDevice` → `LocalFileDevice` in Phase 1 (not just the files). Full triad consistency: `LocalFileLoader` / `LocalFileSaver` / `LocalFileCommon` / `LocalFileDevice`. All construction sites (`std::make_shared<FILEDevice>` in the two renamed loader/saver .cpp files) update in-phase.
- **D-02:** The `LocalFileDevice` name carries into Phase 2's platform split — Phase 2 splits files only (`LocalFileCommon.win.*` / `LocalFileCommon.posix.*`), with the class keeping the `LocalFileDevice` name in both halves. No rename churn mixed into the split.
- **D-03:** While renaming, scrub ALL `FILECommon` string mentions for full consistency: doc banner comments (`/** Header file for the FILECommon */` → `LocalFileCommon`) and logger tags (`createLogger("FILECommon")` → `createLogger("LocalFileCommon")`).

### Dead parse block in FileManager.cpp
- **D-04:** Delete the entire dead `if (parse)` body inside the `handle_read` lambda (`src/FileManager.cpp:74-79` — the `parsers.find("mnn")` lookup with its commented-out `ParseASync` call). It contains the only live `mnn` string in the library and blocks success criterion 5's zero-grep.
- **D-05:** Phase boundary confirmed: everything else parse-related stays untouched in Phase 1 — the `parse` bool parameter, `include/FileParser.hpp`, the `parsers` map, and `RegisterParser` all remain until Phase 3 removes them wholesale. Phase 1 deletes only the hardcoded `"mnn"` lookup block.

### mnn:// rejection test placement
- **D-06:** The `mnn://` save rejection test lives in `filemanager_test` (not `localfile_saver_test`) — the dispatch/registry contract is FileManager's concern, and `filemanager_test` already owns `SaveASync_UnregisteredPrefixThrows` coverage. `localfile_saver_test` stays purely about file I/O behavior (sync save, async save, null-data error).
- **D-07:** The new test covers the **async path only** — a `SaveASync("mnn://...")` throwing `std::range_error` ("No saver registered for prefix mnn"), mirroring the existing `SaveASync_UnregisteredPrefixThrows` shape. It is the exact variant named in success criterion 1.

### Claude's Discretion
- Exact git-rename mechanics (single commit vs. staged rename-then-edit) — planner/executor choose.
- Test file header comment wording and fixture naming details inside the renamed tests.
- Order of edits within the phase (rename-first vs. delete-first), provided the phase ends green.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project planning
- `.planning/ROADMAP.md` — Phase 1 goal, success criteria (5 criteria incl. zero-`mnn` grep scope: `include/ src/ test/` only), and phase boundaries (CMake purge deferred to Phase 4 by design)
- `.planning/REQUIREMENTS.md` — FILE-01..04, MNN-02/04/05, TEST-01/02 definitions; "Out of Scope" table (no shims, `save` bool stays)
- `.planning/PROJECT.md` — Milestone constraints: breaking API accepted, no `#ifdef` in local-file layer (Phase 2), Windows build+tests as verification gate

### Codebase maps
- `.planning/codebase/CONVENTIONS.md` — Naming triad pattern, Allman style, padded parens, singleton self-registration pattern, `SINGLETON_PTR` macro usage, trailing-underscore member preference
- `.planning/codebase/TESTING.md` — `addtest()` registration, async test recipe (`IOContextRunner` + `pollUntil`), `FileManagerTestFixture` usage, Windows ctest invocation
- `.planning/codebase/ARCHITECTURE.md` — Registry dispatch table (registered prefixes per handler), `CompletionCallback`/`FinalCallback` tiers

### Key source touchpoints (verified during discussion)
- `src/FileManager.cpp:74-79` — the dead parse block to delete (D-04)
- `src/MNNSaver.cpp:37-41` — dual registration `RegisterSaver("file")` + `RegisterSaver("mnn")`; the `"mnn"` line dies (MNN-04)
- `src/MNNLoader.cpp` / `src/MNNSaver.cpp` — `FILEDevice` construction sites renamed per D-01
- `test/src/filemanager_test.cpp:83` — `SaveASync_UnregisteredPrefixThrows` pattern to mirror for the `mnn://` test (D-07)

No external specs/ADRs exist — requirements fully captured in ROADMAP.md, REQUIREMENTS.md, and decisions above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `test/testutil/test_fixture.hpp` — `FileManagerTestFixture` with `makeSingleFileResult`/`unwrapResult` helpers; the renamed tests keep deriving from it (rename `MNNLoaderTest`/`MNNSaverTest` subclasses → `LocalFileLoaderTest`/`LocalFileSaverTest`)
- `test/testutil/temp_file.hpp` (`TempFile`/`TempDir`) and `asio_helpers.hpp` (`IOContextRunner`/`pollUntil`) — the async test harness stays as-is
- `include/ASIOSingleton.hpp` — `SINGLETON_PTR` macro pattern; renamed classes keep the identical singleton structure (`InitializeSingleton()` + ctor self-registration)

### Established Patterns
- Per-protocol triad `<PROTO>Loader`/`<PROTO>Saver`/`<PROTO>Common` — the rename aligns local-file code with this pattern instead of inventing a new one
- Constructor self-registers into `FileManager` registry under URL prefix; `InitializeSingletons()` in `src/FileManager.cpp:33-42` must switch to `sgns::LocalFileLoader`/`sgns::LocalFileSaver` names (FILE-03)
- Errors delivered as `outcome::failure` posted onto the io_context; `OUTCOME_CPP_DEFINE_CATEGORY_3` error categories move with the renamed classes

### Integration Points
- `src/CMakeLists.txt` source list — `MNNLoader.cpp`/`MNNSaver.cpp`/`FILECommon.cpp` entries become `LocalFileLoader.cpp`/`LocalFileSaver.cpp`/`LocalFileCommon.cpp` (no MNN CMake removal — that's Phase 4)
- `test/src/CMakeLists.txt` — `addtest(mnn_loader_test ...)`/`addtest(mnn_saver_test ...)` become `localfile_loader_test`/`localfile_saver_test`
- `example/` and root `MNNExample.cpp` are NOT wired into the root build (no `add_subdirectory`) — renaming/deleting headers under `include/` cannot break the library build; example cleanup is Phase 4

</code_context>

<specifics>
## Specific Ideas

- The `mnn://` rejection test should mirror the existing `SaveASync_UnregisteredPrefixThrows` shape in `filemanager_test.cpp:83-98` — same fixture, same `EXPECT_THROW` with `std::range_error`, only the URL prefix changes to `mnn://`.
- Renamed-file header banners should read `/** Header file for the LocalFileLoader */` etc., matching the existing banner convention (`include/FileLoader.hpp` style).

</specifics>

<deferred>
## Deferred Ideas

- Genericize-or-remove the remaining parse machinery (`parse` bool, `FileParser.hpp`, `parsers` map) — Phase 3 by design (D-05 boundary).
- Platform split of `LocalFileCommon` into `.win.*`/`.posix.*` pairs — Phase 2; the `LocalFileDevice` class name carries over unchanged (D-02).
- CMake MNN purge (`find_package(MNN)`, `${MNN_INCLUDE_DIR}`, `MNN_LIBS` glob in `example/CMakeLists.txt`) — Phase 4, per roadmap decision (library never links MNN; `MNNCommon.hpp` flows only through `MNNLoader.hpp` which this phase deletes).
- `.mnn` fixture replacement and example rewrite — Phase 4.
- Activating dormant SFTP/WS loaders — v2 backlog (REQUIREMENTS.md PROTO-01/02).

</deferred>

---

*Phase: 1-LocalFile Rename & MNN Code Purge*
*Context gathered: 2026-09-03*
