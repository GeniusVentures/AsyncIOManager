<!-- GSD:project-start source:PROJECT.md -->

## Project

**AsyncIOManager**

A C++17 asynchronous I/O library for loading and saving files over multiple protocols (local `file://`, `https://`, `ipfs://`, `sftp://`, `wss://`), built on Boost.Asio. It provides a `FileManager` registry that dispatches URL-prefixed operations to per-protocol loader/saver strategies. Consumed downstream by GeniusNetwork components (e.g. SuperGenius) via the installed CMake package.

**Core Value:** Reliable async load/save of data across local and remote protocols behind one URL-dispatched `FileManager` API.

### Constraints

- **Tech stack**: C++17, Boost.Asio/Beast/SSL, libp2p, ipfs-lite-cpp, ipfs-bitswap-cpp, libssh2, OpenSSL, spdlog/soralog — unchanged this milestone
- **Compatibility**: Breaking public API change accepted (callback signature loses `parse` bool, classes renamed, headers deleted). Downstream consumers (SuperGenius) must adapt; no shims.
- **Platform**: Must keep building on Windows (MSVC, verified by build + tests). POSIX side of the new split must be structurally correct (compiles on paper); CI on Linux not available in this environment.
- **Style**: No platform `#ifdef`s in the local-file layer — separate files selected by CMake (boss requirement)
- **Dependencies**: MNN must no longer appear in any `find_package`/link/include — MNN becomes consumers' concern

<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->

## Technology Stack

## Languages & Runtime

- CMake scripting language (>= 3.20 required at root `CMakeLists.txt`; `build/Windows/CMakeLists.txt` still declares legacy `cmake_minimum_required(VERSION 3.5.1)`)
- JavaScript (Node.js) — test helper only: `test/websocketserver/websocket-server.js`
- Protobuf IDL — compiled to C++ via `compile_proto_to_cpp()` in `cmake/functions.cmake`

## Frameworks & Core Libraries

