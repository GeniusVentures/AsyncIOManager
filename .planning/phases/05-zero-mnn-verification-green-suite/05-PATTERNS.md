# Phase 5: Zero-MNN Verification & Green Suite - Pattern Map

**Mapped:** 2026-09-04
**Files analyzed:** 3 (1 modified, 1 created at verify-time, 1 conditional contingency)
**Analogs found:** 2 / 2 planned artifacts (the conditional item has no fixed target file — live scan expects zero triggers)

**Phase character:** documentation + verification only. No library source-code files are expected to change; the only in-scope edit is the `README.md` full rewrite. Evidence capture (reconfigure → `--clean-first` rebuild → ctest → grep gates) is executed live per D-34 and recorded in the phase VERIFICATION.md, mirroring the Phase-4 format.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `README.md` (MOD — full rewrite per D-36/D-37/D-38) | consumer documentation | N/A (docs) | existing `README.md` (rewrite base: section skeleton survives, all content replaced) + API truths read from `include/FileManager.hpp`, `src/FileManager.cpp`, `example/FileExample.cpp` | exact (base + verified sources) |
| `.planning/phases/05-zero-mnn-verification-green-suite/05-VERIFICATION.md` (NEW — created during phase-end verification per D-34, not by execute-phase code plans) | verification evidence record | N/A (docs) | `.planning/phases/04-build-purge-generic-example/04-VERIFICATION.md` | exact |
| (conditional) any D-35-scope straggler file surfaced by the grep gate | varies (source/config) | N/A | Phase-4 minimal-diff scrub edits (e.g. one-token rename in `build/Windows/CMakeLists.txt:85`, block deletion in `build/CommonBuildParameters.cmake`) | role-match — **contingency only**: live scan at discussion/research time shows zero non-README matches |

---

## Pattern Assignments

### `README.md` (consumer documentation, full rewrite)

**Analog:** existing `README.md` (~104 lines) for the section skeleton; header/source files for every factual claim.

**Section skeleton — keep the shape, replace the flesh.** The old file's flow (title/tagline → design → dependencies → build → run example) is the right consumer-doc order; D-37 caps the result at ~80–120 lines.

**Disposition table for the rewrite** (old-line refs from 05-RESEARCH.md "README.md Rewrite Inventory"; the 16 `mnn` hits live at old lines 4, 16, 20, 34, 38, 66, 79–86, 97–99):

| Old README section | Disposition |
|--------------------|-------------|
| `# FileLoader` title + "Parsing and loading any format file" tagline + "MNN format / TODO" bullet (lines 1–6) | Replace: title `AsyncIOManager` + URL-dispatched async I/O tagline; protocol/prefix table |
| Three mermaid diagrams — FileLoader/MNNLoader, FileParser/MNNParser, FileManager with `parsers` map + `RegisterParser`/`ParseData` (lines 8–44) | Replace: simplified registry view (Loaders/Savers only — no parser tier) as mermaid **or** plain table (discretion) |
| "Design: Singleton pattern" (lines 46–48) | May survive condensed |
| Dependencies: MNN GitHub link + GTest (lines 50–67) | Replace: Boost.Asio/Beast/SSL, libp2p, ipfs-lite/bitswap, libssh2, spdlog — all via the prebuilt thirdparty tree (no direct fetch links); keep GTest mention tied to `TESTING=ON` |
| "Build with Linux" + fake MNN build transcript (lines 69–87) | Replace: D-38 POSIX commands (`make` **and** Ninja variants) |
| "Build with Windows / Mac OS: TODO: Need to update" (lines 89, 93) | Replace: D-38 Windows command; macOS folds into the POSIX section |
| "Run the example" fake `MNNExample 1.mnn` inference log (lines 95–102) | Replace: real FileExample usage block (facts below) |
| "Future thinking: flatbuffer" (line 104) | Drop entirely (stale) |

