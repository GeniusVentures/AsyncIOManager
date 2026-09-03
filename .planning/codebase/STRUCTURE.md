---
focus: arch
created: 2026-09-03
last_mapped_commit: 009dc3dc04599c61d04aca5ba0cf547703543b31
---
# Directory Structure

## Top-Level Layout

```text
AsyncIOManager/                     # w:\gnus\GeniusNetwork\thirdparty\AsyncIOManager
├── CMakeLists.txt                  # Root configure: deps, C++17 toolchain, install/export of AsyncIOManager
├── MNNExample.cpp                  # Legacy example copy (NOT built; prefer example/MNNExample.cpp)
├── README.md                       # Design notes (singleton pattern, mermaid class diagrams)
├── .git-blame-ignore-revs / .gitignore
├── .planning/                      # GSD planning docs (this analysis)
├── build/                          # Alternative platform superbuild wrappers + committed VS artifacts
│   ├── CMakeLists-chain: CommonBuildParameters.cmake, CommonCompilerOptions.CMake,
│   │   CompilationFlags.cmake
│   ├── Linux/CMakeLists.txt        # Linux wrapper build
│   ├── OSX/CMakeLists.txt          # macOS wrapper build
│   ├── Windows/CMakeLists.txt      # Windows wrapper build (/MT static CRT, ws2_32/crypt32)
│   ├── Debug/                      # COMMITTED build outputs: .sln, .vcxproj, CMakeCache.txt, hang.dmp
│   └── Release/                    # (may exist per-config)
├── cmake/                          # Reusable CMake modules
│   ├── common.cmake, definition.cmake, functions.cmake, install.cmake, print.cmake
│   ├── config.cmake.in             # Package config template → AsyncIOManagerConfig.cmake
│   ├── compile_option_by_platform/Windows.cmake
│   └── toolchain/cxx17.cmake       # Default toolchain (referenced by root CMakeLists.txt:3)
├── example/
│   ├── CMakeLists.txt              # add_executable(MNNExample MNNExample.cpp) — links AsyncIOManager
│   └── MNNExample.cpp              # Primary demo: libp2p host + bitswap bootstrap + FileManager usage
├── include/                        # PUBLIC headers — installed wholesale by root CMakeLists.txt:74
│   ├── Core interfaces: FileLoader.hpp, FileSaver.hpp, FileParser.hpp, FileManager.hpp
│   ├── Utilities:   ASIOSingleton.hpp, URLStringUtil.h, asiomgr-logger.hpp, httplib.h (vendored)
│   └── Per-protocol triads (see Key Locations)
├── src/                            # Library implementation → STATIC lib target "AsyncIOManager"
│   ├── CMakeLists.txt
│   ├── FileManager.cpp, URLStringUtil.cpp, asiomgr-logger.cpp, FILECommon.cpp
│   ├── HTTPCommon.cpp / HTTPLoader.cpp
│   ├── IPFSCommon.cpp / IPFSLoader.cpp / IPFSSaver.cpp
│   ├── MNNLoader.cpp / MNNSaver.cpp
│   ├── SFTPCommon.cpp / SFTPLoader.cpp / SFTPSaver.cpp
│   └── WSCommon.cpp / WSLoader.cpp
└── test/
    ├── CMakeLists.txt              # include dirs + add_subdirectory(testutil, src)
    ├── 1.mnn, 2.mnn                # MNN model fixtures (binary)
    ├── base_mnn_test.hpp           # BaseMNNTest fixture (boost::filesystem temp dirs)
    ├── src/
    │   ├── CMakeLists.txt          # addtest() targets; network tests behind
    │   │                           #   ASYNC_IO_MANAGER_NETWORK_TESTS=ON
    │   └── *_test.cpp              # GTest suites (see Test Assets)
    ├── testutil/                   # INTERFACE lib "asiomgr_testutil"
    │   ├── CMakeLists.txt
    │   ├── test_fixture.hpp        # FileManagerTestFixture + ResultType builders
    │   ├── temp_file.hpp           # RAII TempFile
    │   ├── asio_helpers.hpp        # IOContextRunner (background io thread)
    │   └── bitswap_node.hpp        # BitswapNode — full libp2p host + bitswap test node
    └── websocketserver/            # Node.js WSS test server
        ├── websocket-server.js     # 'ws' secure server; serves ./certs/{key,cert}.pem
        ├── package.json
        └── README.md
```

