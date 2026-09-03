---
focus: tech
created: 2026-09-03
---
# External Integrations

All network integrations implement the `FileLoader` / `FileSaver` interfaces (`include/FileLoader.hpp`, `include/FileSaver.hpp`) and are dispatched by URL scheme prefix through `FileManager` (`include/FileManager.hpp`, `src/FileManager.cpp`). Every operation is asynchronous on a shared `boost::asio::io_context` and reports via `CompletionCallback`; errors use `libp2p::outcome::result`.

## Network Protocols (HTTP, WS, SFTP, IPFS/bitswap, libp2p)

**HTTP / HTTPS:**
- `HTTPLoader` (`include/HTTPLoader.hpp`, `src/HTTPLoader.cpp`) — prefix `http(s)://`
- Transport: raw Boost.Asio + OpenSSL in `HTTPDevice` (`include/HTTPCommon.hpp`, `src/HTTPCommon.cpp`): `ssl::stream<tcp::socket>`, TLS client handshake, `verify_none` on an opt-in path
- Vendored `cpp-httplib` 0.14.2 header exists at `include/httplib.h` but is not referenced by any source; do not assume it is wired in

**WebSocket / WSS:**
- `WSLoader` (`include/WSLoader.hpp`, `src/WSLoader.cpp`) — prefix `ws(s)://`
- Transport: Boost.Beast `websocket::stream<ssl::stream<tcp::socket>>` in `WSDevice` (`include/WSCommon.hpp`, `src/WSCommon.cpp`); chains TCP resolve → SSL handshake → WS handshake; error enum `COULD_NOT_RESOLVE / HANDSHAKE_ERROR / WS_HANDSHAKE_ERROR`

**SFTP:**
- `SFTPLoader` (`include/SFTPLoader.hpp`, `src/SFTPLoader.cpp` — loader not compiled into the lib currently; see `src/CMakeLists.txt`) and `SFTPSaver` (`include/SFTPSaver.hpp`, `src/SFTPSaver.cpp`) — prefix `sftp://`
- Transport: libssh2 over a Boost.Asio socket in `SFTPDevice` (`include/SFTPCommon.hpp`, `src/SFTPCommon.cpp`); non-blocking pattern keyed on `LIBSSH2_ERROR_EAGAIN` with `libssh2_session_set_blocking(0)`
- Auth priority: private key file → public key file → user/password (`libssh2_userauth_publickey_fromfile` then `libssh2_userauth_password`); credentials supplied per-call via `SFTPDevice` ctor, never stored globally

**IPFS / bitswap:**
- `IPFSLoader` (`include/IPFSLoader.hpp`, `src/IPFSLoader.cpp`) and `IPFSSaver` (`include/IPFSSaver.hpp`, `src/IPFSSaver.cpp`) — prefix `ipfs://<CID>/<file>`
- Stack: ipfs-bitswap-cpp `Bitswap` engine over libp2p host, ipfs-lite-cpp Kad DHT (`IpfsDHT`), graphsync, blockservice; CID parse via `libp2p::multi::ContentIdentifierCodec`
- Host application may inject an existing node: `IPFSLoader::setBitswap(bitswap, dht)` / `IPFSSaver::setBitswap(...)` + `FileManager::setBitswap(...)` (see `example/MNNExample.cpp` line ~218); loaders fall back to creating an ephemeral `IPFSDevice` when no external instance is set
- Publishing announces CIDs through DHT (`IPFSSaver::AnnounceCID`) and supports provider pinning (`bitswap->AddProvider`)

**libp2p plumbing (shared):**
- Host construction via `libp2p/injector/host_injector.hpp` + `kademlia_injector` + Boost.DI shared scope — reference composition in `example/MNNExample.cpp` and `test/testutil/bitswap_node.hpp`
- Linked protocol targets enumerated in `src/CMakeLists.txt` (p2p_default_network, p2p_kademlia, p2p_gossip, p2p_identify, p2p_ping, p2p_cares, dnsaddr, ...)

## Storage (RocksDB, local FS, IPFS)