**Protocol/prefix table — copy from the live registrations** (`src/FileManager.cpp:29-40`):

```cpp
void FileManager::InitializeSingletons()
{
    sgns::LocalFileLoader::InitializeSingleton();
    //sgns::SFTPLoader::InitializeSingleton();     // dormant — do NOT document as active
    sgns::HTTPLoader::InitializeSingleton();
    //sgns::WSLoader::InitializeSingleton();       // dormant — do NOT document as active
    sgns::IPFSLoader::InitializeSingleton();
    sgns::IPFSSaver::InitializeSingleton();
    sgns::LocalFileSaver::InitializeSingleton();
    sgns::SFTPSaver::InitializeSingleton();
}
```

Resulting table (verified in 05-RESEARCH §4): `file://` load+save; `https://` load; `ipfs://` load+save; `sftp://` save (load dormant); `wss://` load dormant/registered-not-initialized.

**API sketch — copy signatures verbatim from `include/FileManager.hpp`** (never from memory; Pitfall 6):

```cpp
// include/FileManager.hpp:56-57
using ResultType = outcome::result<std::shared_ptr<
    std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>;
// include/FileManager.hpp:69-73
using FinalCallback = std::function<void( ResultType buffers )>;

// include/FileManager.hpp:101-107 — note: NO parse parameter anywhere
shared_ptr<void> LoadASync( const std::string &url, bool save,
                            std::shared_ptr<boost::asio::io_context> ioc,
                            FinalCallback finalcall, std::string savertype );
// include/FileManager.hpp:109-115
void SaveASync( const std::string &url, ResultType data,
                std::shared_ptr<boost::asio::io_context> ioc,
                FinalCallback finalcall,
                std::shared_ptr<std::string> save_location = nullptr );
```

Forbidden strings in the new README (each is a stale-era tell): `parse` parameter, `RegisterParser`, `ParseData`, `MNNLoader`, `FileParser`, `mnn://`.

**Build commands — D-38 verbatim** (user's canonical forms; correcting the agent's vcpkg assumption):

```sh
# Windows (from build/Windows/<dir>):
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=W:\gnus\GeniusNetwork\thirdparty
cmake --build . --parallel 8 --config Release
# add -DTESTING=ON to build the test suite (optional; defaults ON)

# POSIX (make):
cmake .. -DCMAKE_BUILD_TYPE=Debug -DTHIRDPARTY_DIR=/Users/fuu/gnus/thirdparty/
make -j8
# POSIX (Ninja — MUST be included):
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=...
ninja -j8
```

Also state: the wrapper lives at `build/<Platform>/` and requires a prebuilt thirdparty tree; the root `CMakeLists.txt` is what the super-build's ExternalProject consumes (not for direct configuring).

**Example usage block — copy facts from `example/FileExample.cpp:1-13`** (usage comment + defaults):

```cpp
// FileExample.cpp — minimal file:// load → save roundtrip through FileManager.
// Usage: FileExample [input-url] [output-dir-url]
//   defaults: input  file://example_data.bin   (relative to the process CWD — run from example/)
//             output file://example_output/    (roundtrip lands in example_output/<basename>)
```

Expected output lines (match the real prints at `example/FileExample.cpp:34-38` / `:57-61`): `Loaded "<path>" (N bytes) from <url>` then `Saved "<path>" (N bytes) to <url>`; exit 0 on success, 1 on failure (delivered via callback, never thrown).

---

### `.planning/phases/05-zero-mnn-verification-green-suite/05-VERIFICATION.md` (verification evidence record)

**Analog:** `.planning/phases/04-build-purge-generic-example/04-VERIFICATION.md` — mirror its structure exactly (D-34 lineage; 05-CONTEXT canonical_refs point at it as "the evidence format this phase's VERIFICATION.md mirrors").

**Frontmatter pattern** (`04-VERIFICATION.md:1-15`):

