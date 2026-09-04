# Phase 3: Parser Layer Removal - Discussion Log

**Date:** 2026-09-04
**Mode:** default (interactive)
**Areas discussed:** Auto-save test design, Latent UB in save branch, Example call sites, Phase-end verify gate

> Human reference only — audits/retrospectives. Downstream agents read `03-CONTEXT.md`.

## Area Selection

Presented 4 gray areas (all grounded in scouting): auto-save test design (criterion 3 has no covering test today), latent UB in the save branch, example call-site handling, phase-end verify gate shape. User selected **all four**.

## Area 1: Auto-save test design

| Question | Options presented | Selection | Notes |
|---|---|---|---|
| Which auto-save chain to test? | file:// chain / ipfs:// chain / Both chains | **file:// chain** | Fast, deterministic, Windows-green; network-gated BitswapNode fixtures deemed too heavy for criterion 3 |
| How to handle the UUID-dir-in-CWD save target? | RAII CWD swap / Scan CWD as-is / Fix the "" target | **RAII CWD swap** | No behavior change (PARSE-04); "" target means "saver decides location" — essential for ipfs CID flow |
| Where does the test live? | filemanager_test / localfile_loader_test / new autosave_test | **filemanager_test** | Auto-save wrapper is FileManager's contract; Phase 1 D-06 precedent |
| Assert depth? | Exist + content / Existence only / + counter check | **Exist + content** | Read back bytes and EXPECT_EQ against source — proves buffers flowed through SaveASync |

Key insight surfaced: `finalcall` fires at load completion BEFORE save completes — the test must poll the filesystem, not the callback latch.

## Area 2: Latent UB in save branch

| Question | Options presented | Selection | Notes |
|---|---|---|---|
| Fix unchecked `savers.find(savetype)` deref in-phase? | Guard in-phase / Leave as-is / Guard + test | **Guard in-phase** | end() check → log + decrement + failure to finalcall; Phase 2 D-12/D-13 precedent; no dedicated test (keep lean) |

## Area 3: Example call sites

| Question | Options presented | Selection | Notes |
|---|---|---|---|
| Touch example's LoadASync call in Phase 3? | One-line drop / Defer to Phase 4 | **One-line drop** | Keeps roadmap criterion 2 ("tests + example") literally true; Phase 4 rewrites example anyway |

## Area 4: Phase-end verify gate

| Question | Options presented | Selection | Notes |
|---|---|---|---|
| Gate shape? | Narrow grep gate / Tests-only gate / Grep + permanent test | **Narrow grep gate** | Patterns: `bool parse`, `parse_`, `FileParser`, `ParseData`, `RegisterParser`, `parsers[`, `parsers.find`; avoids URLStringUtil/httplib.h false positives |
| File scope? | include+src+test / Include example | **include+src+test** | Example not in build tree; Phase 4 rewrites it |

## Close-out

User confirmed ready for context after all four areas — no additional gray areas requested.

## Deferred ideas captured

- ipfs:// auto-save chain variant (v2 hardening candidate)
- Permanent `no_parse_test` runtime gate (rejected — compile breakage guards signatures)
