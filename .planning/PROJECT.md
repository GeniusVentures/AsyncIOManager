# AsyncIOManager

## What This Is

A C++17 asynchronous I/O library for loading and saving files over multiple protocols (local `file://`, `https://`, `ipfs://`, `sftp://`, `wss://`), built on Boost.Asio. It provides a `FileManager` registry that dispatches URL-prefixed operations to per-protocol loader/saver strategies. Consumed downstream by GeniusNetwork components (e.g. SuperGenius) via the installed CMake package.

## Core Value

Reliable async load/save of data across local and remote protocols behind one URL-dispatched `FileManager` API.

## Requirements

### Validated

- ✓ Async local-file load via `file://` prefix (currently `MNNLoader`) — existing
- ✓ Sync `LoadFile` for local files — existing
- ✓ Async/sync save to local disk via `file://` and `mnn://` prefixes (currently `MNNSaver`) — existing
- ✓ Async HTTPS download via `https://` prefix (`HTTPLoader`/`HTTPDevice`) — existing
- ✓ Async IPFS load via `ipfs://` prefix through ipfs-lite-cpp + bitswap (`IPFSLoader`/`IPFSDevice`) — existing
- ✓ Async IPFS save (publish) via `ipfs://` prefix (`IPFSSaver`) — existing
- ✓ Async SFTP upload via `sftp://` prefix (`SFTPSaver`/`SFTPDevice`) — existing
- ✓ URL parsing utilities for HTTP/SFTP/IPFS (`URLStringUtil`) — existing
- ✓ spdlog-based logging factory with soralog bridging for libp2p — existing
- ✓ GTest suite with xunit XML output and test WS server helper — existing
- ✓ CMake package install (headers + `AsyncIOManagerTargets` export, version 0.1) — existing

### Active

- [ ] Rename `MNNLoader`/`MNNSaver` → `LocalFileLoader`/`LocalFileSaver` (still dispatch on `file://`)
- [ ] Rename `FILECommon` device layer → `LocalFileCommon` to match
- [ ] Split `LocalFileCommon` into separate Windows and POSIX `.cpp` + `.hpp` files, selected in `src/CMakeLists.txt` (no `#ifdef _WIN32` in this layer)
- [ ] Remove MNN dependency from build (`find_package(MNN)`, include dirs, link, example MNN libs glob)
- [ ] Delete MNN-specific files: `include/MNNCommon.hpp`, `include/MNNLoader.hpp`, `include/MNNSaver.hpp`, `src/MNNLoader.cpp`, `src/MNNSaver.cpp`, `test/base_mnn_test.hpp`
- [ ] Replace `.mnn` test assets (`test/1.mnn`, `test/2.mnn`) with generic binary test fixtures
- [ ] Remove `mnn://` save prefix registration; `file://` remains the local save prefix
- [ ] Remove parser layer: delete `FileParser` interface, `RegisterParser`, `ParseData`, parsers map from `FileManager`
- [ ] Strip `parse` bool from all loader/saver APIs and `CompletionCallback` signatures (`save` bool stays)
- [ ] Replace `MNNExample` with a generic file:// load/save example
- [ ] Rename/rewrite affected tests (`mnn_loader_test`, `mnn_saver_test`, `filemanager_test` references) and keep suite green
- [ ] Scrub MNN references from doc comments across headers/sources

### Out of Scope

- Activating dormant `SFTPLoader`/`WSLoader`/plain-`http` in `InitializeSingletons` — deferred; refactor-only milestone
- Removing the `save` bool from `LoadASync` — user explicitly wants it kept; it drives downstream auto-save
- MNN model inference — responsibility moves to consumers (e.g. SuperGenius), not this library
- Backward-compatible shims for renamed classes/changed signatures — breaking change accepted; downstream updates separately
- New protocol support — not part of this milestone

## Context

- **Codebase map exists** at `.planning/codebase/` (mapped at commit `009dc3d`, 2026-09-03) — see ARCHITECTURE.md, STACK.md, CONCERNS.md for detail.
- **Naming convention**: loaders/savers are named after their URL prefix (`https`→`HTTPLoader`, `ipfs`→`IPFSLoader`). `FILELoader` was rejected: on Linux (case-insensitive filesystems / case-sensitive includes) it collides with `FileLoader.hpp` (the base class header). `LocalFile` naming chosen instead, while still accepting `file://` URLs.
- **Boss mandate**: no `#ifdef _WIN32`-style platform branching in implementation files — prefer one file per platform, selected in CMake. `FILECommon` is the immediate target; the pattern can extend to other files later.
- **Current parser layer is dead code**: `MNNParser` is never initialized (commented out in `FileManager::InitializeSingletons`), so the `parse` flag has never driven live behavior — safe to excise.
- **`save` flag flow**: `FileManager::LoadASync` wraps the loader callback; `save=true` triggers auto-save of loaded data via the registered saver. This behavior is live and stays.
- **Windows build** produces a static lib with MSVC `/MT` static runtime in super-build mode; standalone mode available via root CMakeLists. `_WIN32_WINNT=0x0601`.
- **Known latent issue** (from CONCERNS): `hang.dmp` in build dir suggests a past hang — not addressed in this milestone.

## Constraints

- **Tech stack**: C++17, Boost.Asio/Beast/SSL, libp2p, ipfs-lite-cpp, ipfs-bitswap-cpp, libssh2, OpenSSL, spdlog/soralog — unchanged this milestone
- **Compatibility**: Breaking public API change accepted (callback signature loses `parse` bool, classes renamed, headers deleted). Downstream consumers (SuperGenius) must adapt; no shims.
- **Platform**: Must keep building on Windows (MSVC, verified by build + tests). POSIX side of the new split must be structurally correct (compiles on paper); CI on Linux not available in this environment.
- **Style**: No platform `#ifdef`s in the local-file layer — separate files selected by CMake (boss requirement)
- **Dependencies**: MNN must no longer appear in any `find_package`/link/include — MNN becomes consumers' concern

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| `LocalFile` prefix for renamed classes (not `FILELoader`) | Avoids collision with `FileLoader.hpp` base-class header on Linux; keeps `file://` URL contract | — Pending |
| Full MNN purge in one milestone | Library responsibility is async I/O, not MNN integration; boss alignment | — Pending |
| Separate Windows/POSIX source+header pairs chosen in CMake | Boss rejects ifdef-based platform switching in implementation files | — Pending |
| Remove `parse` bool + entire parser layer; keep `save` bool | Parser layer is dead code (never initialized); `save` drives live auto-save behavior the user wants | — Pending |
| Accept breaking API change without shims | No shim maintenance burden; downstream consumers small and known | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-09-03 after initialization*
