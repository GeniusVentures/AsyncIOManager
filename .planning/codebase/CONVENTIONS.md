---
focus: quality
created: 2026-09-03
last_mapped_commit: 009dc3dc04599c61d04aca5ba0cf547703543b31
---
# Conventions

**Analysis Date:** 2026-09-03
**Scope:** full repo (`include/`, `src/`, `test/`, `cmake/`, root `CMakeLists.txt`)

## Code Style (formatting, headers guards vs #pragma once, includes order)

**Header guards:** Use `#pragma once` — NOT include guards. All 19 project headers in `include/` start with `#pragma once` (verified: `include/FileManager.hpp:1`, `include/FileLoader.hpp:3`, etc.). No `#ifndef` guards exist in project-authored headers. (`include/httplib.h` is vendored third-party — do not edit.)

**Formatting (no .clang-format / .editorconfig exists — style is convention-only):**
- Indent: 4 spaces
- Brace style: Allman (opening brace on its own line) for classes, functions, namespaces, and control flow
- Parentheses are padded with spaces on the inside: `LoadASync( "file://" + tf.pathString(), false, false, runner.ioc(), ... )` (`src/FileManager.cpp:52-58`)
- `namespace sgns { ... } // End namespace sgns` with closing comment (`src/HTTPLoader.cpp:75`)
- Pointer/reference alignment: right side (`std::string &prefix`, `const std::string &url`)
- Line length: soft ~120 cols; long parameter lists wrapped with vertical alignment (see `include/FileManager.hpp:113-118`)

**File banners:** Headers open with a comment banner naming the file, e.g. `/** Header file for the MNNLoader */` (`include/MNNLoader.hpp:1-4`), `// FileLoaders.hpp` (`include/FileLoader.hpp:1`). Sources follow with banner + grouped includes (`src/MNNSaver.cpp:1-15`).

**Include order (observed in `src/*.cpp`):**
1. Own project headers first: corresponding header + `FileManager.hpp` + sibling protocol headers (`#include "FileManager.hpp"`, `#include "URLStringUtil.h"`, `#include "MNNLoader.hpp"` — `src/FileManager.cpp:1-12`)
2. C++ standard library (`<sstream>`, `<filesystem>`, `<fstream>`, `<memory>`, ...)
3. Third-party (`boost/...`, `libssh2.h`, `bitswap.hpp`)

In headers, project headers come first, then std, then boost/third-party (`include/MNNLoader.hpp:6-10`).

**Namespace structure:**
- Protocol loaders/savers/devices: `namespace sgns { ... }` (e.g. `HTTPLoader`, `IPFSDevice`, `MNNSaver`)
- Base interfaces `FileLoader` (`include/FileLoader.hpp`), `FileSaver` (`include/FileSaver.hpp`), `FileParser` (`include/FileParser.hpp`) are in the **global namespace**
- `FileManager` is also in the global namespace (`include/FileManager.hpp:39`)
- Logging: `namespace sgns::asiomgr` (`include/asiomgr-logger.hpp:12`)
- Every header using outcome re-exports it:
  ```cpp
  namespace outcome
  {
      using libp2p::outcome::failure;
      using libp2p::outcome::result;
      using libp2p::outcome::success;
  }
  ```
  (appears in `include/FileManager.hpp:27-32`, `include/FileLoader.hpp:10-15`, `include/IPFSCommon.hpp:36-42`)

**Platform ifdefs:** Windows/POSIX splits guarded by `#ifdef _WIN32` / `#ifndef _WIN32` with alternative implementations in the same file — see `src/FILECommon.cpp:11-67`. Windows-specific socket headers (`winsock2.h`, `ws2tcpip.h`) vs POSIX (`sys/socket.h`, `unistd.h`) in `test/src/sftp_saver_test.cpp:20-32`.

## Naming (files, classes, functions, enums)

**Files:**
- Protocol code follows a strict **triad**: `<PROTO>Loader`, `<PROTO>Saver`, `<PROTO>Common` — e.g. `HTTPLoader.hpp/.cpp`, `SFTPSaver.hpp/.cpp`, `IPFSCommon.hpp/.cpp`, `WSLoader.hpp/.cpp`
- Mixed extensions: `.hpp` for most headers; `.h` for `URLStringUtil.h` (utility) and vendored `httplib.h`
- Header/source pairs share the exact base name (`MNNLoader.hpp` ↔ `MNNLoader.cpp`)
- Logger files use kebab-case: `asiomgr-logger.hpp/.cpp`

