---
focus: arch
created: 2026-09-03
last_mapped_commit: 009dc3dc04599c61d04aca5ba0cf547703543b31
---
# Architecture

## Pattern Overview

**Registry + Strategy over URL-scheme dispatch, all built on Boost.Asio async I/O.**

- **`FileManager`** (`include/FileManager.hpp`, `src/FileManager.cpp`) is the central singleton **registry**: it maps URL prefixes (`file`, `https`, `ipfs`, `sftp`, `wss`) → `FileLoader*`, URL prefixes → `FileSaver*`, and file extensions → `FileParser*`.
- Each concrete loader/saver is itself a **singleton** (via `SINGLETON_PTR` macro from `include/ASIOSingleton.hpp`) that **self-registers** into `FileManager` from its constructor, e.g. `src/MNNLoader.cpp:36` → `FileManager::GetInstance().RegisterLoader( "file", this )`.
- **Strategy interfaces**: `FileLoader` (`include/FileLoader.hpp`), `FileParser` (`include/FileParser.hpp`), `FileSaver` (`include/FileSaver.hpp`) — pure virtual, implemented per protocol.
- **Per-protocol `*Common` device layer** implements the actual transport I/O as an async state machine over Boost.Asio: `HTTPDevice` (`include/HTTPCommon.hpp`), `IPFSDevice` (`include/IPFSCommon.hpp`), `SFTPDevice` (`include/SFTPCommon.hpp`), `WSDevice` (`include/WSCommon.hpp`), `FILEDevice` (`include/FILECommon.hpp`, platform-split Windows vs POSIX).
- `include/ASIOSingleton.hpp` provides the two singleton macros: `SINGLETON_REF` (Meyers static-local reference, used by `FileManager`) and `SINGLETON_PTR` (raw `new`-based pointer, used by all loaders/savers + `SINGLETON_PTR_INIT` in the .cpp).
- Result payloads use `libp2p::outcome` (`outcome::result<...>`) with the canonical type aliased everywhere as:
  `ResultType = outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>` (parallel arrays: file paths + content buffers).