- Boost.Asio (sockets, `streambuf`, `async_read`, `post`) — used in every loader; see `include/FileLoader.hpp`, `src/MNNLoader.cpp`
- Boost.Beast WebSocket — `src/WSCommon.cpp` (`boost::beast::websocket::stream<boost::asio::ssl::stream<tcp::socket>>`)
- Boost.Asio SSL (OpenSSL backend) — HTTPS in `src/HTTPCommon.cpp` (`SslSocket = ssl::stream<tcp::socket>`), WSS in `src/WSCommon.cpp`
- libp2p — host/kademlia injectors, identify, ping, gossip, multiaddress, c-ares resolver; linked as `p2p::*` targets in `src/CMakeLists.txt`; outcome type (`libp2p::outcome::result`) is the universal error type in public APIs
- ipfs-lite-cpp — graphsync, cbor, ipld_node, blockservice, in-memory + RocksDB datastores, kad DHT (`src/CMakeLists.txt` lines 30–40)
- ipfs-bitswap-cpp — bitswap engine + `ipfs-bitswap-proto` / `ipfs-unixfs-proto` (linked PRIVATE), see `include/IPFSCommon.hpp`, `src/IPFSLoader.cpp`
- libssh2 — SFTP transport (`libssh2_session_*`, `libssh2_sftp_*`, `LIBSSH2_ERROR_EAGAIN` non-blocking loop pattern in `src/SFTPCommon.cpp`, `src/SFTPLoader.cpp`, `src/SFTPSaver.cpp`)
- cpp-httplib 0.14.2 — **vendored, header-only** at `include/httplib.h` (MIT, Yuji Hirose). NOTE: currently NOT `#include`d by any `.cpp`; live HTTP(S) code uses Boost.Asio/Beast directly (`src/HTTPCommon.cpp`). Treat as available-but-dormant.
- MNN (Alibaba) — `MNN/Interpreter.hpp`, `MNN/Tensor.hpp` via `include/MNNCommon.hpp`; model files `test/1.mnn`, `test/2.mnn`; example inference flow in `example/MNNExample.cpp`
- Protobuf (CONFIG, `protobuf::libprotobuf`, `protobuf::protoc` imported executable shim in root `CMakeLists.txt`)
- yaml-cpp — soralog logging config (embedded YAML string in `src/IPFSLoader.cpp`)
- RapidJSON, jsonrpc-lean, libsecp256k1, ed25519, xxhash, Snappy, zlib, SQLiteModernCpp, Microsoft.GSL, tsl_hat_trie, c-ares — located in `build/CommonBuildParameters.cmake` (super-build environment; only some are consumed by this repo's own targets)
- spdlog (external fmt via `SPDLOG_FMT_EXTERNAL`) — project logger factory `sgns::asiomgr::createLogger(tag, basepath)` in `include/asiomgr-logger.hpp` / `src/asiomgr-logger.cpp` (stdout_color_mt / basic_file_mt / android_sink)
- soralog — wraps libp2p logging with YAML configurator; initialized in `src/IPFSLoader.cpp` `LoadASync()` and `example/MNNExample.cpp`
- fmt — standalone dependency, also spdlog backend
- GTest + GMock (`GTest::gtest_main`, `GTest::gmock_main`) via `addtest()` helper in `cmake/functions.cmake`; xunit XML output to `${CMAKE_BINARY_DIR}/xunit/`; binaries land in `${CMAKE_BINARY_DIR}/test_bin`
- Boost.DI (`boost/di/extension/scopes/shared.hpp`) — libp2p injector composition in `example/MNNExample.cpp` and `test/testutil/bitswap_node.hpp`

## Build System

- `BUILD_TESTING` (default OFF) — enables GTest + `test/`
- `ASYNC_IO_MANAGER_NETWORK_TESTS` (`test/src/CMakeLists.txt`, default OFF) — HTTP/SFTP/IPFS network tests; generates self-signed cert via `openssl` executable into `${CMAKE_CURRENT_BINARY_DIR}/test_certs`
- `Boost_USE_STATIC_RUNTIME`, `SGNS_STACKTRACE_BACKTRACE` (`build/CommonBuildParameters.cmake`)

## Configuration

- No `.env` files or runtime config files; all configuration is constructor-injected (e.g. `SFTPDevice(sftp_host, sftp_path, sftp_user, sftp_pass, pubkeyfile, privkeyfile, privkeypass, ...)` in `include/SFTPCommon.hpp`)
- Logging config: YAML embedded as string constant `logger_config` in `src/IPFSLoader.cpp` (sinks: console, level critical, children libp2p/kademlia)
- URL scheme drives dispatch: prefixes `file://`, `http(s)://`, `ipfs://`, `sftp://`, `ws(s)://` parsed by `include/URLStringUtil.h` / `src/URLStringUtil.cpp` and routed by `FileManager` (`include/FileManager.hpp`)
- Singleton registration: loaders/parsers/savers self-register in their constructors (e.g. `MNNLoader::MNNLoader()` registers prefix `"file"` — `src/MNNLoader.cpp`); `FileManager::InitializeSingletons()` must be called first (`example/MNNExample.cpp` line ~217)

## Dependency Notes (versions, constraints, vendored libs)

- **Boost 1.85.0** — cached `BOOST_MAJOR_VERSION=1 MINOR=85 PATCH=0` in `build/CommonBuildParameters.cmake`; components: date_time, filesystem, random, regex, system, thread, log, log_setup, program_options (+ header-only: uuid, lexical_cast, format, beast, asio, di)
- **cpp-httplib 0.14.2** — vendored single header `include/httplib.h`; distributed with the installed headers (root `CMakeLists.txt` installs `include/*.h*`)
- **spdlog** — built against external fmt (`SPDLOG_FMT_EXTERNAL`); a comment in `build/CommonBuildParameters.cmake` says "spdlog v1.4.2"
- **OpenSSL** — static libs (`OPENSSL_USE_STATIC_LIBS ON`, MSVC static RT) per `build/CommonBuildParameters.cmake`; used for HTTPS/WSS transports and linked `PRIVATE` in `src/CMakeLists.txt`
- **Protobuf** — CONFIG mode with manual `protobuf::protoc` imported-executable shim (root `CMakeLists.txt`); depends on absl + utf8_range from the thirdparty tree
- **libp2p / ipfs-lite-cpp / ipfs-bitswap-cpp** — versionless in-repo, all CONFIG packages; must be found AFTER Boost ("Boost should be loaded before libp2p v0.1.2" — `build/CommonBuildParameters.cmake`)
- **RocksDB** — consumed indirectly via `ipfs-lite-cpp::ipfs_datastore_rocksdb` (`src/CMakeLists.txt` line 39); also requires Snappy in super-build
- **MNN** — CONFIG package; include dir is remapped to a sibling source tree in super-build (`set(MNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../MNN/include")` — `build/CommonBuildParameters.cmake`)
- **GTest** — optional; only required when `BUILD_TESTING=ON`
- **Node.js + ws ^8.15.1** — test-only, `test/websocketserver/`; servers on ports 8080 (WS) and 8090 (WSS)
- **libssh2** — CONFIG package; ZLIB resolution quirk documented in `build/CommonBuildParameters.cmake` (`CMAKE_FIND_PACKAGE_PREFER_CONFIG ON` workaround for Windows CI)

<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->

## Conventions

## Code Style (formatting, headers guards vs #pragma once, includes order)

- Indent: 4 spaces
- Brace style: Allman (opening brace on its own line) for classes, functions, namespaces, and control flow
- Parentheses are padded with spaces on the inside: `LoadASync( "file://" + tf.pathString(), false, false, runner.ioc(), ... )` (`src/FileManager.cpp:52-58`)
- `namespace sgns { ... } // End namespace sgns` with closing comment (`src/HTTPLoader.cpp:75`)
- Pointer/reference alignment: right side (`std::string &prefix`, `const std::string &url`)
- Line length: soft ~120 cols; long parameter lists wrapped with vertical alignment (see `include/FileManager.hpp:113-118`)
- Protocol loaders/savers/devices: `namespace sgns { ... }` (e.g. `HTTPLoader`, `IPFSDevice`, `MNNSaver`)
- Base interfaces `FileLoader` (`include/FileLoader.hpp`), `FileSaver` (`include/FileSaver.hpp`), `FileParser` (`include/FileParser.hpp`) are in the **global namespace**
- `FileManager` is also in the global namespace (`include/FileManager.hpp:39`)
- Logging: `namespace sgns::asiomgr` (`include/asiomgr-logger.hpp:12`)
- Every header using outcome re-exports it:

## Naming (files, classes, functions, enums)

- Protocol code follows a strict **triad**: `<PROTO>Loader`, `<PROTO>Saver`, `<PROTO>Common` — e.g. `HTTPLoader.hpp/.cpp`, `SFTPSaver.hpp/.cpp`, `IPFSCommon.hpp/.cpp`, `WSLoader.hpp/.cpp`
- Mixed extensions: `.hpp` for most headers; `.h` for `URLStringUtil.h` (utility) and vendored `httplib.h`
- Header/source pairs share the exact base name (`MNNLoader.hpp` ↔ `MNNLoader.cpp`)
- Logger files use kebab-case: `asiomgr-logger.hpp/.cpp`
- `PascalCase`: `FileManager`, `HTTPLoader`, `IPFSDevice`, `SFTPUploadDevice`, `FILEDevice`, `WSDevice`, `HTTPDevice`
- Device classes (async protocol engines) end in `Device`; registry-dispatched handlers end in `Loader`/`Saver`/`Parser`
- Test classes end in `Test`/`TestFixture`/`IntegrationTest` (`FileManagerIntegrationTest`, `MNNLoaderTest`, `IPFSSaverEdgeTest`)
- `PascalCase` for public API: `LoadFile`, `LoadASync`, `SaveASync`, `RegisterLoader`, `InitializeSingleton`, `StartHTTPDownload`, `StartSFTPDownload`, `StartFindingPeersWithRetry`
- `camelCase` for internal/lower-level helpers: `getURLComponents`, `parseHTTPUrl`, `parseSFTPUrl`, `parseIPFSUrl` (`include/URLStringUtil.h`), `setBitswap`, `clearBitswap`, `getCacheDir`
- Free URL parsers are declared `extern bool ...` in `include/URLStringUtil.h:11-14` (legacy C-style, not in a namespace)
- `m_` prefix for loggers: `sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "FileManager" );` (every class owns one — `include/FileManager.hpp:43`, `include/MNNLoader.hpp:63`, `include/SFTPSaver.hpp:37`)
- trailing underscore for state: `http_host_`, `http_path_`, `parse_`, `save_`, `cacheDir_`, `bitswapMutex_`, `outstandingOperations_`
- One exception: `outstandingOperations_` is trailing-underscore while older members on the same class are not — prefer trailing underscore for new members
- `enum class Error` nested in the owning class, values `SCREAMING_SNAKE_CASE`, starting at 1:

## Patterns (singleton usage, shared_ptr, async callbacks, per-protocol triads)

- `include/PLoader.hpp` + `src/PLoader.cpp` — thin singleton implementing `FileLoader`; parses URL, constructs a `PDevice`, delegates
- `include/PSaver.hpp` + `src/PSaver.cpp` — singleton implementing `FileSaver`
- `include/PCommon.hpp` + `src/PCommon.cpp` — the `PDevice` class holding the boost::asio/SSL/SSH state machine
- Example: `src/HTTPLoader.cpp:56-71` (`LoadASync` → `parseHTTPUrl` → `std::make_shared<HTTPDevice>` → `StartHTTPDownload`)
- Use `SINGLETON_PTR( ClassName )` macro from `include/ASIOSingleton.hpp` (raw-pointer variant) inside the class body; `FileManager` uses `SINGLETON_REF` (reference variant)
- Define `static void InitializeSingleton()` in the .cpp guarded by `if ( _instance == nullptr ) { _instance = new ClassName(); }` (`src/HTTPLoader.cpp:20-25`)
- The **constructor self-registers** with the FileManager registry under a URL prefix:
- Registration must not double-register: `InitializeSingleton()` is idempotent; callers invoke `FileManager::InitializeSingletons()` once (`src/FileManager.cpp:33-42`)
- Savers may register multiple prefixes: `MNNSaver` registers both `"file"` and `"mnn"` (`src/MNNSaver.cpp:37-41`)
- Async operations pass `std::shared_ptr<boost::asio::io_context> ioc` so the context can be stopped when work drains (`include/FileManager.hpp:113`)
- Payloads cross API boundaries as type-erased `std::shared_ptr<void>` (`FileLoader::LoadFile` return); callers `std::static_pointer_cast<std::string>` back
- Async device classes MUST inherit `std::enable_shared_from_this` and capture `self = shared_from_this()` in every continuation lambda so pending handlers keep the device alive (`src/HTTPCommon.cpp:148`, `include/IPFSCommon.hpp:61`, `src/SFTPSaver.cpp:27`). Regression tests enforce this (`test/src/ipfs_device_test.cpp`)
- Prefer capturing shared state as `std::make_shared<T>` rather than by-reference lambda captures when the callback outlives the scope (`test/src/ipfs_device_test.cpp:86-87`)
- Two callback tiers, both declared as `using` aliases on the class:
- `ResultType` is always `outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>` (paths + byte buffers), re-declared per class (`include/FileLoader.hpp:21-22`, `include/FileSaver.hpp:12-13`)
- Errors are delivered by posting a failure onto the io_context, never by throwing across the async boundary:
- Operation counting: `IncrementOutstandingOperations()` before starting, `DecrementOutstandingOperations( ioc )` in every completion path; hitting zero calls `ioc->stop()` (`src/FileManager.cpp:212-233`). `SaveASync` wraps the saver call in `try/catch(...)` so the counter is decremented before rethrowing (`src/FileManager.cpp:155-163`)

## Error Handling (exceptions vs result codes, logging levels)

- Create a per-class logger as a member: `sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "ClassName" );` — loggers are cached per tag (`src/asiomgr-logger.cpp:55-65`)
- Message pattern set globally: `[%Y-%m-%d %H:%M:%S][%l][%n] %v` (`src/asiomgr-logger.cpp:13-16`)
- Use **fmt-style `{}` placeholders, never string concatenation with runtime values** — NOTE: `src/FILECommon.cpp:25` still concatenates; that is the outlier, not the pattern
- Level discipline (observed across `src/`):
- Log from within async continuations via the captured `self->m_logger` (`src/HTTPCommon.cpp:167`)

## CMake Conventions (target naming, find_package usage, install/export)

- `option(BUILD_TESTING "Build tests" OFF)` — tests are **OFF by default**; when ON, `enable_testing()` + `find_package(GTest CONFIG REQUIRED)` (root `CMakeLists.txt:47-51`)
- `option(ASYNC_IO_MANAGER_NETWORK_TESTS ... OFF)` — second gate for HTTP/SFTP/IPFS tests (`test/src/CMakeLists.txt:34`)
- `addtest(<name> <sources...>)` function in `cmake/functions.cmake:10-33` is the ONLY way to register a test: creates the executable, links `GTest::gtest_main` + `GTest::gmock_main`, emits `--gtest_output=xml:${CMAKE_BINARY_DIR}/xunit/xunit-<name>.xml`, calls `add_test`, places binaries in `${CMAKE_BINARY_DIR}/test_bin`, and disables clang-tidy on tests
- Compile definitions for paths use quoted defines: `target_compile_definitions(http_loader_test PRIVATE TEST_CERT_DIR="${TEST_CERT_DIR}")` (`test/src/CMakeLists.txt:83-85`)
- `using namespace std;` at header scope (`include/FileSaver.hpp:6`) — leaks into every includer
- Unqualified `string`/`map` in headers relying on that using-directive (`include/FileManager.hpp:47-52`)
- Commented-out registration code left in `InitializeSingletons` (`src/FileManager.cpp:33-42`) and `std::system`-based server bootstrap in tests (`test/src/sftp_saver_test.cpp`) — acceptable in test code only
- `message(WARNING ...)` debug spew in `test/src/CMakeLists.txt:13-14` — do not add more

<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->

## Architecture

## Pattern Overview

- **`FileManager`** (`include/FileManager.hpp`, `src/FileManager.cpp`) is the central singleton **registry**: it maps URL prefixes (`file`, `https`, `ipfs`, `sftp`, `wss`) → `FileLoader*`, URL prefixes → `FileSaver*`, and file extensions → `FileParser*`.
- Each concrete loader/saver is itself a **singleton** (via `SINGLETON_PTR` macro from `include/ASIOSingleton.hpp`) that **self-registers** into `FileManager` from its constructor, e.g. `src/MNNLoader.cpp:36` → `FileManager::GetInstance().RegisterLoader( "file", this )`.
- **Strategy interfaces**: `FileLoader` (`include/FileLoader.hpp`), `FileParser` (`include/FileParser.hpp`), `FileSaver` (`include/FileSaver.hpp`) — pure virtual, implemented per protocol.
- **Per-protocol `*Common` device layer** implements the actual transport I/O as an async state machine over Boost.Asio: `HTTPDevice` (`include/HTTPCommon.hpp`), `IPFSDevice` (`include/IPFSCommon.hpp`), `SFTPDevice` (`include/SFTPCommon.hpp`), `WSDevice` (`include/WSCommon.hpp`), `FILEDevice` (`include/FILECommon.hpp`, platform-split Windows vs POSIX).
- `include/ASIOSingleton.hpp` provides the two singleton macros: `SINGLETON_REF` (Meyers static-local reference, used by `FileManager`) and `SINGLETON_PTR` (raw `new`-based pointer, used by all loaders/savers + `SINGLETON_PTR_INIT` in the .cpp).
- Result payloads use `libp2p::outcome` (`outcome::result<...>`) with the canonical type aliased everywhere as:

## Layers & Modules

```text

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

## Data Flow

### Async load (primary path)

### Async save

### Sync paths

- `FileManager::LoadFile(url, parse)` → `LoadFile` → optional `ParseData(suffix, data)` (`src/FileManager.cpp:164-198`); `FileManager::SaveFile(url, data)` (`src/FileManager.cpp:200`).
- Sync `HTTPLoader::LoadFile` returns a dummy static string (stub, `src/HTTPLoader.cpp:34-40`).

### MNN model lifecycle

### IPFS bitswap/DHT injection

## Key Abstractions

- `LoadFile(filename) -> shared_ptr<void>` (sync) and `LoadASync(filename, parse, save, ioc, CompletionCallback) -> shared_ptr<void>` (async).
- Defines `ResultType` and `CompletionCallback = function<void(shared_ptr<io_context>, ResultType, bool parse, bool save)>`.
- `SaveFile(filename, data)` (sync, throws on error) and `SaveASync(ioc, handle_write, filename, ResultType, suffix, save_location = nullptr)`.
- `save_location` out-param is the cross-protocol "resulting address" (file path, IPFS CID, SFTP URL).
- `ParseData(shared_ptr<void>)` / `ParseASync(shared_ptr<vector<char>>)` — registered by suffix; only concrete implementations currently disabled.
- Transport-level state machines, `enable_shared_from_this`, each with its own `Error` enum (`OUTCOME_CPP_DEFINE_CATEGORY_3` in the .cpp) and shared `CompletionCallback` signature.
- Header-only macro framework. Reference-style for `FileManager`; pointer-style for handlers (never freed — process lifetime).
- Wraps spdlog with console/file sinks (Android sink under `ANDROID`), pattern `[%Y-%m-%d %H:%M:%S][%l][%n] %v`. Every class holds `m_logger = sgns::asiomgr::createLogger("<ClassName>")`.

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

<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->

## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, `.github/skills/`, or `.codex/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->

## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:

- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->

<!-- GSD:profile-start -->

## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