**Classes:**
- `PascalCase`: `FileManager`, `HTTPLoader`, `IPFSDevice`, `SFTPUploadDevice`, `FILEDevice`, `WSDevice`, `HTTPDevice`
- Device classes (async protocol engines) end in `Device`; registry-dispatched handlers end in `Loader`/`Saver`/`Parser`
- Test classes end in `Test`/`TestFixture`/`IntegrationTest` (`FileManagerIntegrationTest`, `MNNLoaderTest`, `IPFSSaverEdgeTest`)

**Functions:**
- `PascalCase` for public API: `LoadFile`, `LoadASync`, `SaveASync`, `RegisterLoader`, `InitializeSingleton`, `StartHTTPDownload`, `StartSFTPDownload`, `StartFindingPeersWithRetry`
- `camelCase` for internal/lower-level helpers: `getURLComponents`, `parseHTTPUrl`, `parseSFTPUrl`, `parseIPFSUrl` (`include/URLStringUtil.h`), `setBitswap`, `clearBitswap`, `getCacheDir`
- Free URL parsers are declared `extern bool ...` in `include/URLStringUtil.h:11-14` (legacy C-style, not in a namespace)

**Member variables (two coexisting conventions):**
- `m_` prefix for loggers: `sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "FileManager" );` (every class owns one — `include/FileManager.hpp:43`, `include/MNNLoader.hpp:63`, `include/SFTPSaver.hpp:37`)
- trailing underscore for state: `http_host_`, `http_path_`, `parse_`, `save_`, `cacheDir_`, `bitswapMutex_`, `outstandingOperations_`
- One exception: `outstandingOperations_` is trailing-underscore while older members on the same class are not — prefer trailing underscore for new members

**Enums:**
- `enum class Error` nested in the owning class, values `SCREAMING_SNAKE_CASE`, starting at 1:
  ```cpp
  enum class Error
  {
      READ_ERROR     = 1,
      FILE_OPEN_FAIL = 2,
  };
  ```
  (`include/MNNLoader.hpp:24-28`, `include/HTTPLoader.hpp:23`, `include/IPFSCommon.hpp:54-58`)

**Test macros:** `TEST_F( FixtureName, TestName_Scenario )` with `PascalCase_UnderscoreScenario`, grouped by banner comments `// ----- Happy path -----` (`test/src/mnn_loader_test.cpp`).

## Patterns (singleton usage, shared_ptr, async callbacks, per-protocol triads)

**Per-protocol triad (the core architecture pattern):**
For each protocol `P`:
- `include/PLoader.hpp` + `src/PLoader.cpp` — thin singleton implementing `FileLoader`; parses URL, constructs a `PDevice`, delegates
- `include/PSaver.hpp` + `src/PSaver.cpp` — singleton implementing `FileSaver`
- `include/PCommon.hpp` + `src/PCommon.cpp` — the `PDevice` class holding the boost::asio/SSL/SSH state machine
- Example: `src/HTTPLoader.cpp:56-71` (`LoadASync` → `parseHTTPUrl` → `std::make_shared<HTTPDevice>` → `StartHTTPDownload`)

**Singleton usage (mandatory for loaders/savers):**
- Use `SINGLETON_PTR( ClassName )` macro from `include/ASIOSingleton.hpp` (raw-pointer variant) inside the class body; `FileManager` uses `SINGLETON_REF` (reference variant)
- Define `static void InitializeSingleton()` in the .cpp guarded by `if ( _instance == nullptr ) { _instance = new ClassName(); }` (`src/HTTPLoader.cpp:20-25`)
- The **constructor self-registers** with the FileManager registry under a URL prefix:
  ```cpp
  HTTPLoader::HTTPLoader()
  {
      FileManager::GetInstance().RegisterLoader( "https", this );
  }
  ```
  (`src/HTTPLoader.cpp:27-31`; also `"file"` in `src/MNNLoader.cpp:34`, `"sftp"`, `"wss"`, `"ipfs"`)
- Registration must not double-register: `InitializeSingleton()` is idempotent; callers invoke `FileManager::InitializeSingletons()` once (`src/FileManager.cpp:33-42`)
- Savers may register multiple prefixes: `MNNSaver` registers both `"file"` and `"mnn"` (`src/MNNSaver.cpp:37-41`)

**shared_ptr conventions:**
- Async operations pass `std::shared_ptr<boost::asio::io_context> ioc` so the context can be stopped when work drains (`include/FileManager.hpp:113`)
- Payloads cross API boundaries as type-erased `std::shared_ptr<void>` (`FileLoader::LoadFile` return); callers `std::static_pointer_cast<std::string>` back
- Async device classes MUST inherit `std::enable_shared_from_this` and capture `self = shared_from_this()` in every continuation lambda so pending handlers keep the device alive (`src/HTTPCommon.cpp:148`, `include/IPFSCommon.hpp:61`, `src/SFTPSaver.cpp:27`). Regression tests enforce this (`test/src/ipfs_device_test.cpp`)
- Prefer capturing shared state as `std::make_shared<T>` rather than by-reference lambda captures when the callback outlives the scope (`test/src/ipfs_device_test.cpp:86-87`)