## Layers & Modules

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                    Consumers / Entry Points                              │
│   `example/MNNExample.cpp` · `test/src/*_test.cpp` · downstream apps      │
│   (installed via `AsyncIOManagerTargets` CMake export)                   │
├──────────────────────────────────────────────────────────────────────────┤
│                     Facade + Registry (singleton)                        │
│   `include/FileManager.hpp` / `src/FileManager.cpp`                      │
│   LoadASync · SaveASync · LoadFile · SaveFile · ParseData                │
│   outstandingOperations_ counter → ioc->stop()                           │
├───────────────────────────┬──────────────────────┬───────────────────────┤
│  Loader strategies        │  Saver strategies    │  Parser strategies    │
│  (prefix → FileLoader*)   │  (prefix→FileSaver*) │  (ext→FileParser*)    │
│  `src/MNNLoader.cpp` file │  `src/MNNSaver.cpp`  │  (parser layer stub-  │
│  `src/HTTPLoader.cpp` https│   file, mnn         │  bed; MNNParser not   │
│  `src/IPFSLoader.cpp` ipfs│  `src/IPFSSaver.cpp` │  initialized — see    │
│  `src/SFTPLoader.cpp` sftp│   ipfs               │  FileManager::        │
│  `src/WSLoader.cpp` wss   │  `src/SFTPSaver.cpp` │  InitializeSingletons)│
│                           │   sftp               │                       │
├───────────────────────────┴──────────────────────┴───────────────────────┤
│              Protocol Device layer (async state machines)                │
│  `src/FILECommon.cpp` · `src/HTTPCommon.cpp` · `src/IPFSCommon.cpp`      │
│  `src/SFTPCommon.cpp` · `src/WSCommon.cpp`                               │
│  Each Device: resolve → connect/handshake/auth → read/write chunks →     │
│  invoke CompletionCallback on caller's io_context                        │
├──────────────────────────────────────────────────────────────────────────┤
│                      Cross-cutting utilities                             │
│  `src/URLStringUtil.cpp` (`getURLComponents`/`parseHTTPUrl`/             │
│  `parseSFTPUrl`/`parseIPFSUrl`) · `src/asiomgr-logger.cpp` (spdlog       │
│  wrapper, `sgns::asiomgr::createLogger`) · `include/ASIOSingleton.hpp`   │
│  macros · vendored `include/httplib.h` (cpp-httplib, currently unused    │
│  by runtime code — HTTPDevice uses boost::asio::ssl directly)            │
├──────────────────────────────────────────────────────────────────────────┤
│                          External dependencies                           │
│  Boost.Asio/Beast/SSL/DI · libp2p · ipfs-lite-cpp · ipfs-bitswap-cpp     │
│  MNN · libssh2 · OpenSSL · spdlog/soralog · RocksDB · yaml-cpp · fmt     │
└──────────────────────────────────────────────────────────────────────────┘
```

### Registered handlers (verified from constructors)

| Prefix | Handler | Class | Registers at |
|--------|---------|-------|--------------|
| `file` (load) | MNNLoader | `sgns::MNNLoader` | `src/MNNLoader.cpp:36` |
| `https` (load) | HTTPLoader | `sgns::HTTPLoader` | `src/HTTPLoader.cpp:31` (`http` commented out) |
| `ipfs` (load) | IPFSLoader | `sgns::IPFSLoader` | `src/IPFSLoader.cpp:48` |
| `sftp` (load) | SFTPLoader | `sgns::SFTPLoader` | `src/SFTPLoader.cpp:27` — **NOT initialized** in `InitializeSingletons` |
| `wss` (load) | WSLoader | `sgns::WSLoader` | `src/WSLoader.cpp:35` — **NOT initialized** in `InitializeSingletons` (`ws` commented out) |
| `file` (save) | MNNSaver | `sgns::MNNSaver` | `src/MNNSaver.cpp:42` |
| `mnn` (save) | MNNSaver | `sgns::MNNSaver` | `src/MNNSaver.cpp:43` |
| `ipfs` (save) | IPFSSaver | `sgns::IPFSSaver` | `src/IPFSSaver.cpp:28` |
| `sftp` (save) | SFTPSaver | `sgns::SFTPSaver` | `src/SFTPSaver.cpp:482` |

`FileManager::InitializeSingletons()` (`src/FileManager.cpp:32-43`) activates: MNNLoader, HTTPLoader, IPFSLoader, IPFSSaver, MNNSaver, SFTPSaver. MNNParser/SFTPLoader/WSLoader lines are commented out.

## Data Flow

### Async load (primary path)

1. Caller supplies URL + `std::shared_ptr<boost::asio::io_context>` and a `FinalCallback` → `FileManager::LoadASync` (`src/FileManager.cpp:52`).
2. `getURLComponents(url, prefix, filePath, suffix)` (`src/URLStringUtil.cpp:8`) splits `scheme://path.ext`; unknown prefix throws `std::range_error`.
3. `IncrementOutstandingOperations()`; `FileManager` wraps a `handle_read` lambda chaining: optional parse (currently stubbed/commented), optional `SaveASync` chain via the `savetype` saver, `DecrementOutstandingOperations`, then `finalcall(buffers)`.
4. Loader::LoadASync creates its protocol Device:
   - FILE: `FILEDevice` + `boost::asio::async_read` (`src/MNNLoader.cpp:57`)
   - HTTPS: `parseHTTPUrl` → `HTTPDevice::StartHTTPDownload` over `boost::asio::ssl::stream` (`src/HTTPLoader.cpp:35-55`)
   - IPFS: parses CID via `parseIPFSUrl` + `ContentIdentifierCodec`, DHT peer discovery → bitswap wantlist (`src/IPFSLoader.cpp`, `src/IPFSCommon.cpp`)
   - WSS: `WSDevice` over `boost::beast::websocket::ssl_stream` (`src/WSLoader.cpp:66`)
5. Device completes → invokes `CompletionCallback(ioc, ResultType, parse, save)` posted on the caller's `ioc`.
6. `handle_read` runs: save-chain or direct `DecrementOutstandingOperations(ioc)` (`src/FileManager.cpp:211-227`) — when the counter reaches 0 it calls **`ioc->stop()`**, ending the caller's `ioc->run()` loop. Then `finalcall(buffers)` delivers data to the application.

### Async save

1. `FileManager::SaveASync(url, data, ioc, finalcall, save_location)` (`src/FileManager.cpp:110`) — prefix lookup in `savers` map; unknown prefix throws.
2. Saver implementations:
   - `MNNSaver::SaveASync` (`src/MNNSaver.cpp`) — writes path/content buffer pairs to local disk.
   - `IPFSSaver::SaveASync` (`src/IPFSSaver.cpp`) — publishes UnixFS DAG via bitswap, optionally announces CID via DHT (`AnnounceCID`), writes resulting CID into the `save_location` out-param.
   - `SFTPSaver::SaveASync` (`src/SFTPSaver.cpp`) — libssh2 upload with pubkey/password auth.
