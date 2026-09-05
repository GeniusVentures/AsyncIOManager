# AsyncIOManager

A C++17 asynchronous I/O library for loading and saving data across local and remote protocols behind one URL-dispatched `FileManager` API, built on Boost.Asio. Point any operation at a URL and the handler registered for that prefix does the rest.

## Protocol Support

| Prefix | Load | Save | Notes |
|--------|------|------|-------|
| `file://` | ✓ | ✓ | Local files via `LocalFileLoader` / `LocalFileSaver` |
| `https://` | ✓ | — | `HTTPLoader` (plain `http://` registration is disabled) |
| `ipfs://` | ✓ | ✓ | `IPFSLoader` / `IPFSSaver` (bitswap-backed) |
| `sftp://` | dormant (not initialized) | ✓ | `SFTPSaver` active; `SFTPLoader` built but not initialized |
| `wss://` | dormant (not initialized) | — | `WSLoader` built but not initialized |

## Design

Loaders and savers are self-registering singletons. Each one registers its URL prefix with `FileManager::GetInstance()`; `FileManager::InitializeSingletons()` activates the handlers listed above, and every load/save call is dispatched by URL prefix.

## Public API

Signatures copied from `include/FileManager.hpp`:

```cpp
using ResultType = outcome::result<std::shared_ptr<
    std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>;
using FinalCallback = std::function<void( ResultType buffers )>;

shared_ptr<void> LoadASync( const std::string                       &url,
                            bool                                     save,
                            std::shared_ptr<boost::asio::io_context> ioc,
                            FinalCallback                            finalcall,
                            std::string                              savetype );

void SaveASync( const std::string                       &url,
                ResultType                               data,
                std::shared_ptr<boost::asio::io_context> ioc,
                FinalCallback                            finalcall,
                std::shared_ptr<std::string>             save_location = nullptr );
```

`outcome::result<T>` has a deleted default constructor — capture results with `std::optional<FileManager::ResultType>` (assign on success; an empty optional after the callback means failure, reported through `result.error().message()`).

## Dependencies

All dependencies are resolved from a prebuilt thirdparty tree at configure time (no downloads):

- Boost.Asio / Boost.Beast / Boost.Asio SSL (OpenSSL backend)
- libp2p
- ipfs-lite / ipfs-bitswap
- libssh2
- spdlog
- Google Test — only when building the test suite

## Build

The CMake wrapper lives at `build/<Platform>/` (e.g. `build/Windows/`) and requires a prebuilt thirdparty tree pointed at by `THIRDPARTY_DIR`. The root `CMakeLists.txt` is what the super-build's ExternalProject consumes — do not configure it directly.

Windows (Visual Studio 2022):

```sh
cd build/Windows
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=W:\gnus\GeniusNetwork\thirdparty
cmake --build . --parallel 8 --config Release
```

POSIX (make):

```sh
cmake .. -DCMAKE_BUILD_TYPE=Debug -DTHIRDPARTY_DIR=/Users/fuu/gnus/thirdparty/
make -j8
```

POSIX (Ninja):

```sh
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=/Users/fuu/gnus/thirdparty/
ninja -j8
```

Add `-DTESTING=ON` to the configure step to build the test suite (optional; defaults ON).

## Run the Example

```
FileExample [input-url] [output-dir-url]
```

Defaults: input `file://example_data.bin` (relative to the process CWD — run from `example/`), output `file://example_output/` (the roundtrip lands in `example_output/<basename>`).

Expected output:

```
Loaded "example_data.bin" (1024 bytes) from file://example_data.bin
Saved "example_data.bin" (1024 bytes) to file://example_output/
```

Exit code 0 on success; 1 on failure, delivered via the completion callback (errors are never thrown).