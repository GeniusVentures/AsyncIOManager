---
focus: quality
created: 2026-09-03
last_mapped_commit: 009dc3dc04599c61d04aca5ba0cf547703543b31
---
# Testing

**Analysis Date:** 2026-09-03
**Scope:** full repo (`test/`, `test/src/`, `test/testutil/`, `test/websocketserver/`, CMake wiring in root `CMakeLists.txt`, `cmake/functions.cmake`)

## Framework & Setup (GTest + CTest, BUILD_TESTING gate)

**Runner:** GoogleTest + GoogleMock via vcpkg CONFIG package.
- Config: root `CMakeLists.txt:47-51` — `option(BUILD_TESTING "Build tests" OFF)`; when ON: `enable_testing()` + `find_package(GTest CONFIG REQUIRED)`
- Test registration helper: `cmake/functions.cmake:10-33` — the `addtest( <name> <sources...> )` function creates the executable, links `GTest::gtest_main` and `GTest::gmock_main`, registers with CTest, forces JUnit XML output to `${CMAKE_BINARY_DIR}/xunit/xunit-<name>.xml`, and sets binary output to `${CMAKE_BINARY_DIR}/test_bin/`
- Shared test-util interface library: `asiomgr_testutil` (`test/testutil/CMakeLists.txt`) links `AsyncIOManager`, `GTest::gtest`, Boost, spdlog and exposes the `test/testutil/` headers

**Assertion library:** plain GoogleTest macros — `EXPECT_*` for non-fatal, `ASSERT_*` when subsequent lines depend on the check. `ASSERT_TRUE( ok ) << "Timed out..."` with stream messages is the standard timeout idiom (`test/src/mnn_loader_test.cpp:62`).

**Run Commands:**
```powershell
# Configure with tests (file-based tests only)
cmake -S . -B build/Windows -DCMAKE_BUILD_TESTING=ON -DBUILD_TESTING=ON

# Configure including network tests (HTTP/SFTP/IPFS)
cmake -S . -B build/Windows -DBUILD_TESTING=ON -DASYNC_IO_MANAGER_NETWORK_TESTS=ON

cmake --build build/Windows --config Debug

# Run all via CTest
ctest --test-dir build/Windows -C Debug --output-on-failure

# Visual Studio: RUN_TESTS.vcxproj target (build/Windows/Debug/RUN_TESTS.vcxproj)
cmake --build build/Windows --config Debug --target RUN_TESTS

# Single suite / single test
build/Windows/test_bin/Debug/mnn_loader_test.exe --gtest_filter=MNNLoaderTest.LoadASync_ReadsExistingFile
```

**Two-gate structure (`test/src/CMakeLists.txt`):**
- Phase 2 (always built when `BUILD_TESTING=ON`): `mnn_loader_test`, `mnn_saver_test`, `filemanager_test` — pure local filesystem, no network
- Phase 3 (`ASYNC_IO_MANAGER_NETWORK_TESTS=ON`, default OFF): `http_loader_test`, `sftp_saver_test`, `ipfs_loader_test`, `ipfs_saver_test`, `ipfs_device_test`

## Test Structure (fixtures: base_mnn_test.hpp, testutil helpers)

**File organization:** one `<module>_test.cpp` per production unit, in `test/src/`, registered 1:1 via `addtest()`. Shared code lives in `test/testutil/` headers (header-only, linked through `asiomgr_testutil`).

**Primary fixture — `FileManagerTestFixture` (`test/testutil/test_fixture.hpp`):**
- Extends `::testing::Test`; `SetUp()` calls `FileManager::InitializeSingletons()` before every test (idempotent, safe to repeat)
- Provides static factory/unwrap helpers that all tests use for building payloads:
  - `makeSingleFileResult( filename, content )` — single-file `ResultType` from `std::string` or `std::vector<char>`
  - `makeMultiFileResult( filenames, contents )`
  - `unwrapResult( result )` — asserts success and dereferences
- Derive empty subclasses for naming: `class MNNLoaderTest : public FileManagerTestFixture {};` then `TEST_F( MNNLoaderTest, Scenario )`

**`base_mnn_test.hpp` (`test/base_mnn_test.hpp`):** older `BaseMNNTest` fixture (temp dir lifecycle: `mkdir()`/`clear()`, `SetUp`/`TearDown` overrides) — currently declared but **unused** by any test source; prefer `FileManagerTestFixture` + `TempFile`/`TempDir` for new tests.

**Async test recipe (used in every async test — copy this shape):**
```cpp
IOContextRunner runner;                                  // background io_context thread

bool                                   completed = false;
std::optional<FileManager::ResultType> received;

FileManager::GetInstance().LoadASync(
    "file://" + tf.pathString(),
    false, false, runner.ioc(),
    [&]( FileManager::ResultType result )                // FinalCallback
    {
        received  = std::move( result );
        completed = true;
    },
    "" );

bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
ASSERT_TRUE( ok ) << "Timed out waiting for async load";
```
(`test/src/mnn_loader_test.cpp:52-75`)

