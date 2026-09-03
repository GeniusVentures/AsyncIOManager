---
focus: concerns
created: 2026-09-03
last_mapped_commit: 009dc3dc04599c61d04aca5ba0cf547703543b31
---
# Concerns & Technical Debt

## Critical (security, data loss, deadlocks)

- **TLS certificate verification disabled (HTTPS loader)**
  - Severity: Critical
  - Files: `src/HTTPCommon.cpp` (line ~130: `ssl_context->set_verify_mode( boost::asio::ssl::verify_none );`)
  - Evidence: The async HTTPS load path sets `verify_none`, so any server with any certificate (self-signed, mismatched hostname, attacker-in-the-middle) is accepted. This is MITM-able by design.
  - Fix: Default to `verify_peer` + hostname verification (`boost::asio::ssl::context::verify_peer`, `SSL_set_tlsext_host_name`), and only allow `verify_none` behind an explicit opt-in flag.

- **SFTP credentials embedded in URL strings and never sanitized**
  - Severity: Critical
  - Files: `src/URLStringUtil.cpp` (`parseSFTPUrl`, lines ~86–175), `include/SFTPCommon.hpp` (constructor takes `sftp_pass`, `sftp_privkeypass` as `std::string`), `src/SFTPCommon.cpp` (members `sftp_pass_`, `sftp_privkeypass_` held in plaintext), `src/SFTPSaver.cpp` (lines ~189–205, ~550–566 pass `sftp_pass_.c_str()` to libssh2)
  - Evidence: Passwords, private-key paths, and key passphrases are parsed out of the URL (e.g. `sftp://user:pass@host/...`, `...:privkey_identifier:<path>key_passphrase...`) and stored as long-lived `std::string` members on singleton-scoped objects. They will appear in logs (`m_logger->debug("URL: {} ...")` in `src/FileManager.cpp` logs the full URL), core dumps, and swap.
  - Fix: Accept credentials via a dedicated credentials struct/callback instead of URL; redact URLs before logging; wipe buffers on destruction.

- **No SSH host-key verification (libssh2)**
  - Severity: Critical
  - Files: `src/SFTPCommon.cpp` (`StartSFTPHandshake` proceeds straight from `libssh2_session_handshake` to `StartSFTPAuth` — no `libssh2_knownhost_*` calls anywhere), `src/SFTPSaver.cpp` (same pattern duplicated twice)
  - Evidence: Grep for `knownhost|HOSTKEY` finds matches only in the test fixture (`test/src/sftp_saver_test.cpp` writes an sshd config), never in library code. The client authenticates the server never — trivially MITM-able despite SSH.
  - Fix: Add `libssh2_knownhost_readfile` against a known_hosts path and `libssh2_session_hostkey` check before userauth; fail closed on mismatch.

- **Unregistered-parser iterator dereference on `parse=true` (UB / likely crash)**
  - Severity: Critical
  - Files: `src/FileManager.cpp` (`LoadASync` handler, lines ~76–81: `auto parserIter = parsers.find( "mnn" ); auto parser = dynamic_cast<FileParser *>( parserIter->second );`)
  - Evidence: `parsers.find()` result is used without comparing to `parsers.end()`. Meanwhile `InitializeSingletons()` (`src/FileManager.cpp` line ~33) has `//sgns::MNNParser::InitializeSingleton();` commented out, so the parsers map is empty and `parserIter->second` dereferences the end iterator whenever a caller passes `parse=true`.
  - Fix: Guard with `if (parserIter == parsers.end()) { /* error path */ }`; also implement or remove the commented-out parse branch (the `ParseASync` call itself is commented out).

## High (bugs, fragile patterns, singleton/threading risks)

- **Handler registration table partially disabled**
  - Severity: High
  - Files: `src/FileManager.cpp` lines ~32–40 (`InitializeSingletons`), `src/SFTPLoader.cpp` line ~17, `src/WSLoader.cpp` line ~25
  - Evidence: `SFTPLoader::InitializeSingleton()` and `WSLoader::InitializeSingleton()` are commented out (also `MNNParser`). Any `sftp://` load via `FileManager::LoadASync` throws `std::range_error("No loader registered for prefix sftp")` even though a fully implemented loader exists in-tree. `HTTPLoader` registers only `"https"` — the `"http"` registration is commented out (`src/HTTPLoader.cpp` line ~26).
  - Fix: Enable registration (fix the underlying reason it was disabled), or make registration table explicit/config-driven so "implemented but unregistered" cannot happen silently.

