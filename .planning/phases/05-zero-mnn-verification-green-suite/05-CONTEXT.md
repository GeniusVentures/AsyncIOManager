# Phase 5: Zero-MNN Verification & Green Suite - Context

**Gathered:** 2026-09-04
**Status:** Ready for planning

<domain>
## Phase Boundary

The milestone acceptance gate passes — the repo is verifiably MNN-free end to end and the complete always-on test suite is green on Windows in the Release configuration. Any grep stragglers inside the gate scope are fixed in this phase; the README is fully rewritten to describe the post-refactor library (protocol table, parse-free API, real build commands, real example); the zero-mnn grep + green ctest evidence is captured live and recorded in VERIFICATION.md.

**In scope:** TEST-03 (no test references `.mnn` files — confirm via live grep, no stragglers expected: the last ones died with Phase 4's fixture/example swap), TEST-04 (full always-on test suite builds and passes on Windows `BUILD_TESTING=ON`/`TESTING=ON`, ctest zero failures, **Release config**), BUILD-02 (repo verifiably MNN-free — `grep -ri mnn` over `include/`, `src/`, `test/`, `example/`, all `CMakeLists.txt` + cmake/cmake-wrapper files **plus README.md** returns zero matches), the full README.md rewrite (16 mnn hits, stale FileParser/parsers-map diagrams, fake MNNExample output, MNN dependency link — all replaced), and the phase-end verification: reconfigure + `--clean-first` rebuild + full ctest in Release.

**Out of scope (by roadmap design and discussion):** network-gated suites (`http_loader_test`, `sftp_saver_test`, `ipfs_loader_test/saver/device` behind `ASYNC_IO_MANAGER_NETWORK_TESTS=ON`) — gate is **default-gate-only**; a fresh-directory (virgin) configure — verification is **reconfigure + full rebuild** in the existing wrapper tree, not a new build dir; Linux/macOS builds; activating dormant SFTP/WS loaders (v2); `.claude/CLAUDE.md` and `.planning/codebase/*.md` staleness (they deliberately describe the MNN-era mapping commit); build-artifact hygiene / `.gitignore` (explicitly skipped/deferred — see deferred); any library source-code changes unless the grep gate finds stragglers.

</domain>

<decisions>
## Implementation Decisions

### Gate scope & evidence
- **D-33:** Green-suite gate = **default gate only**: the always-on suites registered when `TESTING=ON`/`BUILD_TESTING=ON` (`localfile_loader_test`, `localfile_saver_test`, `filemanager_test`, `no_ifdef_test` — currently 9 ctest entries counting parameterized registrations per prior-phase evidence). Network-gated suites are NOT verified in this phase and their (non-)execution is not evidence for or against the gate.
- **D-34:** Evidence capture = **live run + doc record**: the agent runs the gate live in-phase (wrapper reconfigure, full rebuild, ctest) and records pass/fail counts + the suite list in the phase VERIFICATION.md. NO new permanent test code, NO runtime mnn-grep guard test (mirrors D-24's rationale from Phase 3: the gate is a phase-end acceptance check, not a permanent ctest).
- **D-35:** Zero-mnn grep gate file set = **roadmap criterion-1 scope + README.md**: `include/`, `src/`, `test/`, `example/`, root `CMakeLists.txt`, all other `CMakeLists.txt` (`src/`, `test/src/`, `test/testutil/`, `example/`, `build/{Windows,Linux,OSX}/`), `cmake/*.cmake`/`.cmake`/`.CMake`, `build/CommonBuildParameters.cmake` — **plus `README.md`**, whose 16 stragglers are fixed by the rewrite (D-36), so it joins the gate. Live scan at discussion time already shows zero matches everywhere except README.md.
- **D-35b:** Excluded from the grep gate by design: `.planning/**` (historical phase docs legitimately mention MNN — it's the record of removing it), `.claude/CLAUDE.md` + `.planning/codebase/*.md` (map the pre-refactor commit), `.git/`, and any build output trees.

### README.md fate
- **D-36:** README.md gets a **full rewrite** (not a scrub): the FileParser/MNNParser class diagram, the `parsers` map + `RegisterParser`/`ParseData` entries in the FileManager diagram, the MNN GitHub dependency link, the fake `MNNExample 1.mnn` build log with inference output, and the "TODO: Need to update" Windows/macOS stubs are all replaced with post-refactor reality. The 16 `mnn` hits drop to zero as a consequence, feeding D-35's gate.
- **D-37:** Depth = **concise practical (~80–120 lines)**: what the library is (URL-dispatched async I/O over Boost.Asio), a protocol/prefix table (`file://` load+save, `https://` load, `ipfs://` load+save, `sftp://` save; dormant: `sftp://` load, `wss://` load), a minimal API sketch (post-parse `LoadASync`/`SaveASync` signatures — no parse param), build+test commands per D-38, and FileExample usage. No deep internals, no threading/error-handling essays.
- **D-38:** Build instructions document the **user's canonical wrapper commands** (correcting the agent's wrong vcpkg assumption during discussion):
  - Windows: `cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=W:\gnus\GeniusNetwork\thirdparty` (add `-DTESTING=ON` for tests — noted as optional), then `cmake --build . --parallel 8 --config Release`
  - POSIX: `cmake .. -DCMAKE_BUILD_TYPE=Debug -DTHIRDPARTY_DIR=/Users/fuu/gnus/thirdparty/`, then `make -j8`
  - **Ninja must be included as a POSIX generator option** (e.g. `cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=...` + `ninja -j8`)
  - The README should make clear the wrapper lives at `build/<Platform>/` and requires a prebuilt thirdparty tree; the root `CMakeLists.txt` is what the super-build's ExternalProject consumes (not for direct configuring).

### Clean-tree definition (success criterion 4)
- **D-39:** "Fresh out-of-source configure + build + test" concretely = **reconfigure + full rebuild in the existing wrapper tree**: re-run the wrapper configure against `build/Windows` (Debug tree or a Release equivalent per executor's flow), build with `--clean-first` so every object recompiles from scratch, then run full ctest. NOT a virgin build directory — the incremental-vs-fresh distinction this phase proves is "clean compile from scratch," satisfied by `--clean-first`.
- **D-40:** Verification configuration = **Release** (`-DCMAKE_BUILD_TYPE=Release` + `--config Release` + `ctest -C Release`), matching the user's canonical command. This is the primary and sufficient evidence for TEST-04/BUILD-02 green-suite criteria; no Debug verification required (prior phases already hold Debug evidence).

### Claude's Discretion
- Exact README section ordering/wording, whether the mermaid class diagram survives in simplified form (Loaders/Savers registry, no parser tier) or is replaced by a plain table — provided it stays ~80–120 lines, zero `mnn`, zero stale parser references.
- The exact grep command variant recorded in VERIFICATION.md (PowerShell `Select-String` vs `git grep -iI mnn -- <paths>`) — both ran in prior phases; `git grep -iI` with explicit pathspecs is preferred for reviewability.
- How the Release-config verify tree is arranged (reconfigure the existing `build/Windows/Debug` dir in place vs. a sibling `build/Windows/Release` dir) — noting D-39 allows either as long as the rebuild is `--clean-first` and untracked artifacts stay untracked.
- Whether VERIFICATION.md quotes full ctest output or a summarized table (prior phases summarized + quoted the "100% tests passed" line).
- Fixture/verbatim details of the FileExample usage block (argument defaults, expected output snippet) as long as they match the real `example/FileExample.cpp` behavior.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project planning
- `.planning/ROADMAP.md` — Phase 5 goal + 4 success criteria (zero-mnn grep incl. straggler fixes, no `.mnn` test references, green Windows suite with zero ctest failures, fresh out-of-source end-to-end success); TEST-03/TEST-04/BUILD-02 mapping; "Depends on: Phases 1-4"
- `.planning/REQUIREMENTS.md` — Build & Tests section: TEST-03, TEST-04, BUILD-02 definitions; "Out of Scope" table (Linux CI excluded; dormant protocols v2)
- `.planning/PROJECT.md` — Constraints: "MNN must no longer appear in any find_package/link/include"; Platform constraint (Windows build+tests = verification gate); Context notes on wrapper/super-build split

### Prior phase context
- `.planning/phases/04-build-purge-generic-example/04-CONTEXT.md` — D-29/D-30 (fixtures now `test/fixture.bin` + `example/example_data.bin`), D-31 (super-build MNN block purged), D-32 (`mnn://` test neutralized) — why the live grep is already clean; D-26 (example is super-build-only via wrapper)
- `.planning/phases/04-build-purge-generic-example/04-VERIFICATION.md` — the evidence format this phase's VERIFICATION.md mirrors: wrapper reconfigure command (`cmake -S build/Windows -B build/Windows/Debug`), dual grep gates (`git grep -iI mnn -- CMakeLists.txt '*CMakeLists.txt' '*.cmake'`), "100% tests passed, 0 tests failed out of 9" ctest record
- `.planning/phases/03-parser-layer-removal/03-CONTEXT.md` — D-23/D-24: the phase-end narrow-grep-gate pattern and the no-permanent-guard rationale D-34 inherits; post-parse API shape the README's code sketch must show
- `.planning/phases/02-localfilecommon-platform-split/02-CONTEXT.md` — D-17/D-18: `no_ifdef_test` (the 4th always-on suite this gate runs) — context for what the green suite contains

### Codebase maps (predate Phase 1 rename — verify against live sources)
- `.planning/codebase/TESTING.md` — the two-gate structure (`BUILD_TESTING` vs `ASYNC_IO_MANAGER_NETWORK_TESTS`), the ctest suites-at-a-glance table, Windows multi-config invocation (`ctest -C <Config>`), xunit XML location — the ground truth for D-33's gate contents
- `.planning/codebase/STACK.md` — standalone-vs-super-build modes; `build/<Platform>/CMakeLists.txt` wrapper + `THIRDPARTY_DIR` mechanics the README documents (D-38)

### Key files this phase touches or gates (verified live during this discussion)
- `README.md` — the only in-scope file with MNN residue: 16 case-insensitive `mnn` hits (MNN format bullet, MNNLoader/MNNParser/FileParser diagrams, parsers-map FileManager diagram, MNN dependency link, fake MNNExample build/run logs, `1.mnn` paths); fully rewritten per D-36/D-37/D-38
- `build/Windows/CMakeLists.txt` — the wrapper the canonical configure command targets (VS generator, `/MT` static runtime, `SET(TESTAPP FileExample)` install)
- `build/CommonCompilerOptions.CMake` — `THIRDPARTY_DIR` default resolution + `option(TESTING ... ON)` / `option(BUILD_EXAMPLES ... ON)` — the switches behind D-38/D-39's commands
- `example/FileExample.cpp` + `example/example_data.bin` — what the README usage section documents
- `test/` — `fixture.bin` present; zero `.mnn` references (TEST-03 expected to verify clean, not fix)

No external specs/ADRs exist — requirements fully captured in ROADMAP.md, REQUIREMENTS.md, PROJECT.md, and decisions above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- Prior-phase VERIFICATION.md format (04-VERIFICATION.md) — criteria-table + live-evidence-quote structure D-34's record mirrors
- Established grep gate command: `git grep -iI mnn -- CMakeLists.txt '*CMakeLists.txt' '*.cmake'` (exit 1 = clean) — extend pathspecs per D-35: `include/ src/ test/ example/ README.md`
- Wrapper configure/build/ctest loop proven in Phases 2–4: `cmake -S build/Windows -B build/Windows/Debug` → `cmake --build ... --config <C>` → `ctest --test-dir ... -C <C> --output-on-failure`

### Established Patterns
- Phase-end gates are live-executed + documented, never converted into permanent ctest entries (D-24 → D-34 lineage)
- Evidence lives in VERIFICATION.md with per-criterion ✓ rows and quoted command output
- README tone: this is a consumer-facing library doc — protocol/prefix contract and build recipe matter most (D-37)

### Integration Points
- The README rewrite must match the live post-refactor surface exactly: `FileManager::LoadASync(url, save, ioc, finalcall, savertype)` / `SaveASync(url, data, ioc, completion)` (no parse param anywhere), prefixes actually registered by `InitializeSingletons()` (file load/save, https load, ipfs load/save, sftp save)
- The Release-config verify tree must stay untracked (no new committed build artifacts — the repo currently tracks only the 6 wrapper/cmake files under `build/`)

</code_context>

<specifics>
## Specific Ideas

- README build commands must be the user's verbatim canonical forms (D-38): Windows `cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=W:\gnus\GeniusNetwork\thirdparty`; POSIX `cmake .. -DCMAKE_BUILD_TYPE=Debug -DTHIRDPARTY_DIR=/Users/fuu/gnus/thirdparty/`; `cmake --build . --parallel 8 --config Release` / `make -j8`; **Ninja included as a POSIX option**; `TESTING=ON` mentioned as the optional switch for the test suite.
- The old README's "Parsing and loading any format file" tagline and "Future thinking - flatbuffer" section: drop both (parsing is gone; flatbuffer idea is stale) — replacement tagline centers on URL-dispatched async load/save.
- ctest evidence should quote the summary line (e.g. "100% tests passed, 0 tests failed out of N") plus the suite list, matching 04-VERIFICATION.md style.
- If the D-35 grep DOES surface a straggler (unexpected per live scan), fix it in-phase — that's roadmap criterion 1's explicit instruction, not scope creep.

</specifics>

<deferred>
## Deferred Ideas

- Build-artifact hygiene (deleting local untracked `build/Debug` leftovers incl. `hang.dmp`, adding a `.gitignore`) — explicitly **skipped/deferred by user choice**; artifacts are already untracked and harmless to the gate. Candidate for a future housekeeping phase alongside the `.claude/CLAUDE.md` refresh.
- Refreshing `.claude/CLAUDE.md` + `.planning/codebase/*.md` maps (they describe the pre-refactor MNN-era commit) — a `/gsd-map-codebase` re-map after milestone close is the natural moment; not this phase.
- Network-suite verification (`ASYNC_IO_MANAGER_NETWORK_TESTS=ON` pass) — v2 hardening; not evidence for this gate (D-33).
- Fresh-directory/virgin-tree configure verification — D-39 chose reconfigure + `--clean-first` as sufficient; a true clean-tree clone build can ride the next milestone's CI work if any.
- README deep-dive (architecture diagrams, threading/error-handling conventions, super-build consumer guide) — rejected for D-37's concision; revisit if consumers ask.

None of these block Phase 5.

</deferred>

---

*Phase: 5-Zero-MNN Verification & Green Suite*
*Context gathered: 2026-09-04*
