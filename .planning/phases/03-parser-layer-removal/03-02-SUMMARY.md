---
plan: 03-02
phase: 03-parser-layer-removal
status: complete
completed: 2026-09-04
commits:
  - "78e2bec feat(03-02): strip parse bool from every async API signature, callback, and call site (PARSE-02)"
requirements: [PARSE-02, PARSE-03, PARSE-04]
---

# Plan 03-02 Summary: Async Signature Strip + Phase-End Gate

## What Was Built

**PARSE-02 — atomic parse-bool strip (one commit, 27 files):**

- **Root** (`include/FileLoader.hpp`): `CompletionCallback` alias → `std::function<void(ioc, ResultType, bool save)>`; `LoadASync` pure virtual → `(filename, bool save, ioc, callback)`
- **FileManager** (`include/FileManager.hpp`, `src/FileManager.cpp`): alias + `LoadASync` decl/def → `(url, bool save, ioc, finalcall, savetype)`; the `handle_read` lambda → 3-param (ioc, buffers, save) with the Plan 01 D-20 guard intact inside; dispatch → `loader->LoadASync( filePath, save, ioc, handle_read )`; class doc comment "loaders, parsers and savers" → "loaders and savers"
- **5 loader headers** (LocalFile/HTTP/IPFS/SFTP/WS): identical alias + override strip, `@param parse` doc lines scrubbed
- **5 loader impls**: override defs stripped; device-construction calls `HTTPDevice(host, path, port, save)` / `WSDevice(..., save)` / `SFTPDevice(..., save)` (parse was the 8th arg in SFTP — right one dropped); IPFSLoader's `[=]` implicit captures died automatically when positional `parse` left the calls
- **Device headers+impls (HTTP/WS)**: ctor decl+def drop `bool parse`; `parse_` member + `parse_ = parse;` init deleted; success echo `self->parse_, self->save_` → `self->save_`
- **IPFS param-threading** (`include/IPFSCommon.hpp`, `src/IPFSCommon.cpp`): `bool parse` dropped from all 4 methods (`StartFindingPeers`, `StartFindingPeersWithRetry`, `RequestBlockMain`, `convertUnixFSContentToResult`) decls+defs; `parse, ` removed from all 5 explicit `[...]` capture lists; all positional forwards stripped; the two echoes at the tail of `convertUnixFSContentToResult` now pass `(save)` / `(save)`
- **~24 posted-failure tails** across 8 source files: `false, false` → `false` (both single-line and multi-line `false,\n false);` forms)
- **SFTP mirror (NOT COMPILED — Discovery 2)**: `SFTPCommon.hpp/.cpp`, `SFTPLoader.cpp/.hpp` mirrored mechanically from the compiled HTTP/WS shapes — ctor (parse was 8th arg), `parse_` member + init, ~10 `handle_read` sites, comment at :392 reworded `//We've read all the data, send to save`. Files NOT added to `add_library` (dormant by design)
- **Tests (5 files)**: all `FileManager::LoadASync` call sites dropped the parse arg (localfile×2, http×3, ipfs×4, filemanager×2 — including the D-19 test's `false, true,` → `true,`); `ipfs_device_test.cpp`'s 3 callback lambda definitions `(ioc, ResultType, bool, bool)` → `(ioc, ResultType, bool)` and 3 device calls keep exactly one `false` (the save flag)
- **Example (D-22)**: `example/MNNExample.cpp` — `false, // don't parse` line deleted, stale `// don't save - just load` comment corrected to `// save`

**Untouched by design** (Discovery 1 legit survivors): `parseHTTPUrl`/`parseSFTPUrl`/`parseIPFSUrl` in `URLStringUtil.*`, their call sites in HTTPLoader/WSLoader/SFTPLoader/IPFSLoader/SFTPSaver/MNNExample, the vendored `include/httplib.h`, and URL-parsing prose comments.

## Verification Gate Evidence (D-23/D-24)

**Gate 1 — D-23 word-boundary grep** (`git grep -nE 'bool\s+parse\b|\bparse_\b|FileParser|ParseData|RegisterParser|parsers\[|parsers\.find' -- include/ src/ test/ example/ ':(exclude)include/httplib.h'`):
```
(no output)
D-23 exit code: 1  → PASS
```

**Gate 2 — @param parse doc scrub** (`git grep -nE '@param parse\b|param parse -' -- include/ src/`):
```
(no output)
DOC exit code: 1  → PASS
```

**Gate 3 — bare-parse eyeball, 4 capture-heavy files** (`git grep -nw parse -- src/IPFSCommon.cpp src/IPFSLoader.cpp src/SFTPCommon.cpp src/SFTPLoader.cpp`):
```
(no output)
BARE exit code: 1  → PASS
```

**Gate 3b — SFTP full-token check** (`git grep -n parse -- include/SFTPCommon.hpp include/SFTPLoader.hpp src/SFTPLoader.cpp`):
```
src/SFTPLoader.cpp:52:        parseSFTPUrl( filename,
SFTP exit code: 0  → single hit = parseSFTPUrl, a legit URL-parser survivor (Discovery 1), not a parse-flag token → PASS
```

**Gate 4 — D-24 no runtime parse test** (`git grep -n no_parse_test -- test/`):
```
(no output)
D-24 exit code: 1  → PASS
```

**D-24 build + ctest** (verbatim):
```
cmake --build build/Windows/Debug --config Debug → exit 0 (library + MNNExample + all 9 test targets)

9/9 Test #9: ipfs_device_test .................   Passed    20.15 sec

100% tests passed, 0 tests failed out of 9

Total Test time (real) =  76.16 sec
```

D-19 test direct-run against the NEW signature:
```
[==========] 1 test from 1 test suite ran (68 ms total)
[  PASSED  ] 1 test.  (FileManagerIntegrationTest.LoadASync_SaveTrueAutoSavesLoadedDataToDisk)
```

## ROADMAP Phase 3 Criteria Cross-Check

| # | Criterion | Evidence |
|---|-----------|----------|
| 1 | FileParser.hpp deleted; registry symbols gone | Plan 01: file absent, D-23 grep includes `FileParser|ParseData|RegisterParser|parsers[|parsers.find` → zero |
| 2 | Parse-free signatures incl. tests+example | Gate 1 + Gate 3 zero matches over include/src/test/example |
| 3 | Auto-save test green (D-19) | Direct-run PASSED post-strip; full ctest green twice |
| 4 | No behavioral change (PARSE-04) | D-19 test body needed ONLY the arity fix (parse arg drop); byte-equal content assertions unchanged; all other tests untouched beyond arity |
| 5 | Green suite | 9/9 tests, 0 failures |

## Deviations

None. The multi-line `false,\n false);` failure-tail forms in HTTPCommon/WSCommon/SFTPCommon were converted with the same single-`false` target as the plan's rule of thumb specifies (the plan enumerated the sites; the line-wrapped form was a formatting variant, not a semantic deviation).

## Caveats (recorded honestly)

- **SFTP grep-only verification**: `SFTPCommon.hpp`, `SFTPCommon.cpp`, `SFTPLoader.cpp` are absent from `add_library` (dormant by design). MSVC-green does NOT prove them; they were mirrored mechanically from the compiled HTTP/WS shapes and verified only by Gates 1/3b. Linux CI would be required to compile-verify (per PROJECT.md Out of Scope).
- **Installed-package surface**: `FileParser.hpp` vanishes from the next `install/*.h*` — expected breaking change, no shims per PROJECT.md compatibility constraint. Downstream consumers (SuperGenius) must adapt to the new `LoadASync(url, save, ioc, finalcall, savetype)` signature.

## Self-Check: PASSED

- [x] `include/FileLoader.hpp` alias is exactly `std::function<void(ioc, ResultType, bool save)>`; pure virtual parse-free
- [x] `grep -c 'parse_' include/HTTPCommon.hpp include/WSCommon.hpp include/SFTPCommon.hpp` → 0 each
- [x] `git grep -nw parse` over the 4 capture-heavy files → exit 1
- [x] SFTP uncompiled trio contains zero parse-flag tokens of any form (only `parseSFTPUrl` URL-parser calls survive)
- [x] `ipfs_device_test.cpp` lambdas 3-param; device calls keep exactly one `false`
- [x] `example/MNNExample.cpp` has exactly one `LoadASync(` call, no parse argument
- [x] Windows build green including `MNNExample`; full ctest 9/9 twice
- [x] D-19 (PARSE-03) green post-strip; zero behavioral edits beyond arity (PARSE-04)

## Key Files

### modified (27, one commit)
- Headers (11): `FileLoader.hpp`, `FileManager.hpp`, `LocalFileLoader.hpp`, `HTTPLoader.hpp`, `IPFSLoader.hpp`, `SFTPLoader.hpp`, `WSLoader.hpp`, `HTTPCommon.hpp`, `IPFSCommon.hpp`, `SFTPCommon.hpp`, `WSCommon.hpp`
- Sources (11): `FileManager.cpp`, `LocalFileLoader.cpp`, `HTTPLoader.cpp`, `HTTPCommon.cpp`, `IPFSLoader.cpp`, `IPFSCommon.cpp`, `SFTPLoader.cpp`, `SFTPCommon.cpp`, `WSLoader.cpp`, `WSCommon.cpp`
- Tests (5): `localfile_loader_test.cpp`, `http_loader_test.cpp`, `ipfs_loader_test.cpp`, `ipfs_device_test.cpp`, `filemanager_test.cpp`
- Example (1): `MNNExample.cpp` (D-22 one-liner)