3. `handle_write` fires → `DecrementOutstandingOperations(ioc)` → `finalcall(data)`.

### Sync paths

- `FileManager::LoadFile(url, parse)` → `LoadFile` → optional `ParseData(suffix, data)` (`src/FileManager.cpp:164-198`); `FileManager::SaveFile(url, data)` (`src/FileManager.cpp:200`).
- Sync `HTTPLoader::LoadFile` returns a dummy static string (stub, `src/HTTPLoader.cpp:34-40`).

### MNN model lifecycle

`.mnn` bytes (from `file://`, `https://`, `ipfs://`, …) arrive as `ResultType` buffers → optionally parsed into MNN `Interpreter` (parser currently disabled; `include/MNNCommon.hpp` centralizes `MNN/Interpreter.hpp` includes) → `MNNSaver` writes the model back to disk (`file://`, `mnn://` prefixes) or `IPFSSaver` republishes to IPFS. See `example/MNNExample.cpp` for the full libp2p host + bitswap bootstrap feeding `FileManager::setBitswap`.

### IPFS bitswap/DHT injection

External libp2p hosts inject via `FileManager::setBitswap(bitswap, dht)` (`src/FileManager.cpp:236`) which forwards to `IPFSLoader::setBitswap` / `IPFSSaver`; `IPFSDevice` supports both a process-wide `getInstance(ioc)` singleton and `createWithBitswap(ioc, bitswap, dht)` for externally-owned nodes (`include/IPFSCommon.hpp`). `clearBitswap` uses owner-matching semantics (verified by `test/src/ipfs_saver_test.cpp:63`).

## Key Abstractions

**`FileLoader`** (`include/FileLoader.hpp`):
- `LoadFile(filename) -> shared_ptr<void>` (sync) and `LoadASync(filename, parse, save, ioc, CompletionCallback) -> shared_ptr<void>` (async).
- Defines `ResultType` and `CompletionCallback = function<void(shared_ptr<io_context>, ResultType, bool parse, bool save)>`.

**`FileSaver`** (`include/FileSaver.hpp`):
- `SaveFile(filename, data)` (sync, throws on error) and `SaveASync(ioc, handle_write, filename, ResultType, suffix, save_location = nullptr)`.
- `save_location` out-param is the cross-protocol "resulting address" (file path, IPFS CID, SFTP URL).

**`FileParser`** (`include/FileParser.hpp`):
- `ParseData(shared_ptr<void>)` / `ParseASync(shared_ptr<vector<char>>)` — registered by suffix; only concrete implementations currently disabled.

**`*Device` classes** (`include/*Common.hpp`):
- Transport-level state machines, `enable_shared_from_this`, each with its own `Error` enum (`OUTCOME_CPP_DEFINE_CATEGORY_3` in the .cpp) and shared `CompletionCallback` signature.

**`SINGLETON_REF` / `SINGLETON_PTR` / `SINGLETON_PTR_INIT`** (`include/ASIOSingleton.hpp`):
- Header-only macro framework. Reference-style for `FileManager`; pointer-style for handlers (never freed — process lifetime).

**`sgns::asiomgr::createLogger(tag)`** (`include/asiomgr-logger.hpp`, `src/asiomgr-logger.cpp`):
- Wraps spdlog with console/file sinks (Android sink under `ANDROID`), pattern `[%Y-%m-%d %H:%M:%S][%l][%n] %v`. Every class holds `m_logger = sgns::asiomgr::createLogger("<ClassName>")`.

**`sgns::AsyncError` (`Success` / `CustomResult`)** — referenced by `include/SFTPCommon.hpp:20-22` via `#include "FILEError.hpp"`; that header is **not in this repo** and resolves from an external dependency include path.

## Entry Points

- **Example app**: `example/MNNExample.cpp` → `MNNExample` target (`example/CMakeLists.txt`). Full demo: soralog YAML config, Boost.DI libp2p host injector (Noise security), ProtocolFactory (identify only), bitswap bootstrap, then `FileManager` load loop. Root `MNNExample.cpp` is an older, smaller legacy copy (not built by the root or example CMakeLists).
- **Tests**: `test/src/*_test.cpp` (GTest, registered via `addtest()` in `test/src/CMakeLists.txt`); fixtures in `test/base_mnn_test.hpp` and `test/testutil/test_fixture.hpp`; helpers in `test/testutil/` (`temp_file.hpp`, `asio_helpers.hpp`, `bitswap_node.hpp`).
- **Installed library consumers**: `find_package(AsyncIOManager)` → `AsyncIOManagerConfig.cmake` (generated from `cmake/config.cmake.in`) → `AsyncIOManagerTargets.cmake` (exported from root `CMakeLists.txt:82-89`) → link static lib `AsyncIOManager`, headers installed from `include/`.
- **Platform wrapper builds**: `build/Windows/CMakeLists.txt`, `build/Linux/CMakeLists.txt`, `build/OSX/CMakeLists.txt` + `build/CommonBuildParameters.cmake` provide an alternative superbuild-style configure pointing at a prebuilt thirdparty tree (`_THIRDPARTY_BUILD_DIR`) with static CRT (`/MT`) on Windows.