- **Blocking `resolver.resolve()` on the io_context thread inside async chains**
  - Severity: High
  - Files: `src/SFTPCommon.cpp` (`StartSFTPDownload`, ~line 36: `resolver.resolve( sftp_host_, "22" )`), `src/SFTPSaver.cpp` (`StartSFTPUpload`, ~lines 84–86), `src/WSCommon.cpp` (~line 41)
  - Evidence: Synchronous DNS resolution executed directly in "async" entry points blocks the io_context thread (and with a shared `io_context`, stalls every other pending operation). This is the classic recipe for the kind of hang investigated in `build/Windows/Debug/hang.dmp` (present on disk, git-ignored).
  - Fix: Use `boost::asio::async_resolve` and continue the chain from its completion handler.

- **Continue-after-failure in SFTP connect path (UB on empty resolve results)**
  - Severity: High
  - Files: `src/SFTPCommon.cpp` (`StartSFTPDownload`, ~lines 39–73)
  - Evidence: When `resolver.resolve()` throws, the catch blocks report `status(...failure...)` but do **not** return; execution falls through to `async_connect(*tcpSocket, resolvedaddr, ...)` with default-constructed (empty) `results_type`. Downstream error paths call `status()` and `handle_read` while `downloading_` stays `true` forever, wedging the device.
  - Fix: `return` after each failure branch; reset `downloading_`/`uploading_` on every error path.

- **`outstandingOperations_` counter is a non-atomic `int` shared across async completions**
  - Severity: High
  - Files: `include/FileManager.hpp` (~line 61: `int outstandingOperations_ = 0;`), `src/FileManager.cpp` (`IncrementOutstandingOperations` ~line 232, `DecrementOutstandingOperations` ~line 211)
  - Evidence: Increment/decrement run from io_context completion handlers. With more than one io thread (or multiple io_contexts sharing the FileManager singleton) this is a data race; a lost decrement means `ioc->stop()` never fires (hang), a lost increment can drive the counter to 0 early and `stop()` in-flight work. Additionally `GetOutstandingOperationsPointer()` (~line 238) returns `std::make_shared<int>(outstandingOperations_)` — a **copy** wrapped in a shared_ptr, so callers observing it never see updates. The API is broken as designed.
  - Fix: Make the counter `std::atomic<int>`; replace `GetOutstandingOperationsPointer` with an atomic getter or remove it.

- **`ioc->stop()` as completion mechanism drops queued handlers**
  - Severity: High
  - Files: `src/FileManager.cpp` (~lines 224–228)
  - Evidence: When the counter hits zero the shared `io_context` is stopped abruptly. Any already-posted but unexecuted handlers (e.g., a `finalcall` posted by a just-finished save) are abandoned — a latent lost-callback/lost-data bug when operations overlap. Also, `DecrementOutstandingOperations` is paired with whichever `ioc` the caller supplied; a single caller stopping the context affects all users of that context.
  - Fix: Use `boost::asio::executor_work_guard` / `run_until` semantics, or track per-operation completion rather than stopping the context.

- **`SINGLETON_PTR` instances leak and initialization is not thread-safe**
  - Severity: High
  - Files: `include/ASIOSingleton.hpp` (`SINGLETON_PTR` macro: `if ( _instance == nullptr ) _instance = new the_class();`), used by all loaders/savers (e.g. `src/HTTPLoader.cpp` line ~20)
  - Evidence: Unsynchronized lazy `new` (racy double-init if `InitializeSingleton()` is called from two threads), never deleted (acceptable for program-lifetime singletons, but the raw `_instance` pointer also defeats order-of-destruction control at shutdown). Note `SINGLETON_REF` (Meyers singleton, used by `FileManager`) is the safer variant — prefer it.
  - Fix: Standardize on the Meyers-singleton macro (`SINGLETON_REF`) or add a static-init guard; document that `InitializeSingletons()` must be called once from one thread before any async use.