## Key Locations

**Core abstractions (where the architecture lives):**
- URL→handler dispatch table + facade: `include/FileManager.hpp`, `src/FileManager.cpp`
- Strategy interfaces: `include/FileLoader.hpp`, `include/FileSaver.hpp`, `include/FileParser.hpp`
- Singleton macros: `include/ASIOSingleton.hpp` (`SINGLETON_REF`, `SINGLETON_PTR`, `SINGLETON_PTR_INIT`)
- URL parsing utilities: `include/URLStringUtil.h`, `src/URLStringUtil.cpp` (`getURLComponents`, `parseHTTPUrl`, `parseSFTPUrl`, `parseIPFSUrl`)
- Logging: `include/asiomgr-logger.hpp`, `src/asiomgr-logger.cpp`

**Per-protocol triads** — Loader/Saver strategies in `src/`, transport Device in `*Common` pair:

| Protocol | Loader | Saver | Device/transport | Registered prefixes |
|----------|--------|-------|------------------|---------------------|
| Local file / MNN | `src/MNNLoader.cpp` | `src/MNNSaver.cpp` | `src/FILECommon.cpp` (`FILEDevice`) | load `file`; save `file`, `mnn` |
| HTTP(S) | `src/HTTPLoader.cpp` | — | `src/HTTPCommon.cpp` (`HTTPDevice`, boost::asio::ssl) | load `https` |
| IPFS | `src/IPFSLoader.cpp` | `src/IPFSSaver.cpp` | `src/IPFSCommon.cpp` (`IPFSDevice`, libp2p/bitswap/DHT) | load+save `ipfs` |
| SFTP | `src/SFTPLoader.cpp` (not init'd) | `src/SFTPSaver.cpp` | `src/SFTPCommon.cpp` (`SFTPDevice`, libssh2) | load `sftp`; save `sftp` |
| WebSocket | `src/WSLoader.cpp` (not init'd) | — | `src/WSCommon.cpp` (`WSDevice`, boost::beast) | load `wss` |

Protocol headers mirror this in `include/`: `MNNLoader.hpp`/`MNNSaver.hpp`/`MNNCommon.hpp`, `HTTPLoader.hpp`/`HTTPCommon.hpp`, `IPFSLoader.hpp`/`IPFSSaver.hpp`/`IPFSCommon.hpp`, `SFTPLoader.hpp`/`SFTPSaver.hpp`/`SFTPCommon.hpp`, `WSLoader.hpp`/`WSCommon.hpp`.

**CMake machinery:**
- Root `CMakeLists.txt` — dependency discovery (MNN, Boost, libp2p, ipfs-lite-cpp, ipfs-bitswap-cpp, libssh2, OpenSSL, spdlog, soralog, RocksDB, yaml-cpp, fmt, tsl_hat_trie, Boost.DI, Protobuf), `add_subdirectory(src)`, install/export.
- `src/CMakeLists.txt` — `add_library(AsyncIOManager STATIC ...)` (14 sources) + `target_link_libraries` (PUBLIC p2p::* / ipfs-lite-cpp::* / libssh2; PRIVATE OpenSSL, ipfs-bitswap-cpp); hooks `test/` via `if(BUILD_TESTING)`.
- `cmake/functions.cmake` — `addtest()` (GTest executable + xunit XML output to `${CMAKE_BINARY_DIR}/xunit/`, binaries to `test_bin/`), `addtest_part()`, `compile_proto_to_cpp()`, `disable_clang_tidy()`.
- `cmake/config.cmake.in` — package config template (includes `AsyncIOManagerTargets.cmake`).
- `build/CommonBuildParameters.cmake` — superbuild variant: hardcodes `_THIRDPARTY_BUILD_DIR` paths for GTest/protobuf/OpenSSL/rocksdb/etc., then `add_subdirectory` of `src/`, `test/` (gated), `example/`.

**Where to add new code:**
- New protocol support: add `{PROTO}Loader.hpp/.cpp`, optionally `{PROTO}Saver.hpp/.cpp` and `{PROTO}Common.hpp/.cpp` in `include/` + `src/`; append sources to the `add_library` list in `src/CMakeLists.txt`; self-register prefix in the ctor; enable in `FileManager::InitializeSingletons()` (`src/FileManager.cpp:32`).
- New URL parsing helper: declare in `include/URLStringUtil.h`, implement in `src/URLStringUtil.cpp`.
- New consumer example: new dir under `example/` + `add_executable` linking `AsyncIOManager` (pattern in `example/CMakeLists.txt`).
- New test: `{name}_test.cpp` in `test/src/` + `addtest({name}_test {name}_test.cpp)` + `target_link_libraries(... AsyncIOManager asiomgr_testutil)` in `test/src/CMakeLists.txt` (network-dependent ones inside the `ASYNC_IO_MANAGER_NETWORK_TESTS` guard).
- Shared test helpers: header-only files in `test/testutil/` (auto-available via `asiomgr_testutil` INTERFACE include dir).

## Naming Conventions

**Protocol role suffixes (strict triad):**
- `*Loader` — `FileLoader` subclass, async/sync read, registered by URL prefix (`MNNLoader`, `HTTPLoader`, `IPFSLoader`, `SFTPLoader`, `WSLoader`).
- `*Saver` — `FileSaver` subclass, async/sync write (`MNNSaver`, `IPFSSaver`, `SFTPSaver`).
- `*Common` — transport device + shared includes for a protocol (`FILECommon`, `HTTPCommon`, `IPFSCommon`, `SFTPCommon`, `WSCommon`); device classes named `{PROTO}Device` (`FILEDevice`, `HTTPDevice`, `IPFSDevice`, `SFTPDevice`, `WSDevice`).
- `MNNCommon.hpp` is the exception: pure include-aggregator for MNN headers (no device class).

**File casing:**
- C++ headers: PascalCase `.hpp` (`FileManager.hpp`, `HTTPCommon.hpp`) — the established pattern for new headers.
- Legacy C-style utilities keep `.h` (`URLStringUtil.h`) — do not propagate.
- Sources: PascalCase `.cpp` matching their header (`FileManager.cpp`, `HTTPCommon.cpp`); the only kebab-case outlier is `asiomgr-logger.cpp`/`asiomgr-logger.hpp` (logger module, keep as-is).
- Tests: snake_case `{subject}_test.cpp` (`mnn_loader_test.cpp`, `ipfs_device_test.cpp`).

**Code conventions observed:**
- Namespace `sgns` for all library code (loggers in `sgns::asiomgr`); outcome aliases re-exported at file top: `namespace outcome { using libp2p::outcome::...; }`.
- Classes: PascalCase; members: `snake_case_` trailing underscore for privates (`http_host_`, `outstandingOperations_` is the snake/camel outlier); methods: PascalCase (`LoadFile`, `SaveASync`, `StartHTTPDownload`, `getURLComponents` free functions are lowerCamel).
- Singleton handlers: `static void InitializeSingleton()` + private ctor that self-registers with `FileManager`.
- Error enums: `enum class Error { ... }` inside each class + `OUTCOME_CPP_DEFINE_CATEGORY_3` in the .cpp.
- Logger member: `sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "<ClassName>" );`

## Build Outputs

**Two configure paths exist:**

1. **Root project** (`cmake -S . -B <dir>`): standard CMake; produces static lib `AsyncIOManager` + optional `MNNExample` (`example/` must be added manually — root CMakeLists does NOT `add_subdirectory(example)`) and tests when `-DBUILD_TESTING=ON` (plus `-DASYNC_IO_MANAGER_NETWORK_TESTS=ON` for HTTP/SFTP/IPFS suites). Install exports `AsyncIOManagerTargets.cmake` + `AsyncIOManagerConfig.cmake` + `AsyncIOManagerConfigVersion.cmake` to `lib/cmake/AsyncIOManager/`.
2. **Platform wrapper** (`cmake -S build/Windows -B ...` with `PROJECT_ROOT`/`_THIRDPARTY_BUILD_DIR` cache vars): superbuild against a prebuilt dependency tree; static CRT (`/MT`), forces `/FORCE:MULTIPLE`, installs `MNNExample` to `${BUILD_FILELOADER_DIR}/bin`.

**Committed VS build tree** (`build/Debug/`): `AsyncIOManager.sln`, `generated.vcxproj`, `CMakeCache.txt`, `CTestTestfile.cmake`, plus subdirs `AsyncIOManager/`, `CMakeFiles/`, `example/`, `src/`, `test/`, `test_bin/`, and a `hang.dmp` crash dump. Test binaries land in `build/Debug/test_bin/`, xunit XML in `build/Debug/xunit/` (per `cmake/functions.cmake`).

**Version**: `VERSION_STRING 0`, `SUBVERSION_STRING 1` (root `CMakeLists.txt:70-71`) → package version 0.1, `AnyNewerVersion` compatibility.

## Test Assets

**GTest suites** (`test/src/`, via `addtest()`):

| Suite | File | Gate |
|-------|------|------|
| MNNLoaderTest | `test/src/mnn_loader_test.cpp` | always (BUILD_TESTING) |
| MNNSaverTest | `test/src/mnn_saver_test.cpp` | always |
| FileManagerIntegrationTest | `test/src/filemanager_test.cpp` | always |
| HTTPLoaderTest | `test/src/http_loader_test.cpp` | network gate; embeds a Boost.Beast HTTPS server, self-signed cert generated at configure time into `${CMAKE_CURRENT_BINARY_DIR}/test_certs/` (path injected as `TEST_CERT_DIR` define) |
| SFTPSaverTest | `test/src/sftp_saver_test.cpp` | network gate; requires local sshd |
| IPFSIntegrationTest | `test/src/ipfs_loader_test.cpp` | network gate; spins `BitswapNode` publisher + DHT discovery |
| IPFSSaverEdgeTest | `test/src/ipfs_saver_test.cpp` | network gate |
| IPFSDeviceRetryTest | `test/src/ipfs_device_test.cpp` | network gate; retry/lifetime semantics |

**Fixtures & helpers:**
- `test/testutil/test_fixture.hpp` — `FileManagerTestFixture` (calls `FileManager::InitializeSingletons()` in SetUp; `makeSingleFileResult`/`makeMultiFileResult`/`unwrapResult` builders for `ResultType`).
- `test/testutil/bitswap_node.hpp` — full libp2p host + bitswap + DHT node builder (Boost.DI injector, Noise); heavy but self-contained.
- `test/testutil/asio_helpers.hpp` — `IOContextRunner` (io_context + work guard + background thread; RAII stop).
- `test/testutil/temp_file.hpp` — `TempFile` RAII wrapper writing under `std::filesystem::temp_directory_path()`.
- `test/base_mnn_test.hpp` — `test::BaseMNNTest` fixture (boost::filesystem dir create/clear per test).
- `test/asiomgr_testutil` — INTERFACE library providing all of the above include paths (`test/testutil/CMakeLists.txt`).

**Binary fixtures:** `test/1.mnn`, `test/2.mnn` — MNN models used by loader/saver tests and the example (`file://../test/1.mnn` default).

**Node.js WS test server** (`test/websocketserver/`): `websocket-server.js` — `ws` secure server (`wss`) verifying/sanitizing resource paths, requires `./certs/key.pem` + `./certs/cert.pem` (generated out-of-band, not committed); serves files for `WSLoader` testing. Run with npm per `package.json`/`README.md`.

---

*Structure analysis: 2026-09-03*