```yaml
---
phase: 05-zero-mnn-verification-green-suite
verified: <ISO-8601 timestamp>
status: passed
score: <N>/<N> must-haves verified
overrides_applied: <count — expect 0>
---
```

**Section order to replicate** (all present in the analog): `# Phase N: ... Verification Report` → Phase Goal + Verified/Status/Re-verification/Mode header lines → `## User Flow Coverage` → `## Goal Achievement` → `### Observable Truths` (one table per plan) → `### Required Artifacts` → `### Key Link Verification` → `### Data-Flow Trace (Level 4)` → `### Behavioral Spot-Checks` → `### Probe Execution` → `## Requirements Coverage` (per REQ-ID + cross-reference table) → `## Roadmap Phase-N Success Criteria` → `## Anti-Patterns Found` → `## Deviation Judgment` (if any) → `## Human Verification Required` → `## Deferred Items` → `### Gaps Summary`.

**Evidence-row pattern** (`04-VERIFICATION.md` Observable Truths):

```markdown
| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `git grep -iI mnn -- CMakeLists.txt '*CMakeLists.txt' '*.cmake'` returns no matches | ✓ VERIFIED | Re-ran live: exit 1 (empty). ... |
```

Style: every ✓ row quotes the command + its observed result ("exit 1 (empty)", ctest summary line verbatim). Phase-5 rows extend the grep pathspecs per D-35 (`include/ src/ test/ example/ ... README.md`) and add the TEST-03 sub-gate (`git grep -iI '\.mnn' -- test/`).

**Phase-5-specific content requirements** (deltas vs the analog — planner must encode these):