- **`catch ( ... )` swallowing with degraded diagnostics**
  - Severity: High
  - Files: `src/FileManager.cpp` (~line 143), `src/HTTPCommon.cpp` (~line 111), `src/SFTPCommon.cpp` (~line 59), `src/SFTPSaver.cpp` (~line 100), `src/WSCommon.cpp` (~line 60)
  - Evidence: Broad catch-alls log a generic string (or nothing) and continue, hiding root causes; in `SFTPCommon.cpp` the catch-all path leads directly to the fall-through bug above.
  - Fix: Catch `std::exception&`, log `e.what()`, propagate a typed failure through the status/completion callback, and return.

## Medium (tech debt, duplication, dependency risk)

- **`SFTPSaver.cpp` re-implements the SFTP handshake/auth state machine three times**
  - Severity: Medium
  - Files: `src/SFTPSaver.cpp` (662 lines; inline `StartSFTPUpload`/`StartSFTPHandshake`/`StartSFTPAuth` at ~lines 60–230 duplicate the upload path again at ~lines 540–566), `src/SFTPCommon.cpp` (497 lines, same pattern for download)
  - Evidence: Resolve→connect→handshake→auth (`libssh2_userauth_publickey_fromfile` / `libssh2_userauth_password`) is copy-pasted across at least three sites; fixes (e.g. the port-parsing added only in `SFTPSaver.cpp` ~lines 74–81, absent from `SFTPCommon.cpp` which hardcodes `"22"`) have already diverged.
  - Fix: Extract a single shared SFTP session-establishment helper (in `SFTPCommon`) parameterized by direction; delete the copies.

- **Vendored `httplib.h` 0.14.2 (7,788 lines) with open upstream TODOs**
  - Severity: Medium
  - Files: `include/httplib.h` (`#define CPPHTTPLIB_VERSION "0.14.2"` line 10; TODOs at lines 3415, 3421, 4738, 6470, 6803, 6903, 7016, 7166)
  - Evidence: Single-header vendor drop pinned in-tree; no fetch/upgrade mechanism visible in `cmake/`. Upstream fixes (including security hardening around decompression/FD handling flagged by those TODOs) require manual re-vendoring.
  - Fix: Move to a CMake `FetchContent`/CPM dependency pinned by version, or at minimum record the upstream commit and upgrade procedure next to the vendored copy.

- **`MNNExample.cpp` duplicated at root and under `example/` (diverged)**
  - Severity: Medium
  - Files: `MNNExample.cpp` (106 lines, root — not referenced by any `add_executable`), `example/MNNExample.cpp` (261 lines — the one built via `example/CMakeLists.txt`)
  - Evidence: Binary compare shows the files differ; the root copy is dead code that will silently rot and mislead readers.
  - Fix: Delete the root `MNNExample.cpp`.

- **`.mnn` model binaries committed to the repo**
  - Severity: Medium
  - Files: `test/1.mnn`, `test/2.mnn` (tracked per `git ls-files`)
  - Evidence: Binary test fixtures bloat the repo and make diffs/reviews noisy; provenance/licensing of the models is undocumented.
  - Fix: Generate or download fixtures in CMake test setup, or isolate in LFS; document their origin in `test/CMakeLists.txt`.

- **`build/` directory mixes committed CMake wrappers with local artifacts**
  - Severity: Medium
  - Files: `build/CommonBuildParameters.cmake`, `build/CommonCompilerOptions.CMake`, `build/CompilationFlags.cmake`, `build/Linux/CMakeLists.txt`, `build/OSX/CMakeLists.txt`, `build/Windows/CMakeLists.txt` (all tracked) alongside untracked-but-present `build/Windows/Debug/**` (`.sln`, `.vcxproj`, `CMakeCache.txt`, `hang.dmp`)
  - Evidence: Platform wrapper scripts living in a directory named `build/` (which `.gitignore` half-covers via `Debug/`/`Release` patterns — note `Release` pattern would also ignore any legit path named `Release`) invites accidental commits of generated state and confuses fresh configure. `hang.dmp` on disk is leftover from a deadlock investigation with no accompanying notes.
  - Fix: Rename the wrapper directory (e.g. `cmake/platform/`); root the ignore at `/build/`; capture the hang-analysis outcome as an issue before deleting `hang.dmp`.