**Async callback pattern:**
- Two callback tiers, both declared as `using` aliases on the class:
  - `CompletionCallback` — `void( shared_ptr<io_context> ioc, ResultType buffers, bool parse, bool save )` (device → FileManager)
  - `FinalCallback` — `void( ResultType buffers )` (FileManager → application) (`include/FileManager.hpp:68-77`)
- `ResultType` is always `outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>` (paths + byte buffers), re-declared per class (`include/FileLoader.hpp:21-22`, `include/FileSaver.hpp:12-13`)
- Errors are delivered by posting a failure onto the io_context, never by throwing across the async boundary:
  ```cpp
  boost::asio::post( *ioc, [handle_read, ioc]()
      { handle_read( ioc, outcome::failure( Error::INVALID_URL ), false, false ); } );
  ```
  (`src/HTTPLoader.cpp:57-60`, `src/MNNLoader.cpp:62-66`)
- Operation counting: `IncrementOutstandingOperations()` before starting, `DecrementOutstandingOperations( ioc )` in every completion path; hitting zero calls `ioc->stop()` (`src/FileManager.cpp:212-233`). `SaveASync` wraps the saver call in `try/catch(...)` so the counter is decremented before rethrowing (`src/FileManager.cpp:155-163`)

**URL parsing:** All URL decomposition goes through `getURLComponents` / `parseHTTPUrl` / `parseSFTPUrl` / `parseIPFSUrl` in `src/URLStringUtil.cpp` (declared in `include/URLStringUtil.h`). Prefix `"file"`, `"https"`, `"sftp"`, `"wss"`, `"ipfs"` selects the handler.

**Registry dispatch:** `FileManager` keeps `map<std::string, FileLoader *> loaders` etc.; lookups that miss throw `std::range_error`; hits are double-checked with `assert( dynamic_cast<FileLoader *>( loader ) )` before use (`src/FileManager.cpp:132-136`).

## Error Handling (exceptions vs result codes, logging levels)

**Two-tier strategy:**
1. **Synchronous paths throw** `std::range_error` with a human-readable message: unregistered prefix (`src/FileManager.cpp:62`), missing file (`src/MNNLoader.cpp:42`), null save data (`src/MNNSaver.cpp:48`). Tests assert these with `EXPECT_THROW( ..., std::range_error )`
2. **Asynchronous paths return** `outcome::failure( Error::X )` through the completion callback. Error categories are defined at file scope with:
   ```cpp
   OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, MNNLoader::Error, e )
   {
       switch ( e )
       {
           case sgns::MNNLoader::Error::READ_ERROR: return "File could not be read";
           ...
       }
       return "Unknown error";
   }
   ```
   (`src/MNNLoader.cpp:10-20`, `src/HTTPCommon.cpp:57-78`, `src/WSLoader.cpp:11-20`)

**Sync→async bridge:** `FileManager::SaveASync` catches all exceptions from savers, decrements the outstanding-ops counter, and rethrows (`src/FileManager.cpp:155-163`).

**Logging (`sgns::asiomgr::Logger` via spdlog, `include/asiomgr-logger.hpp`):**
- Create a per-class logger as a member: `sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "ClassName" );` — loggers are cached per tag (`src/asiomgr-logger.cpp:55-65`)
- Message pattern set globally: `[%Y-%m-%d %H:%M:%S][%l][%n] %v` (`src/asiomgr-logger.cpp:13-16`)
- Use **fmt-style `{}` placeholders, never string concatenation with runtime values** — NOTE: `src/FILECommon.cpp:25` still concatenates; that is the outlier, not the pattern
- Level discipline (observed across `src/`):
  - `debug` — URL decomposition, routine file-open traces (`src/FileManager.cpp:55`)
  - `info` — lifecycle milestones: resolved address, published CID, bitswap set (`src/HTTPCommon.cpp:91`, `src/IPFSSaver.cpp:171`)
  - `warn` — recoverable misconfiguration: cast failed, handler not registered (`src/FileManager.cpp:262,267`)
  - `error` — operation failures delivered via outcome/exception: resolve failure, read error, publish failure (`src/HTTPCommon.cpp:97`, `src/MNNLoader.cpp:98`, `src/IPFSSaver.cpp:68`)
- Log from within async continuations via the captured `self->m_logger` (`src/HTTPCommon.cpp:167`)

