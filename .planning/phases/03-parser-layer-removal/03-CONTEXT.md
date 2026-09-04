# Phase 3: Parser Layer Removal - Context

**Gathered:** 2026-09-04
**Status:** Ready for planning

<domain>
## Phase Boundary

The public API carries no parse concept anywhere — `include/FileParser.hpp` is deleted, `RegisterParser`/`ParseData`/the `parsers` map are removed from `FileManager`, the `parse` bool is gone from every signature and callback (`FileLoader::LoadASync`, `FileManager::LoadASync`/`LoadFile`, `CompletionCallback`, `HTTPLoader`/`IPFSLoader`/`SFTPLoader`/`WSLoader`/`LocalFileLoader` and every `*Device` class), the live `save`-driven auto-save-after-load flow keeps working (now covered by a real test), and the Windows build + full test suite stay green.

**In scope:** PARSE-01 (delete `FileParser.hpp`, `RegisterParser`, `ParseData`, `parsers` map), PARSE-02 (strip `parse` bool from every API/callback/call site — headers, sources, tests, the one example call line), PARSE-03 (`save` bool stays; auto-save chain preserved), PARSE-04 (no behavioral change beyond signature cleanup), plus the in-phase guard for the unchecked `savers.find(savetype)` deref inside the auto-save wrapper (D-20/D-21) and a new auto-save-chain test (D-19).

**Out of scope (by roadmap design):** removing the `save` bool (explicitly kept per PROJECT.md); CMake MNN purge, `.mnn` fixture replacement, example rewrite, root `MNNExample.cpp` deletion (all Phase 4); activating dormant SFTP/WS loaders (v2 PROTO-01/02 — their signatures still get the parse drop since they compile in the library); Linux CI; any `#ifdef` work (Phase 2 is done).

</domain>

<decisions>
## Implementation Decisions

### Auto-save chain test (success criterion 3 — no test exists today)
- **D-19:** New test in `filemanager_test` (not a new target, not `localfile_loader_test`) exercising the full `FileManager::LoadASync` auto-save wrapper on the **file:// chain only**: load a `TempFile` via `file://` with `save=true`, `savetype="file"` → `LocalFileSaver` writes the loaded buffers to disk. No ipfs:// variant (network-gated BitswapNode fixtures are out of scope for this criterion).
  - **Mechanics:** RAII swap of `std::filesystem::current_path()` into a `TempDir` (restore in destructor — gtest runs serially in-process), because `FileManager::LoadASync` hardcodes an empty target filename and `LocalFileSaver` then generates a random UUID directory **in the process CWD**. Do NOT change that hardcoded `""` — empty target means "saver decides location" and is essential for the ipfs CID flow; PARSE-04 forbids behavior change.
  - **Latch discipline:** `finalcall` fires at load completion, BEFORE the save completes — the test must poll the filesystem for the UUID subdir (e.g. first entry in TempDir matching the UUID pattern containing the original filename), never just the callback flag.
  - **Assert depth:** existence AND content — read back the written file and `EXPECT_EQ` its bytes against the source content (proves the loaded buffers actually flowed through `SaveASync`). No outstanding-operations counter assertion required.
- **D-20:** While editing the auto-save wrapper (the exact lambda losing its `parse` param), fix the latent UB: `savers.find(savetype)` result is dereferenced unchecked (`saverIter->second`). Add the end() guard: on miss → log error via `m_logger` + `DecrementOutstandingOperations(ioc)` + deliver `outcome::failure` to `finalcall` — the established async-error pattern (never throw across the async boundary). Precedent: Phase 2 D-12/D-13 latent-bug fixes in-phase.
- **D-21:** No dedicated new test for the D-20 guard (keep phase lean); the always-on suite's green run covers the registered-savetype path via D-19.