1. **Expected ctest count is 4, not 9.** Quote-the-line pattern stays, but the expected text is `100% tests passed, 0 tests failed out of 4` — Phase 4's "out of 9" came from a network-tests-ON configure; D-33's default gate registers only the 4 always-on suites (`localfile_loader_test`, `localfile_saver_test`, `filemanager_test`, `no_ifdef_test`). Record the gate definition alongside the count to preempt misreads (05-RESEARCH Pitfall 1).
2. **Record the pre-rewrite baseline for the delta narrative:** 16 `mnn` hits in README.md at old lines 4, 16, 20, 34, 38, 66, 79–86, 97–99 (post-rewrite: exit 1). 05-RESEARCH Open Question 2 recommends post-rewrite exit-1 evidence + baseline citation.
3. **Note the stale untracked `mnn_*.exe` relics** in `build/Windows/Release/test_bin/Release/` (pre-Phase-1 artifacts, invisible to the git-grep gate, left in place per the deferred hygiene decision) — prevents false BUILD-02 alarms (Pitfall 4).
4. **Verification loop evidence** must show the three aligned Release pins (`-DCMAKE_BUILD_TYPE=Release` + `--config Release` + `ctest -C Release`, per D-40) and the explicit `-DASYNC_IO_MANAGER_NETWORK_TESTS=OFF` on reconfigure (cache-stickiness, Pitfall 2).
5. Roadmap success-criteria table has **4 rows** this phase (zero-mnn grep incl. straggler fixes, no `.mnn` test refs, green Windows suite / zero ctest failures, fresh out-of-source end-to-end per D-39's reconfigure + `--clean-first` definition).

---

### (conditional) grep-straggler fixes

**Analog:** Phase-4 minimal-diff edits — one-token renames (`build/Windows/CMakeLists.txt:85` `SET(TESTAPP FileExample ...)`) and whole-block deletions (`build/CommonBuildParameters.cmake` MNN block).

**Protocol if triggered** (roadmap SC-1 explicit instruction, not scope creep): fix in-place following the offending file's own conventions; smallest possible diff; no drive-by edits. **Live scan says zero non-README matches exist**, so the planner should treat this as a documented contingency (a conditional plan task or verify-step instruction), not a scheduled edit.

---

## Shared Patterns

### 1. Negation grep gate with exit-code semantics
**Source:** `04-VERIFICATION.md` Behavioral Spot-Checks + 05-RESEARCH Pattern 1
**Apply to:** BUILD-02 (D-35) and TEST-03 evidence rows in VERIFICATION.md

```powershell
git grep -iI mnn -- include/ src/ test/ example/ CMakeLists.txt '*CMakeLists.txt' '*.cmake' '*.CMake' README.md
# exit 1 + no output = PASS ; exit 0 + listed matches = FAIL
# TEST-03 sub-gate: git grep -iI '\.mnn' -- test/   (verified already exit 1)
$LASTEXITCODE   # PowerShell check
```

Never use a bare repo-wide `git grep -i mnn` — D-35b excludes `.planning/**`, `.claude/`, build trees (guaranteed false failures, Pitfall 7).

### 2. Wrapper reconfigure → clean rebuild → config-pinned ctest loop
**Source:** Phases 2–4 recorded evidence; 05-RESEARCH Code Examples §1
**Apply to:** TEST-04 / D-39 / D-40 verification

```powershell
cmake -S build/Windows -B build/Windows/Release -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DTHIRDPARTY_DIR=W:\gnus\GeniusNetwork\thirdparty `
  -DASYNC_IO_MANAGER_NETWORK_TESTS=OFF
cmake --build build/Windows/Release --clean-first --parallel 8 --config Release
ctest --test-dir build/Windows/Release -C Release --output-on-failure   # expect 4/4
```

Non-negotiables: explicit `NETWORK_TESTS=OFF` (cache stickiness), `--clean-first` (D-39's "clean compile from scratch"), all three Release pins aligned (Pitfall 5), terminal cmake only (VS Code CMake Tools cannot configure this super-build, Pitfall 8), ~10 min build latency — don't mutate the tree mid-build (Pitfall 9).

### 3. Evidence formatting (criteria tables + verbatim quotes)
**Source:** `04-VERIFICATION.md` throughout
**Apply to:** every row of 05-VERIFICATION.md

Pattern: `| ✓ VERIFIED | <command> → <observed result> |` — command and output quoted verbatim; runtime evidence produced in-session may be designated trusted rather than re-run (~10 min rebuilds are not repeated; filesystem/git corroboration instead), exactly as the analog's "Behavioral Spot-Checks" preamble does.

### 4. API-surface anti-drift for documentation
**Source:** Pitfall 6 + 05-CONTEXT code_context ("README rewrite must match the live post-refactor surface exactly")
**Apply to:** every signature, prefix, and usage claim in the new README

Rule: no documented symbol unless read this-session from `include/FileManager.hpp` (signatures), `src/FileManager.cpp:29-40` (registered prefixes), or `example/FileExample.cpp` (usage/output). Warning signs: any `parse` parameter, `RegisterParser`, `ParseData`, `MNNLoader`, or `mnn://` in the draft. Note for the usage example: `outcome::result<T>` has a deleted default constructor — consumer examples capture results via `std::optional<FileManager::ResultType>` (04-VERIFICATION override lineage, visible at `example/FileExample.cpp:26,49`).

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| — | — | — | Both planned artifacts have exact analogs. The conditional straggler-fix target (if the gate ever triggers) is by definition unknown until grep runs; its fallback analog is the Phase-4 minimal-diff scrub pattern above. |

## Metadata

**Analog search scope:** repo root (`README.md`), `include/`, `src/`, `example/`, `.planning/phases/04-build-purge-generic-example/` (VERIFICATION + PATTERNS format precedent)
**Files scanned:** README.md (full), 04-VERIFICATION.md (full), include/FileManager.hpp (full), example/FileExample.cpp (full), src/FileManager.cpp (InitializeSingletons + dispatch), 05-CONTEXT.md, 05-RESEARCH.md
**Pattern extraction date:** 2026-09-04
