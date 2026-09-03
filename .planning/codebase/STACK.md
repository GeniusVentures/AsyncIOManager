---
focus: tech
created: 2026-09-03
last_mapped_commit: 009dc3dc04599c61d04aca5ba0cf547703543b31
---
# Technology Stack

## Languages & Runtime

**Primary:** C++17 (forced via `cmake/toolchain/cxx17.cmake`; `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, no extensions — set in `CMakeLists.txt`, `build/CommonCompilerOptions.CMake`, `cmake/toolchain/cxx17.cmake`)

**Secondary:** 
- CMake scripting language (>= 3.20 required at root `CMakeLists.txt`; `build/Windows/CMakeLists.txt` still declares legacy `cmake_minimum_required(VERSION 3.5.1)`)
- JavaScript (Node.js) — test helper only: `test/websocketserver/websocket-server.js`
- Protobuf IDL — compiled to C++ via `compile_proto_to_cpp()` in `cmake/functions.cmake`

**Runtime model:** Asynchronous I/O built on Boost.Asio `io_context` (shared pointers passed through all loader APIs — see `include/FileLoader.hpp`). Windows targets define `_WIN32_WINNT=0x0601` (Win7+) and `BOOST_BIND_GLOBAL_PLACEHOLDERS` in `CMakeLists.txt` and `build/Windows/CMakeLists.txt`.

**Package manager:** None for C++ (all deps prebuilt under `_THIRDPARTY_BUILD_DIR` and located via `find_package(... CONFIG)`). npm used only for the test WS server (`test/websocketserver/package.json`, single dependency `ws ^8.15.1`, no lockfile committed).

## Frameworks & Core Libraries

**Networking / async core:**
- Boost.Asio (sockets, `streambuf`, `async_read`, `post`) — used in every loader; see `include/FileLoader.hpp`, `src/MNNLoader.cpp`
- Boost.Beast WebSocket — `src/WSCommon.cpp` (`boost::beast::websocket::stream<boost::asio::ssl::stream<tcp::socket>>`)
- Boost.Asio SSL (OpenSSL backend) — HTTPS in `src/HTTPCommon.cpp` (`SslSocket = ssl::stream<tcp::socket>`), WSS in `src/WSCommon.cpp`
- libp2p — host/kademlia injectors, identify, ping, gossip, multiaddress, c-ares resolver; linked as `p2p::*` targets in `src/CMakeLists.txt`; outcome type (`libp2p::outcome::result`) is the universal error type in public APIs
- ipfs-lite-cpp — graphsync, cbor, ipld_node, blockservice, in-memory + RocksDB datastores, kad DHT (`src/CMakeLists.txt` lines 30–40)
- ipfs-bitswap-cpp — bitswap engine + `ipfs-bitswap-proto` / `ipfs-unixfs-proto` (linked PRIVATE), see `include/IPFSCommon.hpp`, `src/IPFSLoader.cpp`
- libssh2 — SFTP transport (`libssh2_session_*`, `libssh2_sftp_*`, `LIBSSH2_ERROR_EAGAIN` non-blocking loop pattern in `src/SFTPCommon.cpp`, `src/SFTPLoader.cpp`, `src/SFTPSaver.cpp`)
- cpp-httplib 0.14.2 — **vendored, header-only** at `include/httplib.h` (MIT, Yuji Hirose). NOTE: currently NOT `#include`d by any `.cpp`; live HTTP(S) code uses Boost.Asio/Beast directly (`src/HTTPCommon.cpp`). Treat as available-but-dormant.

**AI/ML:**
- MNN (Alibaba) — `MNN/Interpreter.hpp`, `MNN/Tensor.hpp` via `include/MNNCommon.hpp`; model files `test/1.mnn`, `test/2.mnn`; example inference flow in `example/MNNExample.cpp`

**Serialization / config:**
- Protobuf (CONFIG, `protobuf::libprotobuf`, `protobuf::protoc` imported executable shim in root `CMakeLists.txt`)
- yaml-cpp — soralog logging config (embedded YAML string in `src/IPFSLoader.cpp`)
- RapidJSON, jsonrpc-lean, libsecp256k1, ed25519, xxhash, Snappy, zlib, SQLiteModernCpp, Microsoft.GSL, tsl_hat_trie, c-ares — located in `build/CommonBuildParameters.cmake` (super-build environment; only some are consumed by this repo's own targets)

**Logging:**
- spdlog (external fmt via `SPDLOG_FMT_EXTERNAL`) — project logger factory `sgns::asiomgr::createLogger(tag, basepath)` in `include/asiomgr-logger.hpp` / `src/asiomgr-logger.cpp` (stdout_color_mt / basic_file_mt / android_sink)
- soralog — wraps libp2p logging with YAML configurator; initialized in `src/IPFSLoader.cpp` `LoadASync()` and `example/MNNExample.cpp`
- fmt — standalone dependency, also spdlog backend

**Testing:**
- GTest + GMock (`GTest::gtest_main`, `GTest::gmock_main`) via `addtest()` helper in `cmake/functions.cmake`; xunit XML output to `${CMAKE_BINARY_DIR}/xunit/`; binaries land in `${CMAKE_BINARY_DIR}/test_bin`

**DI:**
- Boost.DI (`boost/di/extension/scopes/shared.hpp`) — libp2p injector composition in `example/MNNExample.cpp` and `test/testutil/bitswap_node.hpp`

## Build System

Two build modes exist:

1. **Standalone (root `CMakeLists.txt`):** `cmake -S . -B build [-DBUILD_TESTING=ON]`. Produces static lib `AsyncIOManager` from `src/CMakeLists.txt`, installs headers + CMake package (`AsyncIOManagerConfig.cmake`, version `0.1`) via `cmake/config.cmake.in`. Sub-projects: `src/` (always), `test/` (when `BUILD_TESTING=ON`), `example/` (referenced from `build/CommonBuildParameters.cmake` as `BUILD_EXAMPLES`).
2. **Super-build (`build/CommonBuildParameters.cmake` + `build/{Linux,OSX,Windows}/CMakeLists.txt`):** all third-party deps pinned to `${_THIRDPARTY_BUILD_DIR}`; per-platform flags in `build/Windows/CMakeLists.txt` (MSVC `/MT`+`/MTd` static runtime, `wsock32 ws2_32 crypt32 userenv` system libs), `build/CommonCompilerOptions.CMake` (CPACK 21.0.0-12, "Genius Ventures", PIC ON, `CMAKE_EXPORT_COMPILE_COMMANDS ON`), `build/CompilationFlags.cmake`, `cmake/compile_option_by_platform/Windows.cmake`.

**Key options:**
- `BUILD_TESTING` (default OFF) — enables GTest + `test/`
- `ASYNC_IO_MANAGER_NETWORK_TESTS` (`test/src/CMakeLists.txt`, default OFF) — HTTP/SFTP/IPFS network tests; generates self-signed cert via `openssl` executable into `${CMAKE_CURRENT_BINARY_DIR}/test_certs`
- `Boost_USE_STATIC_RUNTIME`, `SGNS_STACKTRACE_BACKTRACE` (`build/CommonBuildParameters.cmake`)

**Debug artifacts exist** in `build/Windows/Debug/` (Visual Studio solution `AsyncIOManager.sln`, incl. `hang.dmp`) — MSVC toolchain on Windows.

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

---

*Stack analysis: 2026-09-03*