## Threading & Async Model

- **Single `io_context`, caller-owned**: every async API takes `std::shared_ptr<boost::asio::io_context>`; the caller runs it (dedicated thread, e.g. `IOContextRunner` in `test/testutil/asio_helpers.hpp`, or `ioc->run()` after posting work as in `example/MNNExample.cpp`).
- **Outstanding-operations counting**: `FileManager::outstandingOperations_` (plain `int`) increments per dispatched load/save and decrements in completion callbacks; reaching 0 calls `ioc->stop()` (`src/FileManager.cpp:211-227`). Note: the counter is not atomic — all mutations are expected to occur on io threads of the (typically single) shared context.
- **No strands** are used anywhere; serialization relies on single-context execution.
- **Work guards**: callers keep the context alive with `executor_work_guard` (example) or `boost::asio::io_context::work` (`test/testutil/asio_helpers.hpp`).
- **libp2p/bitswap co-threading**: bitswap and DHT run on the same shared `ioc`; `FileManager::bitswapMutex_` (`include/FileManager.hpp:73`) guards bitswap pointer swaps between application thread and io thread.
- **Global mutable state**: `IPFSDevice::instance_` static singleton (`include/IPFSCommon.hpp`), per-handler `SINGLETON_PTR` statics (e.g. `IPFSLoader::_instance`, `src/IPFSLoader.cpp:23`), `HTTPDevice::s_verify_peer` static SSL toggle (`include/HTTPCommon.hpp:75`), and the raw-pointer `loaders`/`parsers`/`savers` maps inside the `FileManager` singleton.
- **Platform split in FILEDevice** (`src/FILECommon.cpp:10`, `include/FILECommon.hpp`): POSIX uses `boost::asio::posix::stream_descriptor`, Windows uses `boost::asio::stream_file`; selected via `#ifndef _WIN32` at compile time.
- **Windows specifics**: root `CMakeLists.txt` defines `_WIN32_WINNT=0x0601` and `BOOST_BIND_GLOBAL_PLACEHOLDERS`; `build/Windows/CMakeLists.txt` forces `/MT` static runtime and `ws2_32`/`crypt32`/`userenv` link libs.

## Error Handling

- **Async**: `outcome::result` values delivered through callbacks; error categories via `OUTCOME_CPP_DEFINE_CATEGORY_3` per class (`Error` enums in each Loader/Device header).
- **Sync + dispatch**: exceptions — `std::range_error("No loader registered for prefix ...")` for unknown prefixes (`src/FileManager.cpp:60`, `:122`, `:190`), `std::range_error` for file open failures (`src/MNNLoader.cpp:39`, `:45`). `SaveASync` wraps saver calls in `try/catch(...)` to guarantee the outstanding-operations counter is decremented before rethrow (`src/FileManager.cpp:140-146`).
- **SFTP progress**: additional `StatusCallback` surfaced per stage (`sgns::AsyncError::CustomResult`, `src/SFTPCommon.cpp`).

## Anti-Patterns to Avoid (current state — do not extend)

- **Commented-out registrations** in `FileManager::InitializeSingletons` (`src/FileManager.cpp:32-43`) and loader ctors (`src/HTTPLoader.cpp:30`, `src/WSLoader.cpp:36`) — dead handler code paths.
- **Stub sync loaders**: `HTTPLoader::LoadFile` / `IPFSLoader::LoadFile` return dummy strings rather than data.
- **Empty parse branch** in the `LoadASync` handler (`src/FileManager.cpp:74-79` — commented out; hardcodes `parsers.find("mnn")` lookup).
- **Global `using namespace std;`** in `include/FileSaver.hpp:8` and `using namespace boost::asio;` inside namespace `sgns` in `*Common.hpp` headers.
- **Build artifacts committed** under `build/Debug` (`.vcxproj`, `CMakeCache.txt`, `hang.dmp`).

---

*Architecture analysis: 2026-09-03*
