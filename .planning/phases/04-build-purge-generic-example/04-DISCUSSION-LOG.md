# Phase 4: Build Purge & Generic Example - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-04
**Phase:** 4-Build Purge & Generic Example
**Areas discussed:** Example scope & shape, Fixture replacement, Super-build MNN block, mnn:// rejection test fate

---

## Example scope & shape

| Option | Description | Selected |
|--------|-------------|----------|
| Minimal file:// only | ~50-80 lines: InitializeSingletons → LoadASync(file://) → SaveASync(file://) → run ioc. No libp2p, no bitswap, no soralog. Old MNNExample.cpp deleted outright. | ✓ |
| Keep ipfs demo too | New FileExample.cpp plus a slimmed ipfs example carrying the libp2p/bitswap setup. More surface to maintain; network-dependent. | |
| One binary, both paths | file:// and ipfs:// paths selected by URL prefix at runtime; keeps ~250 lines of libp2p scaffolding alive. | |

**Follow-up — build wiring:**

| Option | Description | Selected |
|--------|-------------|----------|
| Super-build only (as-is) | example/CMakeLists.txt gets add_executable(FileExample ...); root CMakeLists gains no add_subdirectory(example) — built only via BUILD_EXAMPLES in super-build mode, exactly as MNNExample was wired. | ✓ |
| Root build, opt-in | Root CMakeLists gains add_subdirectory(example) behind BUILD_EXAMPLES option (default OFF); example compiles in the standard Windows verification loop. | |

**Follow-up — what the demo shows:**

| Option | Description | Selected |
|--------|-------------|----------|
| load → save roundtrip | Input path arg (default file://example_data.bin), LoadASync then SaveASync to a second path, byte counts in callbacks; mirrors the test recipe (work guard + ioc->run()). | ✓ |
| Load-only demo | Just LoadASync and print byte count — save exercised only by tests. | |
| Roundtrip + auto-save | Also demo the save=true auto-save-after-load flow (Phase 3 D-19 chain); rejected as coupling the example to UUID-dir-in-CWD saver behavior. | |

**Follow-up — default input source:**

| Option | Description | Selected |
|--------|-------------|----------|
| Committed .bin (default arg) | example/example_data.bin (~1KB committed binary) as the default argument; runnable out-of-the-box. | ✓ |
| Generate-then-load | No committed asset; example writes a temp buffer first and loads it back. | |
| Share test fixture | Reuse the same generic fixture from test/ for both example default and fixture needs; couples example/ to test/ dirs. | |

**User's choice:** Minimal file://-only example, super-build wiring as-is, load→save roundtrip demo, committed .bin default arg.
**Notes:** The old example is 291 lines with ~250 of libp2p/bitswap/soralog scaffolding serving only the ipfs:// demo path; file:// needs none of it. The auto-save flow stays covered by filemanager_test (D-19), not the example.

---

## Fixture replacement

| Option | Description | Selected |
|--------|-------------|----------|
| One small committed .bin | Delete 1.mnn/2.mnn; commit a single ~1KB varied-byte binary. Satisfies MNN-03's "e.g. test/*.bin" hint; drops ~5MB. | ✓ |
| No fixtures in test/ at all | Delete .mnn files, commit nothing — tests already use TempFile inline content; example gets its own bin under example/. | |
| Multiple sized .bin files | empty/small/multi-KB variants pre-staged for future edge cases; more files than anything consumes. | |

**Follow-up — location:**

| Option | Description | Selected |
|--------|-------------|----------|
| example/ only | Single consumer (the example's default arg); test/ carries zero binary fixtures. | |
| test/ only | test/example_data.bin referenced by the example as file://../test/example_data.bin; mirrors old layout but couples dirs. | |
| Both directories | example/example_data.bin + test/fixture.bin — an extra test copy available for any current/future test wanting a committed binary. | ✓ |

**Follow-up — content production:**

| Option | Description | Selected |
|--------|-------------|----------|
| Commit pre-made bytes | Generate once with a trivial command, commit the result; no generator script, no CMake-time generation. | ✓ |
| Scripted generation | Commit a generator script / CMake custom command for deterministic regeneration; machinery for opaque data. | |
| Rename existing file | Use a copy of a small repo file renamed .bin; misleading pseudo-binary. | |

**User's choice:** Small committed .bin files in BOTH example/ and test/, pre-made bytes committed directly.
**Notes:** Verified live: zero test sources reference the .mnn files — only the examples' baked-in file://../test/1.mnn path. Both fixtures are opaque to loaders (TempFile string content is what unit tests actually use).

---

## Super-build MNN block

| Option | Description | Selected |
|--------|-------------|----------|
| Purge it now (Phase 4) | Delete build/CommonBuildParameters.cmake:274-278 (MNN_INCLUDE_DIR/MNN_LIBRARY_DIR sets + global include_directories) alongside the CMakeLists purge; Phase 5's repo-wide grep would otherwise fail on this file. | ✓ |
| Leave for Phase 5 | CommonBuildParameters.cmake is shared super-build environment; defers a known fix into the acceptance phase. | |
| Condition it | Guard behind if(PROJECT_NAME ...) conditionality; complexity for a block nobody needs after this phase. | |

**User's choice:** Purge the super-build MNN block in Phase 4.
**Notes:** The block exists solely to feed this repo's removed MNN dependency (it is the source of MNN_LIBRARY_DIR consumed by the example glob). Purging keeps "MNN becomes consumers' concern" true repo-wide.

---

## mnn:// rejection test fate

| Option | Description | Selected |
|--------|-------------|----------|
| Neutralize prefix (foo://) | Rename SaveASync_MnnPrefixThrows and swap mnn://some/path → neutral unregistered prefix (e.g. foo://some/path). Dispatch contract stays covered; mnn string disappears now. | ✓ |
| Delete as redundant | Remove outright — near-identical neighbor SaveASync_UnregisteredPrefixThrows already covers the path. | |
| Leave for Phase 5 | Phase 5's grep gate flags and fixes it there; defers a mechanical fix into the acceptance phase. | |

**User's choice:** Neutralize the prefix (keep the test, drop the mnn reference).
**Notes:** filemanager_test.cpp:124-133; mechanical rename + URL swap, same EXPECT_THROW/std::range_error shape. Kept as a second data point beside the adjacent generic test.

---

## Claude's Discretion

- Example source structure (single main vs. helpers), console output wording, callback error-reporting style
- Exact .bin byte content (~1KB varied opaque bytes) and the one-off generation command
- example/CMakeLists.txt details beyond the FileExample rename + MNN_LIBS glob removal
- The exact neutral prefix and renamed test name for D-32 (no mnn substring)
- Edit order and commit slicing, provided the phase ends green
- Optional example README/usage notes (none exist today)

## Deferred Ideas

- ipfs:// example coverage — dedicated v2 example if wanted; the old libp2p scaffolding is deleted, not preserved
- Root-build wiring for the example — rejected this phase (D-26); revisit only if the example rots from never being compiled in CI
- Repo-wide zero-mnn grep + clean-tree build verification — Phase 5 by design
- README updates describing the new example — optional, not a criterion