- **Tests depend on live network, local sshd, and a Node.js websocket server; sync via sleeps**
  - Severity: Medium
  - Files: `test/src/ipfs_loader_test.cpp` (line ~35: unconditional `sleep_for(2s)` before skip check), `test/src/sftp_saver_test.cpp` (lines ~193, ~225, ~263: `sleep_for` polling; shells out to `ssh-keygen` and starts sshd, skipping when absent), `test/src/ipfs_device_test.cpp`, `test/src/ipfs_saver_test.cpp` (require BitswapNode/bootstrapped IPFS infra), `test/websocketserver/package.json` + `websocket-server.js` (Node runtime needed for `ws://` tests), `test/src/http_loader_test.cpp` (spins local https server on a thread, line ~52)
  - Evidence: IPFS tests hit real swarm infrastructure (flaky, environment-dependent, possibly slow); SFTP tests mutate the host (generates keys, runs sshd); fixed sleeps create race-prone timing coupling.
  - Fix: Gate network tests behind a CTest label (e.g. `NETWORK`), replace sleeps with condition-variable/future waits on completion callbacks, containerize the sshd fixture.

- **`add_definitions` + `-DBOOST_BIND_GLOBAL_PLACEHOLDERS` at top level; toolchain file forced via cache**
  - Severity: Medium
  - Files: `CMakeLists.txt` (lines ~8–16: `set(CMAKE_TOOLCHAIN_FILE ...)` inside the project's own list file — runs after compiler checks in some flows and only takes effect on first configure; `add_definitions(-D_WIN32_WINNT=0x0601)`, `add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)` before `project()`), `cmake/toolchain/cxx17.cmake`
  - Evidence: `BOOST_BIND_GLOBAL_PLACEHOLDERS` is the deprecation-escape hatch for `boost::bind` placeholders (`_1`, `_2` as globals); no `boost::bind` usages were found in first-party code (grep clean), suggesting the define only exists to keep the vendored/dep code quiet — i.e. latent deprecated API usage somewhere in the dependency chain. `add_definitions` is legacy vs `add_compile_definitions`, and global defines leak into any `add_subdirectory` consumer.
  - Fix: Switch to `add_compile_definitions`; move toolchain selection to preset/CI invocation (`CMakePresets.json`); attempt removing the placeholders define to surface the real usage.

- **Non-atomic `downloading_` / `uploading_` guards**
  - Severity: Medium
  - Files: `src/SFTPCommon.cpp` (`downloading_` checked/set in `StartSFTPDownload`), `src/SFTPSaver.cpp` (`uploading_` in `StartSFTPUpload`)
  - Evidence: Plain bools guarding re-entry across async completions; not atomic, and never reset on error paths (see High item above), so a failed transfer permanently disables the device.
  - Fix: `std::atomic<bool>` + RAII reset on all exits.

- **Raw non-owning pointers in handler maps**
  - Severity: Medium
  - Files: `include/FileManager.hpp` (~lines 51–58: `map<std::string, FileLoader *> loaders;` etc.), `src/FileManager.cpp` (`RegisterLoader`/`RegisterSaver`)
  - Evidence: Maps store raw pointers to heap singletons created via `SINGLETON_PTR` (never freed); nothing enforces lifetime or initialization order — a map populated before `InitializeSingleton()` of a dependency would hold nulls.
  - Fix: Store `std::shared_ptr` (make singletons `enable_shared_from_this`-based) or at minimum assert non-null at registration.

## Low (hygiene, naming, minor debt)

- **`std::cerr` used instead of the project logger in SFTP paths**
  - Severity: Low
  - Files: `src/SFTPCommon.cpp` (throughout, e.g. ~lines 39–72), `src/SFTPSaver.cpp` (~lines 65–115 diagnostics)
  - Evidence: Project has `asiomgr-logger` (`include/asiomgr-logger.hpp`, `src/asiomgr-logger.cpp`, spdlog-based) and other modules use `m_logger`; SFTP code bypasses it, losing level control and formatting.
  - Fix: Route through `sgns::asiomgr::Logger`.

- **Stub synchronous APIs returning dummy data**
  - Severity: Low
  - Files: `src/HTTPLoader.cpp` (`LoadFile` returns a static dummy string with `TODO: scorpioluck20`, ~line 33), `src/SFTPLoader.cpp` (~line 35), `src/WSLoader.cpp` (~line 44)
  - Evidence: `FileManager::LoadFile`/`SaveFile` (sync API, `src/FileManager.cpp` ~lines 150–203) will silently hand callers garbage for these prefixes — the dummy `shared_ptr` with no-op deleter is especially misleading.
  - Fix: Throw `std::runtime_error("not implemented")` from the stubs until implemented.

- **Dead callback and empty handler**
  - Severity: Low
  - Files: `src/FileManager.cpp` (~line 28: `void AsyncHandler( boost::system::error_code ec, std::size_t n, std::vector<char> &buffer ) {}` — unreferenced empty function)
  - Fix: Delete.

- **`#include <bitswap.hpp>` leaks into `FileManager.cpp`**
  - Severity: Low
  - Files: `src/FileManager.cpp` (line ~12), `include/FileManager.hpp` (bitswap types in public signature)
  - Evidence: The facade depends directly on ipfs-bitswap internals, coupling the generic URL layer to a specific backend and dragging its includes into every consumer.
  - Fix: Introduce an opaque interface/forward declarations in the header.

## Security Notes (creds, TLS, input validation)

- TLS: `verify_none` in `src/HTTPCommon.cpp` — see Critical. WebSocket path sets `no_sslv2|no_sslv3` and default verify paths but never installs a verify callback (`src/WSCommon.cpp` ~line 79 has the commented-out `// ctx->set_verify_callback(...);`), leaving hostname checking effectively unenforced.
- Credentials: SFTP user/pass/key-passphrase parsed from URLs in `src/URLStringUtil.cpp`; full URL (potentially containing secrets) logged at `debug` level in `src/FileManager.cpp` `LoadASync`/`SaveASync`. libssh2 host-key verification absent (see Critical).
- Input validation: `parseSFTPUrl` (`src/URLStringUtil.cpp`) performs naive `find`/`substr` parsing — no percent-decoding, no rejection of embedded `@`/`:` ambiguity; a password containing `@` silently misparses into host. `FileManager::LoadASync` throws `std::range_error` on unknown prefix — throwing across an async boundary from a public API is caller-hostile.

## Performance Notes

- Synchronous DNS resolution on io threads (`src/SFTPCommon.cpp`, `src/SFTPSaver.cpp`, `src/WSCommon.cpp`) — stalls all pending work on the context; the single biggest latency risk under load.
- `libssh2_session_set_blocking(session, 0)` + `async_wait` polling loop (`src/SFTPCommon.cpp` handshake, `src/SFTPSaver.cpp` handshake) is correct-ish but spins one wait per EAGAIN; a single wait-read on the socket per retry is fine, but the duplicated state machines make it easy to miss a branch and busy-loop.
- Fixed 2-second sleeps in IPFS tests (`test/src/ipfs_loader_test.cpp` line ~35) and 1–2 s waits in SFTP tests inflate CI time linearly with test count.
- `GetOutstandingOperationsPointer()` allocates a fresh `shared_ptr<int>` copy per call (`src/FileManager.cpp` ~line 238) — pointless allocation for a value that is wrong anyway (see High).

## Recommended Cleanup Order

1. **Stop the bleeding (security):** enable TLS peer verification (`src/HTTPCommon.cpp`), add libssh2 known-hosts checks, stop logging raw URLs, move SFTP creds out of URL strings (`src/URLStringUtil.cpp`, `src/SFTPCommon.cpp`, `src/SFTPSaver.cpp`).
2. **Fix crash/UB bugs:** guard the `parsers.find("mnn")` dereference and re-enable or remove parser registration in `src/FileManager.cpp`; add early `return` after resolve failures in `src/SFTPCommon.cpp`.
3. **Threading hardening:** `std::atomic<int>` for `outstandingOperations_`, fix `GetOutstandingOperationsPointer`, atomic re-entry flags, replace `ioc->stop()` completion semantics (`include/FileManager.hpp`, `src/FileManager.cpp`).
4. **De-duplicate SFTP state machine:** one shared connect/handshake/auth helper used by loader and saver (`src/SFTPCommon.cpp`, `src/SFTPSaver.cpp`).
5. **Async correctness:** replace synchronous `resolver.resolve` with `async_resolve` in all three loaders/savers.
6. **Repo hygiene:** delete root `MNNExample.cpp`, relocate `build/*` wrapper CMake files, tighten `.gitignore`, decide fate of `.mnn` fixtures and `hang.dmp`, vendor-manage `include/httplib.h`.
7. **Test stabilization:** replace sleeps with callback-based waits, label network-dependent tests, containerize sshd/websocket fixtures.

---

*Concerns audit: 2026-09-03 (HEAD 009dc3dc04599c61d04aca5ba0cf547703543b31)*