### Example call sites
- **D-22:** One-line edit only: drop the `parse` argument from the single `LoadASync(..., false, true, ioc, ...)` call in `example/MNNExample.cpp` so roadmap criterion 2 ("every call site (tests + example) compile without a parse parameter") stays literally true. No deeper example work — Phase 4 rewrites it entirely. (The stale root `MNNExample.cpp` is also Phase 4's to delete.)

### Phase-end verification gate
- **D-23:** Phase-end grep gate over `include/`, `src/`, `test/` for the dead concept's exact signatures: `bool parse` (any spacing), `parse_`, `FileParser`, `ParseData`, `RegisterParser`, `parsers[`, `parsers.find`. A raw `parse` grep is unusable — legit survivors: `parseHTTPUrl`/`parseSFTPUrl`/`parseIPFSUrl` (URL utilities, untouched), vendored `httplib.h` (`multipart_form_data_parser.parse`), "Parse hostname and path" comments. The narrow pattern list avoids all of them by construction.
- **D-24:** Gate = narrow grep (D-23) + Windows build green + full ctest green. NO permanent runtime test (no `no_parse_test` à la `no_ifdef_test`) — signature params can't silently regress the way `#ifdef` blocks can; compile breakage is the guard.

### Claude's Discretion
- Doc-comment scrub mechanics: every `@param parse - Whether to parse file upon completion` line dies with its parameter; reflow of adjacent doc text at executor's judgment.
- Whether `FileManager::LoadFile` keeps a default argument or drops to `(url)` — the `parse` param is simply deleted; overload/virtual-signature details follow from `FileLoader`.
- Order of edits (headers-first vs. leaf devices-first) and commit slicing, provided the phase ends green.
- Exact error value used in the D-20 failure delivery (reuse an existing `FileManager` error path or post a generic failure) as long as it never throws across the async boundary.
- The D-19 poll helper shape (reuse `pollUntil` with a filesystem predicate vs. small bespoke loop).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project planning
- `.planning/ROADMAP.md` — Phase 3 goal + 5 success criteria (FileParser deletion, parse-free callbacks incl. tests+example, auto-save chain test, no behavioral change, green Windows suite); PARSE-01..04 mapping
- `.planning/REQUIREMENTS.md` — Parser Layer Removal section: PARSE-01..04 definitions; "Out of Scope" table (save bool stays; no shims)
- `.planning/PROJECT.md` — Constraints: breaking API accepted, no shims; "save flag flow" context paragraph (LoadASync wraps loader callback; save=true triggers auto-save — live behavior that stays); Key Decisions row "Remove parse bool + entire parser layer; keep save bool"

### Prior phase context
- `.planning/phases/01-localfile-rename-mnn-code-purge/01-CONTEXT.md` — D-04/D-05: the dead `if (parse)` mnn-lookup block was already deleted in Phase 1; everything else parse-related was deferred wholesale to Phase 3 (this phase)
- `.planning/phases/02-localfilecommon-platform-split/02-CONTEXT.md` — D-12/D-13 in-phase latent-bug-fix precedent (basis for D-20)

### Codebase maps (predate Phase 1 rename — verify against live sources)
- `.planning/codebase/CONVENTIONS.md` — Allman style, padded parens, doc-comment conventions for the @param scrub
- `.planning/codebase/TESTING.md` — `addtest()` registration, `IOContextRunner` + `pollUntil` async recipe, JUnit/ctest invocation on Windows
- `.planning/codebase/ARCHITECTURE.md` — Callback tiers (`CompletionCallback` vs `FinalCallback`), registry dispatch context

### Key source touchpoints (verified live during this discussion)
- `src/FileManager.cpp:63-96` — `LoadASync` wrapper: `handle_read` lambda (drops `parse` param), the save branch (`savers.find(savetype)` UB guard lands here), `loader->LoadASync(filePath, parse, save, ...)` call
- `src/FileManager.cpp:142-167` — `LoadFile(url, parse)` (param deleted, `if (parse) ParseData(...)` branch deleted) and `ParseData` method (deleted entirely)
- `include/FileManager.hpp:41-92` — `parsers` map member, `RegisterParser` declaration, `CompletionCallback` alias (drops `bool parse`), `LoadASync`/`LoadFile` declarations, `ParseData` declaration, `#include "FileParser.hpp"`
- `include/FileParser.hpp` — the interface file to delete
- `include/FileLoader.hpp` — base-class `CompletionCallback` alias + `LoadASync` pure virtual (drops `parse`)
- Per-protocol headers with `parse` in signatures/callbacks/docs: `include/HTTPLoader.hpp`, `include/LocalFileLoader.hpp`, `include/IPFSLoader.hpp`, `include/SFTPLoader.hpp`, `include/WSLoader.hpp`, `include/HTTPCommon.hpp` (+ `parse_` member), `include/IPFSCommon.hpp` (~10 methods), `include/SFTPCommon.hpp` (+ `parse_`), `include/WSCommon.hpp` (+ `parse_`)
- Corresponding `.cpp` echo sites: `src/HTTPCommon.cpp:250` (`self->parse_`), `src/SFTPCommon.cpp:400` (`parse_`), `src/WSCommon.cpp:161` (`self->parse_`), IPFS threading (`src/IPFSCommon.cpp` — ~15 captures/calls), loader ctors/devices in `src/HTTPLoader.cpp:61`, `src/WSLoader.cpp:66`, `src/SFTPLoader.cpp:77`, `src/LocalFileLoader.cpp:58-95`
- Test call sites (all `false, false` today): `test/src/localfile_loader_test.cpp:57,92`, `test/src/http_loader_test.cpp:186,222,247`, `test/src/ipfs_loader_test.cpp:150,221,293,321`, `test/src/ipfs_device_test.cpp:85,123,184`, `test/src/filemanager_test.cpp:127` (+ new D-19 test lands here)
- `example/MNNExample.cpp:267-272` — the one example call line (D-22)
- `src/LocalFileSaver.cpp:69-75` — the UUID-dir-in-CWD behavior the D-19 test must accommodate (not change)

No external specs/ADRs exist — requirements fully captured in ROADMAP.md, REQUIREMENTS.md, PROJECT.md, and decisions above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `test/testutil/temp_file.hpp` (`TempFile`/`TempDir`) + `asio_helpers.hpp` (`IOContextRunner`/`pollUntil`) — the D-19 test harness; `pollUntil` takes any predicate, so a filesystem check (UUID subdir exists containing expected filename) drops straight in
- `FileManagerTestFixture` (`test/testutil/test_fixture.hpp`) — already derives the `filemanager_test` fixture; `makeSingleFileResult` available if needed
- Phase 1/2 grep-gate pattern — D-23 mirrors the zero-mnn and zero-#ifdef phase-end gates (documented in the respective PLAN/VERIFICATION files)

### Established Patterns
- Errors are posted onto the io_context as `outcome::failure`, never thrown across the async boundary — D-20's guard follows this exactly
- `IncrementOutstandingOperations()` before dispatch, `DecrementOutstandingOperations(ioc)` in EVERY completion path (including error paths) — D-20's guard must decrement before delivering failure or the io_context never drains
- Devices store the flags as members (`parse_`, `save_`) and echo them into `handle_read(ioc, data, parse_, save_)` — the member `parse_` and its echo arg simply die
- Doc comments carry `@param parse - Whether to parse file upon completion` above every affected signature — scrubbed together with the param (also feeds the D-23 grep)

### Integration Points
- `FileManager::LoadASync`'s `handle_read` wrapper is the auto-save orchestrator: `save=true` → `savers[savetype]->SaveASync(ioc, handle_write, "", buffers, suffix)` → `handle_write` decrements the counter → only then does `ioc` drain. The D-19 test validates this whole chain end-to-end for the first time.
- `LocalFileSaver::SaveASync` with empty filename → `boost::uuids::random_generator()` directory + original filenames inside — the on-disk shape the D-19 filesystem poll expects (`<TempDir>/<uuid>/<original-filename>`)
- `FinalCallback` (`finalcall`) fires before save completes — tests must not treat it as the save-done signal

</code_context>

<specifics>
## Specific Ideas

- The D-19 test name should say what it proves, e.g. `LoadASync_SaveTrueAutoSavesLoadedDataToDisk` — placed in `FileManagerIntegrationTest` alongside the other dispatch-contract tests.
- The D-20 guard's failure delivery should mirror existing error paths verbatim in shape: log via `m_logger->error(...)`, decrement, `finalcall(outcome::failure(...))`.
- `#include "FileParser.hpp"` in `include/FileManager.hpp` is deleted together with the map — no other file includes `FileParser.hpp` (verified by grep during discussion).
- Keep the D-23 grep list exactly: `bool parse`, `parse_`, `FileParser`, `ParseData`, `RegisterParser`, `parsers[`, `parsers.find` — no broader `parse` matching (breaks on URLStringUtil + httplib.h).

</specifics>

<deferred>
## Deferred Ideas

- ipfs:// auto-save chain variant (BitswapNode server/client, savetype="ipfs") — heavier network-gated coverage; not required by criterion 3, candidate for v2 hardening.
- Permanent runtime parse-gate test (`no_parse_test`) — rejected (D-24): compile breakage is the guard for signature params.
- Deeper example modernization, root `MNNExample.cpp` deletion, `.mnn` fixtures — Phase 4 by design.
- Activating dormant `SFTPLoader`/`WSLoader` (their parse-drop happens here because they compile in the library; activation itself is v2 PROTO-01/02).
- Removing the `save` bool — explicitly out of scope per PROJECT.md; user wants it kept.

None of these block Phase 3.

</deferred>

---

*Phase: 3-Parser Layer Removal*
*Context gathered: 2026-09-04*