**Suite-scope fixtures:** expensive peers are built once per suite in `static void SetUpTestSuite()` with `GTEST_SKIP()` on construction failure — see `IPFSIntegrationTest` (`test/src/ipfs_loader_test.cpp:28-97`) and `IPFSDeviceRetryTest` (`test/src/ipfs_device_test.cpp:43-60`).

**Naming convention:** `TEST_F( Fixture, Method_Scenario )` grouped by banner comments — happy paths first, then unhappy paths (`test/src/filemanager_test.cpp`, `test/src/mnn_saver_test.cpp`).

## Test Data & External Deps (.mnn fixtures, websocketserver node service)

**MNN model fixtures:**
- `test/1.mnn`, `test/2.mnn` — real MNN model binaries used by `example/MNNExample.cpp` (`file://../test/1.mnn` path baked into the demo)
- Unit tests avoid binary fixtures where possible: `mnn_loader_test.cpp` / `mnn_saver_test.cpp` use `TempFile( "arbitrary string content" )` because MNNLoader/MNNSaver treat files as opaque byte blobs

**Generated test certs (HTTP):** `test/src/CMakeLists.txt:38-82` finds the `openssl` executable, writes a minimal `openssl.cnf` (CN=127.0.0.1, SAN IP.1/DNS.1), and runs `openssl req -x509 -newkey rsa:2048` at build time into `${CMAKE_CURRENT_BINARY_DIR}/test_certs/{cert,key}.pem`. Cert files are added as sources of `http_loader_test` and the directory is injected via `TEST_CERT_DIR="..."` compile definition. `TestHttpsServer` in `test/src/http_loader_test.cpp:33-57` loads them with `use_certificate_chain_file` / `use_private_key_file`.

**Node.js websocket service (`test/websocketserver/`):**
- `websocket-server.js` — `ws` (v8.15.1, `package.json`) secure WebSocket server on port 8090; verifies/sanitizes resource paths in `verifyClient`, tracks active sockets, reads TLS key/cert from `./certs/{key,cert}.pem` (certs NOT committed — must be generated manually)
- Run manually when developing `WSLoader` / `WSCommon`:
  ```powershell
  cd test/websocketserver
  npm install
  node websocket-server.js
  ```
- **Not wired into CMake/CTest** — there is no `ws_loader_test.cpp`; the server is a manual development aid (see Coverage Gaps)

**SFTP external dep:** `sftp_saver_test.cpp` self-provisions a local OpenSSH server at runtime — checks for `sshd` via `where sshd` / `C:\Windows\System32\OpenSSH\sshd.exe`, generates host + client keys with `ssh-keygen -t rsa -m PEM`, writes an `sshd_config` (publickey-only, `internal-sftp` chrooted to a temp data dir), picks port `2222 + rand() % 10000`, and **skips the suite via `GTEST_SKIP() << "sshd not available"`** if any step fails (`test/src/sftp_saver_test.cpp:344-348`). No SFTPLoader round-trip test exists — only SFTPSaver.

## Mocking/Fakes (bitswap_node, local servers)

**No gmock mocks are in use** (gmock_main is linked by `addtest` but unused). Fakes are real in-process implementations:

**`BitswapNode` (`test/testutil/bitswap_node.hpp`) — full libp2p peer fixture:**
- Wraps a complete libp2p host (Boost.DI injector, Noise security, Ed25519 keys, kademlia DHT) + bitswap instance — mirrors `ipfs-bitswap-cpp/test/bitswap_server_client_test.cpp`
- One-time soralog YAML config via `std::call_once` (console sink, libp2p at debug)
- API used by tests: `getBitswap()`, `getHost()`, `getPeerInfo()`, `startDHT( bootstrapPeers )`
- Usage pattern: two nodes (server + client); manual peer discovery by upserting server addresses into the client's address repository, then client-only DHT bootstrap (both-ways bootstrap deadlocks — documented in `test/src/ipfs_loader_test.cpp:60-68`), plus a connection warm-up `newStream` before the real assertions

**`TestHttpsServer` (`test/src/http_loader_test.cpp:33-141`) — embedded Boost.Beast HTTPS server:**
- Threaded accept loop with self-signed SSL, serves `/test/data.bin` as `application/octet-stream`, 404 otherwise
- Tests disable cert verification around the server's lifetime via the production test hook `sgns::HTTPDevice::SetVerifyPeer( false )` in `SetUp`, restored to `true` in `TearDown` (`test/src/http_loader_test.cpp:154-170`)

**`SftpTestServer` (`test/src/sftp_saver_test.cpp:42-150`) — OpenSSH subprocess wrapper** (see above).

**`IOContextRunner` + `pollUntil` (`test/testutil/asio_helpers.hpp`) — async harness:**
- `IOContextRunner` — RAII background thread running an `io_context` with work guard; `stop()`, `restart()`; non-copyable
- `pollUntil( pred, timeout = 30s, interval = 50ms )` — polling latch; always pair with `ASSERT_TRUE( ok ) << "..."`
- Callbacks must capture through shared state (`std::make_shared<bool>`), never raw refs, since they fire on the runner thread