**Dual-checks:** `assert( dynamic_cast<...> )` guards registry casts in debug builds (`src/FileManager.cpp:136`); timeouts implemented with `ArmDeadline`/`CancelDeadline` steady-timer helpers that cancel the socket (`src/HTTPCommon.cpp:12-49`).

## CMake Conventions (target naming, find_package usage, install/export)

**Target naming:** library target is `AsyncIOManager` (matches project name); test execututables are named `<module>_test` (`mnn_loader_test`, `ipfs_saver_test`); test util interface library is `asiomgr_testutil` (`test/testutil/CMakeLists.txt`).

**find_package style:** `find_package(<Dep> CONFIG REQUIRED)` for everything vcpkg-style: `Protobuf`, `MNN`, `fmt`, `spdlog`, `RocksDB`, `soralog`, `yaml-cpp`, `tsl_hat_trie`, `Boost.DI`, `libp2p`, `ipfs-lite-cpp`, `ipfs-bitswap-cpp`, `libssh2`, `GTest`. Module-style with components only for Boost: `find_package(Boost REQUIRED COMPONENTS date_time filesystem ...)` (root `CMakeLists.txt:10-45`).

**Link targets (not raw vars):** link `spdlog::spdlog`, `OpenSSL::SSL`, `libssh2::libssh2`, `GTest::gtest_main`; `${Boost_LIBRARIES}` still used in older blocks — new code should prefer `Boost::<component>` imported targets. PUBLIC vs PRIVATE split in `src/CMakeLists.txt:21-46`: libp2p/ipfs-lite linkages are PUBLIC (headers expose them), OpenSSL/bitswap internals PRIVATE.

**Standard/toolchain:** C++17 enforced (`CMAKE_CXX_STANDARD 17`, `STANDARD_REQUIRED ON`, `EXTENSIONS OFF`); default toolchain `cmake/toolchain/cxx17.cmake`; global defines `-D_WIN32_WINNT=0x0601` and `-DBOOST_BIND_GLOBAL_PLACEHOLDERS` (root `CMakeLists.txt:3-15`).

**Test gating:**
- `option(BUILD_TESTING "Build tests" OFF)` — tests are **OFF by default**; when ON, `enable_testing()` + `find_package(GTest CONFIG REQUIRED)` (root `CMakeLists.txt:47-51`)
- `option(ASYNC_IO_MANAGER_NETWORK_TESTS ... OFF)` — second gate for HTTP/SFTP/IPFS tests (`test/src/CMakeLists.txt:34`)
- `addtest(<name> <sources...>)` function in `cmake/functions.cmake:10-33` is the ONLY way to register a test: creates the executable, links `GTest::gtest_main` + `GTest::gmock_main`, emits `--gtest_output=xml:${CMAKE_BINARY_DIR}/xunit/xunit-<name>.xml`, calls `add_test`, places binaries in `${CMAKE_BINARY_DIR}/test_bin`, and disables clang-tidy on tests
- Compile definitions for paths use quoted defines: `target_compile_definitions(http_loader_test PRIVATE TEST_CERT_DIR="${TEST_CERT_DIR}")` (`test/src/CMakeLists.txt:83-85`)

**Warnings management:** toolchain-conformance noise silenced narrowly per-target, e.g. `-Wno-missing-template-arg-list-after-template-kw` only for clang on `ipfs_device_test` (`test/src/CMakeLists.txt:199-203`).

**Install/export:** headers installed from `include/` matching `*.h*`; library target exported as `AsyncIOManagerTargets` to `lib/cmake/AsyncIOManager`; config/version generated from `cmake/config.cmake.in` via `configure_package_config_file` + `write_basic_package_version_file` (version `0.1`, `AnyNewerVersion`) (root `CMakeLists.txt:78-113`).

**Include directories:** prefer `${CMAKE_SOURCE_DIR}/include` at top level; legacy `include_directories(../include)` appears in `src/`, `test/`, `example/` CMakeLists — keep for consistency until modernized to `target_include_directories`.

**Anti-patterns to avoid (observed but not to be copied):**
- `using namespace std;` at header scope (`include/FileSaver.hpp:6`) — leaks into every includer
- Unqualified `string`/`map` in headers relying on that using-directive (`include/FileManager.hpp:47-52`)
- Commented-out registration code left in `InitializeSingletons` (`src/FileManager.cpp:33-42`) and `std::system`-based server bootstrap in tests (`test/src/sftp_saver_test.cpp`) — acceptable in test code only
- `message(WARNING ...)` debug spew in `test/src/CMakeLists.txt:13-14` — do not add more

---

*Convention analysis: 2026-09-03*
