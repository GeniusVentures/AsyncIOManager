# Phase 5: Zero-MNN Verification & Green Suite - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-04
**Phase:** 5-Zero-MNN Verification & Green Suite
**Areas discussed:** Gate scope & evidence, README.md fate, Clean-tree definition, Build artifact hygiene

---

## Gate scope & evidence

### Q1: Does the green-suite gate include the network-gated tests?

| Option | Description | Selected |
|--------|-------------|----------|
| Default gate only | Verify only always-on suites (localfile_loader, localfile_saver, filemanager, no_ifdef); network suites stay unverified | ✓ |
| Include network tests | Configure with ASYNC_IO_MANAGER_NETWORK_TESTS=ON so http/ipfs/sftp suites build & run; skips tolerated | |
| Two-tier gate | Default gate 100% green; network pass as best-effort evidence | |

**User's choice:** Default gate only
**Notes:** Matches roadmap criterion 3 literally ("BUILD_TESTING=ON"); network suites are a separate gated surface.

### Q2: How is the zero-mnn grep + green-suite evidence captured?

| Option | Description | Selected |
|--------|-------------|----------|
| Live run + doc record | Run gate live in-phase (build + ctest), record pass/fail counts + suite list in VERIFICATION.md; no new permanent test code | ✓ |
| Add permanent guard | Above + permanent ctest-adjacent grep guard scanning repo for 'mnn' at runtime | |
| Document commands only | Only document commands for a human; agent doesn't execute | |

**User's choice:** Live run + doc record
**Notes:** Continues the D-24 (Phase 3) lineage: phase-end gates are live-executed and documented, never permanent tests.

### Q3: What file set must the zero-mnn grep gate cover?

| Option | Description | Selected |
|--------|-------------|----------|
| Roadmap scope only | include/, src/, test/, example/, all CMakeLists.txt + cmake + build wrappers (roadmap criterion 1 exactly) | |
| Roadmap + README | Roadmap scope + README.md (16 known stragglers fixed here, so README joins the gate) | ✓ |
| All tracked files | Everything tracked except .planning/ history and .claude/CLAUDE.md map | |

**User's choice:** Roadmap + README
**Notes:** Live scan during discussion showed the roadmap scope already at zero matches; README is the only residue.

---

## README.md fate

### Q1: What happens to README.md in this phase?

| Option | Description | Selected |
|--------|-------------|----------|
| Full rewrite in P5 | Correct diagrams (no parser layer), current protocols/prefixes, real FileExample usage, real build instructions, drop MNN deps/output | ✓ |
| MNN-scrub only | Delete the 16 mnn strings + stale parser bits; keep structure | |
| Defer entirely | Leave README untouched; grep gate excludes it | |

**User's choice:** Full rewrite in P5
**Notes:** README currently shows MNNParser/FileParser diagrams, parsers-map FileManager, MNN GitHub dep link, fake MNNExample inference output.

### Q2: How deep should the rewritten README go?

| Option | Description | Selected |
|--------|-------------|----------|
| Concise practical | ~80-120 lines: what it is, protocol/prefix table, minimal API sketch, build+test commands, FileExample usage | ✓ |
| Deep documentation | Architecture mermaid diagrams, threading model, error-handling conventions, super-build guide | |
| Mirror current shape | Replace stale content section-by-section 1:1 | |

**User's choice:** Concise practical
**Notes:** Consumer-facing doc; contract and build recipe matter most.

### Q3: Which build flows does the README document?

| Option | Description | Selected |
|--------|-------------|----------|
| Wrapper flow only | cmake -S build/Windows style wrapper + THIRDPARTY_DIR, TESTING=ON, build + ctest | |
| Wrapper + root note | Above + note that root CMakeLists is ExternalProject-consumed | |
| Wrapper + stubs | Above + untested Linux/macOS stubs | |

**User's choice:** (free text) User corrected the agent's vcpkg assumption and supplied canonical commands
**Notes:** "We don't use vcpkg? What? Anyway standalone build is in build/<Platform> must specify thirdparty directory, the root cmakelist is for the thirdparty externalproject." Canonical commands supplied: Windows `cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=W:\gnus\GeniusNetwork\thirdparty` (or Debug; `TESTING=ON` optional add), then `cmake --build . --parallel 8 --config Release`; POSIX `cmake .. -DCMAKE_BUILD_TYPE=Debug -DTHIRDPARTY_DIR=/Users/fuu/gnus/thirdparty/` then `make -j8`; **Ninja should be included as a POSIX option**.

---

## Clean-tree definition

### Q1: What does 'fresh out-of-source configure + build + test' concretely mean?

| Option | Description | Selected |
|--------|-------------|----------|
| Fresh dir, full run | New/directory-wiped configure + build + ctest from scratch | |
| Reconfigure + rebuild | Keep existing tree; re-configure + full rebuild (--clean-first) + ctest | ✓ |
| Pristine tree wipe | git clean -xfd everything, then fresh dir configure/build/test | |

**User's choice:** Reconfigure + rebuild
**Notes:** `--clean-first` satisfies "clean compile from scratch" without a virgin build directory.

### Q2: Which configuration does the green-suite verification target?

| Option | Description | Selected |
|--------|-------------|----------|
| Release | Matches canonical command (-DCMAKE_BUILD_TYPE=Release + --config Release); proves shipping config | ✓ |
| Debug | Matches existing build/Windows/Debug tree + prior evidence | |
| Both | Release primary + Debug regression confirmation | |

**User's choice:** Release
**Notes:** Prior phases hold Debug evidence; Release is the user's canonical flow.

---

## Build artifact hygiene

### Q1: What happens with stale build/Debug artifacts (untracked) and ignore hygiene?

| Option | Description | Selected |
|--------|-------------|----------|
| Purge + .gitignore | Delete local leftovers + hang.dmp, add .gitignore covering build outputs | |
| .gitignore only | Add .gitignore; leave local files untouched | |
| Skip / defer | Out of phase scope; note as deferred | ✓ |

**User's choice:** Skip / defer
**Notes:** Artifacts are already untracked (git ls-files build shows only the 6 wrapper/cmake files; git status clean) — harmless to the gate. Deferred alongside the future `.claude/CLAUDE.md` map refresh.

---

## Claude's Discretion

- README section ordering/wording; mermaid diagram simplified vs plain table (within ~80-120 lines, zero mnn, zero stale parser refs)
- Exact grep command variant recorded in VERIFICATION.md (git grep -iI with pathspecs preferred)
- Release verify-tree arrangement (existing dir reconfigured vs sibling Release dir), provided --clean-first rebuild and untracked status hold
- VERIFICATION.md quoting style (summary table + "100% tests passed" line, per 04-VERIFICATION.md)
- FileExample usage-block details, matching real example/FileExample.cpp behavior

## Deferred Ideas

- Build-artifact hygiene + .gitignore (user chose skip/defer)
- .claude/CLAUDE.md + .planning/codebase/*.md re-map post-milestone
- Network-suite verification (ASYNC_IO_MANAGER_NETWORK_TESTS=ON)
- Fresh-directory/virgin-tree configure verification
- README deep-dive documentation