**Local filesystem:**
- `FILEDevice` in `include/FILECommon.hpp` / `src/FILECommon.cpp` — Asio stream descriptor/file; `MNNLoader` uses it for `file://` async reads (`src/MNNLoader.cpp`)
- `std::filesystem` used for existence checks / path decomposition
- Downloaded data is optionally cached to disk (`save` flag through `FileManager::LoadASync`, cache dir tracked via `FileManager::cacheDir_`)

**IPFS as storage:**
- `IPFSSaver::SaveASync` writes unixfs DAG via bitswap and returns the content location (optional out-param `save_location`)
- `ipfs-lite-cpp::ipfs_datastore_rocksdb` is the on-disk block store target linked in `src/CMakeLists.txt`; `ipfs_datastore_in_memory` also available for ephemeral nodes

**RocksDB:** consumed only transitively through ipfs-lite-cpp; no direct `RocksDB::*` API calls in `src/`.

## AI/ML (MNN models)

- MNN inference framework (Alibaba): `include/MNNCommon.hpp` centralizes `MNN/Interpreter.hpp` + `MNN/Tensor.hpp`
- `MNNLoader` (`include/MNNLoader.hpp`, `src/MNNLoader.cpp`) loads `.mnn` bytes; `MNNParser` (registered per suffix `.mnn`) turns buffers into MNN interpreters/sessions; `MNNSaver` (`src/MNNSaver.cpp`) serializes back
- Composable with remote loaders: e.g. `ipfs://<cid>/model.mnn` + `parse=true` streams bytes into the MNN parser through `FileManager` (demonstrated in `example/MNNExample.cpp` + root `MNNExample.cpp` legacy copy)
- Sample models: `test/1.mnn`, `test/2.mnn`

## Logging (soralog/spdlog/asiomgr-logger)

**Two stacked systems — know which one to use:**
1. `sgns::asiomgr::createLogger(tag, basepath)` (`include/asiomgr-logger.hpp`, `src/asiomgr-logger.cpp`) — spdlog wrapper; console (color) by default, file sink when `basepath` given, Android sink under `#if defined(ANDROID)`; patterns set in `setGlobalPattern`/`setDebugPattern`. Use this inside AsyncIOManager classes.
2. soralog + libp2p `Configurator` with embedded YAML — bootstrapped inside `IPFSLoader::LoadASync()` (`src/IPFSLoader.cpp`) and by the host app; silences noisy libp2p/kademlia/Bitswap loggers (`soralog::Level::OFF`, `spdlog::level::off`)
- Most singletons carry an `m_logger = sgns::asiomgr::createLogger("<ClassName>")` member (e.g. `include/FileManager.hpp`, `include/IPFSSaver.hpp`)

## Test-support services (test/websocketserver Node.js)

- `test/websocketserver/websocket-server.js` — Node.js `ws` server (dep: `ws ^8.15.1`, `test/websocketserver/package.json`); serves files over WS on port 8080 and WSS on port 8090, mapping connections to file paths; run manually when developing `WSLoader`
- `test/testutil/` — C++ fixtures: `test_fixture.hpp`, `bitswap_node.hpp` (local bitswap node for IPFS tests), `temp_file.hpp`, `asio_helpers.hpp`; exposed via INTERFACE lib `asiomgr_testutil` (`test/testutil/CMakeLists.txt`)
- `test/src/http_loader_test.cpp` embeds a Boost.Beast HTTPS server using a generated self-signed cert (`test/src/CMakeLists.txt` runs `openssl req` into `test_certs/`, gated behind `ASYNC_IO_MANAGER_NETWORK_TESTS`)
- `sftp_saver_test` requires a local sshd (also network-gated)
- `.mnn` fixture models in `test/`

## Environment / secrets notes

- No `.env` files, no API keys, no auth providers beyond per-call SFTP credentials (user/pass or key files passed as ctor args)
- Test TLS material is generated at build time (`test_certs/cert.pem`, `key.pem`) — never commit
- HTTPS loader has an opt-in `verify_none` path in `src/HTTPCommon.cpp` — certificate validation is not enforced there; see CONCERNS for hardening

---

*Integration audit: 2026-09-03*
