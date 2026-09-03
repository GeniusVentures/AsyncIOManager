# Phase 1: LocalFile Rename & MNN Code Purge - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-03
**Phase:** 1-LocalFile Rename & MNN Code Purge
**Areas discussed:** FILEDevice class name, Dead parse block, mnn:// rejection test

---

## FILEDevice class name

| Option | Description | Selected |
|--------|-------------|----------|
| Rename to LocalFileDevice | Full triad consistency: LocalFileLoader / LocalFileSaver / LocalFileCommon / LocalFileDevice. Touches ~4 construction sites in the two renamed .cpp files — all in-phase anyway. | ✓ |
| Keep FILEDevice name | Files become LocalFileCommon.hpp/.cpp per FILE-04, but the class keeps the FILEDevice name. Fewer symbol changes; slight naming inconsistency until Phase 2 splits it. | |

**User's choice:** Rename to LocalFileDevice
**Notes:** Decision also confirmed to carry into Phase 2 — the platform split keeps the `LocalFileDevice` class name in both `.win.*` and `.posix.*` halves, so Phase 2 does files-only work with no rename churn.

Follow-up — scrub scope:

| Option | Description | Selected |
|--------|-------------|----------|
| Yes — scrub all | Banner comment becomes 'Header file for the LocalFileCommon'; logger tags like createLogger("FILECommon") become "LocalFileCommon" too. Fully consistent zero-grep story. | ✓ |
| Symbols only | Only rename symbols and includes; leave comment banners and logger tag strings as-is where harmless. | |

**User's choice:** Yes — scrub all (banners + logger tags)

---

## Dead parse block

| Option | Description | Selected |
|--------|-------------|----------|
| Delete block only | Delete the entire if(parse) block in src/FileManager.cpp:74-79. It's dead (ParseASync call commented out, find result unused). The outer parse param stays until Phase 3 — only the hardcoded "mnn" lookup disappears. | ✓ |
| Genericize to suffix | Replace "mnn" with the runtime suffix variable so the block stays functional-looking until Phase 3 removes it wholesale. | |
| Comment out | Leave the block; #if 0 it out — fails criterion 5's grep (text still contains mnn), so not viable. | |

**User's choice:** Delete block only
**Notes:** Phase boundary explicitly confirmed: the `parse` bool parameter, `include/FileParser.hpp`, the `parsers` map, and `RegisterParser` all remain untouched until Phase 3. Phase 1 deletes only the hardcoded `"mnn"` lookup block.

---

## mnn:// rejection test

| Option | Description | Selected |
|--------|-------------|----------|
| filemanager_test owns it | The dispatch/registry contract lives in FileManager — filemanager_test already owns UnregisteredPrefixThrows coverage for both load and save. Add SaveASync_MnnPrefixThrows there; localfile_saver_test stays purely about file I/O behavior. | ✓ |
| localfile_saver_test owns it | Put it in localfile_saver_test as the proof that the renamed saver no longer claims the mnn prefix. | |
| Both suites | Mirror the test in both suites — belt-and-braces, slight duplication. | |

**User's choice:** filemanager_test owns it

Follow-up — rejection paths covered:

| Option | Description | Selected |
|--------|-------------|----------|
| Async only | Only the async variant — it's the one named in success criterion 1. Mirrors SaveASync_UnregisteredPrefixThrows shape. | ✓ |
| Sync + async | Cover sync SaveFile + async SaveASync for mnn:// — both throw paths verified. | |
| All paths | Save + Load for mnn:// — exhaustive rejection matrix, though load rejection is already covered generically by LoadFile_UnregisteredPrefixThrows. | |

**User's choice:** Async only

---

## Claude's Discretion

- Exact git-rename mechanics (single commit vs. staged rename-then-edit)
- Test file header comment wording and fixture naming details inside the renamed tests
- Order of edits within the phase (rename-first vs. delete-first), provided the phase ends green

## Deferred Ideas

- Remaining parse machinery removal (`parse` bool, `FileParser.hpp`, `parsers` map) — Phase 3
- Platform split of `LocalFileCommon` — Phase 2 (class name carries over unchanged)
- CMake MNN purge, `.mnn` fixture replacement, example rewrite — Phase 4
- Activating dormant SFTP/WS loaders — v2 backlog