**`TempFile` / `TempDir` (`test/testutil/temp_file.hpp`) — RAII filesystem fixtures:**
- Unique names from steady-clock suffix (`asiomgr_test_*`), self-cleaning destructors, `TempDir::writeFile()` creates nested paths
- Prefer these over hardcoded `/tmp` paths

## Running Tests (ctest / RUN_TESTS, build configs)

**CTest is the entry point.** `addtest()` registers each binary with `--gtest_output=xml` so CTest/every CI reader gets JUnit XML at `build*/xunit/xunit-<name>.xml`.

**Windows multi-config:** binaries land in `build/Windows/test_bin/<Config>/`; always pass `-C Debug` (or `Release`) to `ctest` and `--config` to `cmake --build`. VS solution target: `build/Windows/Debug/RUN_TESTS.vcxproj` (RUN_TESTS project in `AsyncIOManager.sln`). Note `build/Windows/Debug/hang.dmp` exists — evidence of past debug runs of a hung test.

**Expected suites at a glance:**

| CTest name | Gate | Covers |
|---|---|---|
| `filemanager_test` | `BUILD_TESTING` | Registry dispatch, op-counter lifecycle, sync + async file paths |
| `mnn_loader_test` | `BUILD_TESTING` | `file://` load, sync + async, error paths |
| `mnn_saver_test` | `BUILD_TESTING` | `file://`/`mnn://` save, single + multi-file with subdirs, `save_location` |
| `http_loader_test` | `+ASYNC_IO_MANAGER_NETWORK_TESTS` | HTTPS download via embedded Beast server, self-signed certs |
| `sftp_saver_test` | `+ASYNC_IO_MANAGER_NETWORK_TESTS` | SFTP upload against self-provisioned sshd (skips if absent) |
| `ipfs_loader_test` | `+ASYNC_IO_MANAGER_NETWORK_TESTS` | Loader + saver happy path over two BitswapNodes |
| `ipfs_saver_test` | `+ASYNC_IO_MANAGER_NETWORK_TESTS` | Saver edge cases, `setBitswap`/`clearBitswap` ownership |
| `ipfs_device_test` | `+ASYNC_IO_MANAGER_NETWORK_TESTS` | Use-after-free lifetime regressions in `IPFSDevice` retry timers |

**Flakiness mitigation baked into tests:** generous `pollUntil` timeouts (5s local FS, 20-35s IPFS/DHT to absorb Debug-build slowness on Windows), `GTEST_SKIP()` on missing external deps, and deterministic suite teardown that waits out armed timers (`test/src/ipfs_device_test.cpp:92-100`).

## Coverage Gaps (which modules lack tests)

**No test file at all:**
- **WSLoader / WSCommon** — zero tests. `test/websocketserver/websocket-server.js` exists as a manual aid but no `ws_loader_test.cpp` is registered in `test/src/CMakeLists.txt`. Risk: the WebSocket state machine (`src/WSCommon.cpp`, ~180 lines of nested async continuations) is completely unverified
- **SFTPLoader** — only the *saver* side is tested. `SFTPLoader::LoadASync`/`LoadFile` (download direction, `src/SFTPLoader.cpp`) has no test; `LoadFile` currently returns a dummy string
- **URLStringUtil** — pure, easily testable functions (`getURLComponents`, `parseHTTPUrl`, `parseSFTPUrl`, `parseIPFSUrl` in `src/URLStringUtil.cpp`) with no direct unit tests; they're only exercised incidentally through FileManager tests

**Partially covered:**
- **HTTPLoader::LoadFile** — sync path returns a hardcoded dummy value (`src/HTTPLoader.cpp:36-41`); only `LoadASync` is tested. Error-path tests (bad URL, resolve failure, timeout) beyond `INVALID_URL` are thin
- **FileParser** — `RegisterParser` exists but no parser implementation or test (MNNParser commented out in `src/FileManager.cpp:35`); the parse branch inside `LoadASync`'s `handle_read` lambda is dead code
- **FileManager::ParseData** — untested (no registered parser)
- **`base_mnn_test.hpp`** — dead fixture, no test uses it

**Structural gaps:**
- **No coverage measurement** — no gcov/lcov/OpenCppCoverage integration; xUnit XML exists but no coverage target
- **No CI pipeline** in-repo; tests run only on developer machines
- **Single-config bias** — tests developed/verified on Windows Debug; Linux/macOS paths (`cmake/toolchain`, `build/Linux`, `build/OSX`) have no test runs recorded
- **`GTEST_SKIP()` silently degrades network suites** — on machines without sshd, `sftp_saver_test` reports skipped (green), which can hide regressions if not watched
- **GoogleMock linked but unused** — no interface-level mocks; every network test requires real infrastructure (embedded server or libp2p node), keeping test wall-time high

---

*Testing analysis: 2026-09-03*
