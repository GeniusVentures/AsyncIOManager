# Phase 3: Parser Layer Removal - Pattern Map

**Mapped:** 2026-09-04
**Files analyzed:** 28 (11 modified headers + 1 deleted header + 10 modified sources + 5 test files + 1 example)
**Analogs found:** 28 / 28 — this is a signature-purging refactor, so every "analog" is the file itself (current signature → post-edit target shape). The two genuinely NEW code artifacts (D-19 test body, D-20 guard block) map to in-tree patterns detailed below.

**Key framing for the planner:** PARSE-02 is not "copy a pattern from elsewhere" — it is "remove one parameter from an existing, type-coupled shape." Every excerpt below shows BOTH the current code (line-verified 2026-09-04) AND the post-edit target. The compile cascade (base virtual + 9 callback aliases) makes PARSE-02 atomic; PARSE-01 + D-20 is independently compilable (commit-slicing opportunity per RESEARCH.md Discovery 3).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/FileParser.hpp` | interface (dead) | n/a | itself — whole-file DELETE (16 lines, only includer is `FileManager.hpp:13`) | exact |
| `include/FileLoader.hpp` | interface (base) | request-response | itself — the type-coupling strip root | exact |
| `include/FileManager.hpp` | service (registry) | event-driven | itself (PARSER deletions + alias strip) | exact |
| `include/LocalFileLoader.hpp` | loader header | file-I/O | itself — **canonical loader header analog** for HTTPLoader/IPFSLoader/SFTPLoader/WSLoader | exact |
| `include/HTTPLoader.hpp` | loader header | request-response | `include/LocalFileLoader.hpp` | exact |
| `include/IPFSLoader.hpp` | loader header | event-driven | `include/LocalFileLoader.hpp` | exact |
| `include/SFTPLoader.hpp` | loader header | streaming | `include/LocalFileLoader.hpp` (compiled — included by `src/FileManager.cpp:8`) | exact |
| `include/WSLoader.hpp` | loader header | streaming | `include/LocalFileLoader.hpp` | exact |
| `include/HTTPCommon.hpp` | device header | streaming | itself + `include/WSCommon.hpp` (same ctor/`parse_` member/alias triad) | exact |
| `include/IPFSCommon.hpp` | device header | event-driven | itself — unique param-threading shape (no `parse_` member) | exact |
| `include/SFTPCommon.hpp` | device header | streaming | `include/HTTPCommon.hpp` / `include/WSCommon.hpp` — **NOT COMPILED, mirror mechanically** | exact |
| `include/WSCommon.hpp` | device header | streaming | itself — **canonical compiled device analog** for SFTP mirroring | exact |
| `src/FileManager.cpp` | service (registry impl + auto-save orchestrator) | event-driven | itself; D-20 error-path analog = own `SaveASync` try/catch (`FileManager.cpp` ~152–160) + posted-failure pattern (`src/HTTPCommon.cpp:100`) | exact |
| `src/LocalFileLoader.cpp` | loader impl | file-I/O | itself — **canonical loader impl analog** (override + capture list + echo + posted failure) | exact |
| `src/HTTPLoader.cpp` | loader impl | streaming | `src/LocalFileLoader.cpp` + `src/WSLoader.cpp` (device ctor arg edit) | exact |
| `src/IPFSLoader.cpp` | loader impl | event-driven | itself — unique `[=]` implicit captures (research Pitfall 4) | exact |
| `src/SFTPLoader.cpp` | loader impl | streaming | `src/WSLoader.cpp` — **NOT COMPILED, mirror mechanically** | exact |
| `src/WSLoader.cpp` | loader impl | streaming | itself | exact |
| `src/HTTPCommon.cpp` | device impl | streaming | `src/WSCommon.cpp` (verified ctor/init/failure/echo shapes) | exact |
| `src/IPFSCommon.cpp` | device impl | event-driven | itself — explicit `[...]` capture lists at ~134, ~206, ~257 | exact |
| `src/SFTPCommon.cpp` | device impl | streaming | `src/WSCommon.cpp` / `src/HTTPCommon.cpp` — **NOT COMPILED, mirror mechanically** | exact |
| `src/WSCommon.cpp` | device impl | streaming | itself — **canonical compiled device impl analog** | exact |
| `test/src/filemanager_test.cpp` | test (+ NEW D-19 test) | file-I/O | own `SaveASync_FilePrefixDispatchesToLocalFileSaver` (lines ~137–163: pollUntil + read-back + EXPECT_EQ) + `localfile_loader_test.cpp` async call shape | exact |
| `test/src/localfile_loader_test.cpp` | test | file-I/O | itself — canonical test call-site analog | exact |
| `test/src/http_loader_test.cpp` | test | request-response | `test/src/localfile_loader_test.cpp` call sites (lines 55–63, 90) | exact |
| `test/src/ipfs_loader_test.cpp` | test | event-driven | `test/src/localfile_loader_test.cpp` call sites | exact |
| `test/src/ipfs_device_test.cpp` | test | event-driven | itself — callback **lambda definitions** (not just call sites) need arity fix (lines ~78–83, 112–119, 176–184) | exact |
| `example/MNNExample.cpp` | example entry point | event-driven | itself — one-line arg drop (lines ~267–273) | exact |

**Do NOT touch** (legit `parse` survivors, per RESEARCH.md Discovery 1): `include/URLStringUtil.h`, `src/URLStringUtil.cpp` (`parseHTTPUrl`/`parseSFTPUrl`/`parseIPFSUrl`), `include/httplib.h` (`parse_*` family). Saver side (`FileSaver.hpp`, `LocalFileSaver.*`, `IPFSSaver.*`, `SFTPSaver.*`) carries no parse concept — zero edits.

---

## Pattern Assignments

### 1. The canonical signature strip — `include/FileLoader.hpp` (the type-coupling root)

This alias + pure virtual type-couple every loader, device, test lambda, and the example. Change it and the cascade is mandatory everywhere.

**Current** (`include/FileLoader.hpp:28-31, 48-53`):

```cpp
    using CompletionCallback =
        std::function<void( std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool parse, bool save )>;
```

```cpp
    virtual std::shared_ptr<void> LoadASync( std::string                              filename,
                                             bool                                     parse,
                                             bool                                     save,
                                             std::shared_ptr<boost::asio::io_context> ioc,
                                             CompletionCallback                       callback ) = 0;
```

**Post-edit target:**

```cpp
    using CompletionCallback =
        std::function<void( std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool save )>;
```

```cpp
    virtual std::shared_ptr<void> LoadASync( std::string                              filename,
                                             bool                                     save,
                                             std::shared_ptr<boost::asio::io_context> ioc,
                                             CompletionCallback                       callback ) = 0;
```

Also delete the `@param parse - Whether to parse file upon completion` doc line above each (doc scrub, see Shared Patterns).

---

### 2. Loader header analog — `include/LocalFileLoader.hpp` (apply same shape to HTTPLoader/IPFSLoader/SFTPLoader/WSLoader)

Each loader header re-declares its own `CompletionCallback` alias and the `LoadASync` override — identical strip per file.

**Current** (`include/LocalFileLoader.hpp:33-40, 50-58`):

```cpp
        using CompletionCallback = std::function<
            void( std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool parse, bool save )>;
```

```cpp
        std::shared_ptr<void> LoadASync( std::string                              filename,
                                         bool                                     parse,
                                         bool                                     save,
                                         std::shared_ptr<boost::asio::io_context> ioc,
                                         CompletionCallback                       callback ) override;
```

**Post-edit target:** drop `bool parse,` from the alias and `bool parse,` from the override parameter list (exact same shape as Pattern 1). The five loader headers differ only in namespace/class name — the edit is byte-identical modulo those.

---

### 3. Loader impl analog — `src/LocalFileLoader.cpp` (canonical; apply to HTTPLoader/IPFSLoader/SFTPLoader/WSLoader)

Four distinct edit sites per loader impl. **Current** (`src/LocalFileLoader.cpp`):

Override definition (line ~57):
```cpp
    std::shared_ptr<void> LocalFileLoader::LoadASync( std::string                              filename,
                                                bool                                     parse,
                                                bool                                     save,
                                                std::shared_ptr<boost::asio::io_context> ioc,
                                                CompletionCallback                       handle_read )
```

Posted failure — tail becomes `false` (line ~65):
```cpp
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::FILE_OPEN_FAIL ), false, false ); } );
```

Async callback capture list — `parse` capture dies (line ~74):
```cpp
            [this, fileDevice, ioc, handle_read, parse, save, buffer, filename]( const boost::system::error_code &error,
                                                                                 std::size_t bytes_transferred )
```

Success echo — one arg dies (line ~86) / failure echo (line ~92):
```cpp
                    handle_read( ioc, finaldata, parse, save );
                    ...
                    handle_read( ioc, outcome::failure( Error::READ_ERROR ), false, false );
```

**Post-edit targets:**

```cpp
    std::shared_ptr<void> LocalFileLoader::LoadASync( std::string                              filename,
                                                bool                                     save,
                                                std::shared_ptr<boost::asio::io_context> ioc,
                                                CompletionCallback                       handle_read )
```

```cpp
                               { handle_read( ioc, outcome::failure( Error::FILE_OPEN_FAIL ), false ); } );
```

```cpp
            [this, fileDevice, ioc, handle_read, save, buffer, filename]( const boost::system::error_code &error,
                                                                         std::size_t bytes_transferred )
```

```cpp
                    handle_read( ioc, finaldata, save );
                    ...
                    handle_read( ioc, outcome::failure( Error::READ_ERROR ), false );
```

**Rule of thumb for all 24+ failure sites:** tail `false, false` → `false`; tail `(parse, save)` / `(self->parse_, self->save_)` → `(save)` / `(self->save_)`.

**Device-construction call sites** (HTTPLoader.cpp ~64, WSLoader.cpp ~68, SFTPLoader.cpp ~60-68): `HTTPDevice( host, path, port, parse, save )` → `HTTPDevice( host, path, port, save )` — drop only the `parse` argument.

---

### 4. Device impl analog — `src/WSCommon.cpp` / `src/HTTPCommon.cpp` (canonical; **SFTP mirrors these — it is NOT compiled, MSVC will not check it**)

**Current** (`src/WSCommon.cpp:28-35` — ctor; `:161` — success echo; `:51`-style posted failure):

```cpp
    WSDevice::WSDevice( std::string ws_host, std::string ws_path, std::string ws_port, bool parse, bool save )
    {
        ws_host_ = ws_host;
        ws_path_ = ws_path;
        ws_port_ = ws_port;
        parse_   = parse;
        save_    = save;
    }
```

```cpp
                                            handle_read( ioc, finaldata, self->parse_, self->save_ );
```

```cpp
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::COULD_NOT_RESOLVE ), false, false ); } );
```

**Post-edit targets:**

```cpp
    WSDevice::WSDevice( std::string ws_host, std::string ws_path, std::string ws_port, bool save )
    {
        ws_host_ = ws_host;
        ws_path_ = ws_path;
        ws_port_ = ws_port;
        save_    = save;
    }
```

```cpp
                                            handle_read( ioc, finaldata, self->save_ );
```

```cpp
                               { handle_read( ioc, outcome::failure( Error::COULD_NOT_RESOLVE ), false ); } );
```

Header side (`HTTPCommon.hpp:67` ctor decl, `:112` `bool parse_;` member, `:57` alias): ctor drops the `bool parse` param (note: parse is the LAST bool before save — drop the right one), delete the `parse_;` member line, strip the alias per Pattern 1. `src/SFTPCommon.cpp` ctor init (`parse_ = ...` at ~27) and ~10 `handle_read` sites (incl. echo at ~400) mirror this exactly — **mechanical mirror, no improvisation, verified only by the grep gate** (RESEARCH.md Discovery 2).

---

### 5. IPFS param-threading analog — `src/IPFSCommon.cpp` / `src/IPFSLoader.cpp` (no `parse_` member; unique capture shapes)

IPFS threads `parse` as a function parameter through `StartFindingPeers` / `StartFindingPeersWithRetry` / `RequestBlockMain` / `convertUnixFSContentToResult` and their lambda capture lists. **Current** (`src/IPFSCommon.cpp:102-109, ~134`):

```cpp
    bool IPFSDevice::StartFindingPeers( std::shared_ptr<boost::asio::io_context> ioc,
                                        const sgns::ipfs_bitswap::CID           &cid,
                                        std::string                              filename,
                                        int                                      addressoffset,
                                        bool                                     parse,
                                        bool                                     save,
                                        CompletionCallback                       handle_read )
```

```cpp
            [self, ioc, cid, filename, addressoffset, parse, save, handle_read](
                libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> res )
```

plus positional forwards inside the body:
```cpp
            return RequestBlockMain( ioc, cid, filename, addressoffset, parse, save, handle_read );
```

**Post-edit target:** delete the `bool parse,` parameter line, delete `parse, ` from every explicit `[...]` capture list, delete `parse, ` from every positional call. In `src/IPFSLoader.cpp` the captures at ~159/163/215 are `[=]` (implicit) — removing the positional `parse` argument from the call kills the implicit capture automatically. **Run a plain-`parse` eyeball grep on exactly these 4 files afterward** (`IPFSCommon.cpp`, `IPFSLoader.cpp`, `SFTPCommon.cpp`, `SFTPLoader.cpp`) — bare identifiers in capture lists are invisible to the tightened gate (Pitfall 4).

---

### 6. Registry deletion (PARSE-01) — `include/FileManager.hpp` + `src/FileManager.cpp`

**Delete verbatim** from `include/FileManager.hpp`:

```cpp
#include "FileParser.hpp"                                    // line 13
    /// @brief a map from std::string to parser handlers
    map<std::string, FileParser *> parsers;                  // lines 49-50
    /// @brief Register a synchronous Parser class to handle a specific extension suffix
    /// @param suffix = ".bin", ".jpg", etc from file://file.jpg
    /// @param handlerParser Handler class object that can parse the data
    void RegisterParser( const std::string &suffix, FileParser *handlerParser );   // lines 90-93
    /// @brief Parse Data from a previously loaded file
    /// @param suffix the extension/suffix to know how to parse the data
    /// @param data
    /// @return shared pointer to void * of the data parsed
    shared_ptr<void> ParseData( const std::string &suffix, shared_ptr<void> data ); // lines 137-140
```

**Delete verbatim** from `src/FileManager.cpp`:

```cpp
void FileManager::RegisterParser( const std::string &suffix, FileParser *handlerParser )   // lines 18-21
{
    parsers[suffix] = handlerParser;
}
```

The whole `FileManager::ParseData` method (lines ~166–178) and in `LoadFile` (line ~142 → `shared_ptr<void> FileManager::LoadFile( const std::string &url )`) the branch:

```cpp
    if ( parse )
    {
        data = ParseData( suffix, data );
    }
```

`FileManager::LoadFile` callers all pass 1 arg today — param drop is caller-neutral. `FileManager.hpp` `CompletionCallback` alias (~line 71) + `LoadASync` decl (~line 109) get the Pattern-1 strip. Note `FileManager::LoadASync`'s local `handle_read` lambda is 4-param today (see Pattern 7).

---

### 7. D-20 UB guard — `src/FileManager.cpp` auto-save wrapper (the NEW code's error-path analog)

**Current latent UB** (`src/FileManager.cpp:62-96`, the `handle_read` lambda inside `FileManager::LoadASync`; UB at ~74-75):

```cpp
    auto handle_read = [this, savetype, suffix, finalcall]( std::shared_ptr<boost::asio::io_context> ioc,
                                                            ResultType                               buffers,
                                                            bool                                     parse,
                                                            bool                                     save )
    {
        if ( buffers )
        {
            //Save data or otherwise decrement counter of operations
            if ( save )
            {
                auto handle_write = [this]( std::shared_ptr<boost::asio::io_context> ioc )
                { DecrementOutstandingOperations( ioc ); };
                auto saverIter = savers.find( savetype );
                auto saver     = saverIter->second;          // ← unchecked deref: UB when savetype unregistered
                saver->SaveASync( ioc, handle_write, "", buffers, suffix );
            }
            ...
        }
        ...
        finalcall( buffers );
    };
```

**Post-edit target** (parse param dropped AND guard added; error shape mirrored from `FileManager::SaveASync`'s decrement-before-fail at ~152–160 and the posted-failure pattern):

```cpp
    auto handle_read = [this, savetype, suffix, finalcall]( std::shared_ptr<boost::asio::io_context> ioc,
                                                            ResultType                               buffers,
                                                            bool                                     save )
    {
        if ( buffers )
        {
            //Save data or otherwise decrement counter of operations
            if ( save )
            {
                auto handle_write = [this]( std::shared_ptr<boost::asio::io_context> ioc )
                { DecrementOutstandingOperations( ioc ); };
                auto saverIter = savers.find( savetype );
                if ( saverIter == savers.end() )
                {
                    m_logger->error( "No saver registered for savetype: {}", savetype );
                    DecrementOutstandingOperations( ioc );
                    finalcall( outcome::failure( std::make_error_code( std::errc::operation_not_supported ) ) );
                    return;   // MUST return — trailing finalcall(buffers) must not double-fire (Pitfall 1)
                }
                auto saver = saverIter->second;
                saver->SaveASync( ioc, handle_write, "", buffers, suffix );
            }
            else
            {
                // Handle completion
                DecrementOutstandingOperations( ioc );
            }
        }
        else
        {
            DecrementOutstandingOperations( ioc );
        }
        finalcall( buffers );
    };
```

**Decrement-before-fail precedent** (`src/FileManager.cpp` ~152–160, inside `FileManager::SaveASync` — the D-20 guard mirrors this discipline):

```cpp
    try
    {
        saver->SaveASync( ioc, handle_write, filePath, data, suffix, save_location );
    }
    catch ( ... )
    {
        DecrementOutstandingOperations( ioc );
        throw;
    }
```

Failure value: `std::error_code` via `std::make_error_code(std::errc::...)` (research A1 — discretion granted; do NOT mint a new `Error` enum + category for one site). The dispatch tail becomes `loader->LoadASync( filePath, save, ioc, handle_read )` (was `filePath, parse, save, ...`).

---

### 8. NEW D-19 test — `test/src/filemanager_test.cpp` (auto-save chain, `LoadASync_SaveTrueAutoSavesLoadedDataToDisk`)

**Analog A — test skeleton + read-back assert** (`test/src/filemanager_test.cpp:137-163`, `SaveASync_FilePrefixDispatchesToLocalFileSaver` — copy its pollUntil-then-read-back-then-EXPECT_EQ rhythm):

```cpp
TEST_F( FileManagerIntegrationTest, SaveASync_FilePrefixDispatchesToLocalFileSaver )
{
    IOContextRunner runner;
    TempDir         dir;
    std::string     content  = "filemanager async save";
    std::string     fileName = "fm_async_save.bin";

    bool completed = false;

    auto data = makeSingleFileResult( fileName, content );

    FileManager::GetInstance().SaveASync(
        "file://" + dir.pathString() + "/", data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for async save";

    auto          fullPath = dir.path() / fileName;
    std::ifstream ifs( fullPath, std::ios::binary );
    ASSERT_TRUE( ifs.is_open() ) << "File not found: " << fullPath;
    std::string readBack( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    EXPECT_EQ( readBack, content );
}
```

**Analog B — `FileManager::LoadASync` call shape** (`test/src/localfile_loader_test.cpp:51-64`, post-strip arity):

```cpp
    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(),
        false,       // save
        runner.ioc(),
        [&]( FileManager::ResultType result ) { ... },
        "" );        // savetype
```

**Harness primitives** (`test/testutil/asio_helpers.hpp:56-70` — `pollUntil` takes ANY predicate, so a filesystem check drops straight in; `test/testutil/temp_file.hpp` — `TempFile(content)` writes under `temp_directory_path` and exposes `path()/pathString()`; `TempDir` RAII-creates/removes a dir):

```cpp
template <typename Predicate>
bool pollUntil( Predicate pred, std::chrono::milliseconds timeout = std::chrono::seconds( 30 ),
                std::chrono::milliseconds interval = std::chrono::milliseconds( 50 ) )
```

**Assembled D-19 target shape** (test lands in `FileManagerIntegrationTest`; `finalcall` is NOT the save-done signal — poll the filesystem):

```cpp
TEST_F( FileManagerIntegrationTest, LoadASync_SaveTrueAutoSavesLoadedDataToDisk )
{
    const std::string expected = "auto-save chain content";
    TempFile          tf( expected );   // absolute path under temp_directory_path — unaffected by CWD swap
    TempDir           dir;              // becomes process CWD (LocalFileSaver writes <uuid>/ relative to CWD)
    IOContextRunner   runner;

    // RAII CWD guard: swap current_path to dir, restore on exit (survives ASSERT failures;
    // gtest serial in-process). NO in-tree analog — synthesize per this pattern:
    // struct CwdGuard { std::filesystem::path old; CwdGuard(auto p){old=std::filesystem::current_path();
    //   std::filesystem::current_path(p);} ~CwdGuard(){std::filesystem::current_path(old);} };

    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(),
        true,          // save=true → auto-save chain via savetype "file" → LocalFileSaver
        runner.ioc(),
        []( FileManager::ResultType ) {},   // fires at load completion, BEFORE save completes
        "file" );

    const auto basename = tf.path().filename().string();
    bool ok = pollUntil(
        [&]()
        {
            for ( auto &e : std::filesystem::directory_iterator( dir.path() ) )
            {
                auto name = e.path().filename().string();
                if ( name.size() == 36 && std::count( name.begin(), name.end(), '-' ) == 4
                     && std::filesystem::exists( e.path() / basename ) )
                {
                    return true;   // UUID subdir (36 chars, 4 dashes) containing the original filename
                }
            }
            return false;
        },
        std::chrono::seconds( 10 ) );
    ASSERT_TRUE( ok ) << "auto-save UUID directory never appeared";

    // Depth: locate the UUID dir again, read back bytes, EXPECT_EQ against `expected`
    // (rhythm copied verbatim from Analog A's readBack block).
}
```

**On-disk contract being asserted** (`src/LocalFileSaver.cpp:69-71` — empty filename → UUID dir relative to process CWD; do NOT change the hardcoded `""` upstream):

```cpp
        if ( filename.empty() )
        {
            filename = boost::lexical_cast<string>( ( boost::uuids::random_generator() )() ) + "/";
        }
```

---

### 9. Test call-site arity fixes — `localfile_loader_test.cpp` (canonical), `http_loader_test.cpp`, `ipfs_loader_test.cpp`, `ipfs_device_test.cpp`, `filemanager_test.cpp:126-131`

**Current** (`test/src/localfile_loader_test.cpp:55-63`):

```cpp
    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(),
        false,       // parse
        false,       // save
        runner.ioc(),
```

**Post-edit target:**

```cpp
    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(),
        false,       // save
        runner.ioc(),
```

Same mechanical drop at `localfile_loader_test.cpp:92`, `http_loader_test.cpp:186,222,247`, `ipfs_loader_test.cpp:~150,221,293,321`, `filemanager_test.cpp:~127`.

**`ipfs_device_test.cpp` is different — callback LAMBDA DEFINITIONS also carry the arity** (type-checked against `IPFSDevice::CompletionCallback`). Current (lines ~78-83, 112-119, ~176-184):

```cpp
    IPFSDevice::CompletionCallback callback =
        [fired]( std::shared_ptr<boost::asio::io_context>, IPFSDevice::ResultType, bool, bool )
        {
            *fired = true;
        };
```

Post-edit (one `bool` fewer in the lambda parameter list):
```cpp
    IPFSDevice::CompletionCallback callback =
        [fired]( std::shared_ptr<boost::asio::io_context>, IPFSDevice::ResultType, bool )
        {
            *fired = true;
        };
```

And the device calls at ~85/123/184: `StartFindingPeersWithRetry( runner.ioc(), cid, "uaf_probe.bin", 0, false, false, callback )` → drop one `false` (keep ONE — it's the save flag now).

---

### 10. Example one-liner — `example/MNNExample.cpp` (D-22)

**Current** (`example/MNNExample.cpp:267-273`):

```cpp
        FileManager::GetInstance().LoadASync(
            file_names[i],
            false, // don't parse
            true,  // don't save - just load
            ioc,
```

**Post-edit target:**

```cpp
        FileManager::GetInstance().LoadASync(
            file_names[i],
            true,  // save
            ioc,
```

Target is built (`example/CMakeLists.txt:3`), so MSVC verifies it. Nothing else in the example changes (Phase 4 rewrites it).

---

## Shared Patterns

### Posted-failure — never throw across the async boundary
**Source:** `src/HTTPCommon.cpp:100` (representative of 24+ sites); applies to D-20 and every failure-tail edit.

```cpp
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::COULD_NOT_RESOLVE ), false, false ); } );
```

Every failure site's `, false, false );` tail becomes `, false );`. The D-20 guard delivers `outcome::failure(...)` to `finalcall` directly (it already runs on the io thread inside `handle_read`) — log → decrement → fail → `return`.

### Outstanding-operations ledger
**Source:** `src/FileManager.cpp` — `IncrementOutstandingOperations()` at dispatch (~line 60), `DecrementOutstandingOperations( ioc )` in EVERY completion path (~211+). D-20's guard adds the only new completion path: decrement BEFORE delivering failure or `ioc->stop()` never fires and callers hang (Pitfall 2).

### Two-tier callback contract
Tier 1 `CompletionCallback = (ioc, ResultType, parse, save)` — loses `parse` this phase. Tier 2 `FinalCallback = (ResultType)` — untouched. Never conflate: `finalcall` fires at load completion, BEFORE the auto-save write lands (D-19 latch discipline; verified live).

### Doc-comment scrub
Every affected signature carries `@param parse - Whether to parse file upon completion` — the line dies with its parameter (see `include/FileLoader.hpp:23-28` doc block for the canonical example). ~15 doc blocks across 8 headers. Consider optional gate pattern `param parse` (zero legit survivors — RESEARCH.md Open Question 1).

### Phase-end grep gate (D-23, TIGHTENED — RESEARCH.md Discovery 1)
Literal `bool parse` / `parse_` produce ~28 false positives (`bool parseHTTPUrl`, httplib's `parse_request_line` family). Use word-boundary forms:

```
bool\s+parse\b    \bparse_\b    FileParser    ParseData    RegisterParser    parsers\[    parsers\.find
```

Plus a plain-`parse` eyeball grep on `src/IPFSCommon.cpp src/IPFSLoader.cpp src/SFTPCommon.cpp src/SFTPLoader.cpp` (bare identifiers in capture lists evade the gate). Gate scope: `include/`, `src/`, `test/` (+ `example/`). Gate = grep clean + Windows build green + full ctest green (D-24).

---

## No Analog Found

| Item | Role | Data Flow | Reason |
|------|------|-----------|--------|
| D-19 RAII CWD guard | test utility | file-I/O | No in-tree CWD-swapping helper exists; synthesize per RESEARCH.md shape (ctor captures `current_path()`, swaps to `TempDir`, dtor restores). `TempFile`/`TempDir` in `test/testutil/temp_file.hpp` are the RAII style analogs. |
| D-20 failure payload (`std::errc::operation_not_supported`) | error value | event-driven | `FileManager` has no `Error` enum of its own; per research A1 use `std::make_error_code(std::errc::...)` into `outcome::failure` rather than minting category machinery. |

Everything else in this phase has an exact in-tree analog: the file itself, pre-strip.

## Metadata

**Analog search scope:** `include/`, `src/`, `test/src/`, `test/testutil/`, `example/` (all reads live, 2026-09-04, post-Phase-2 tree)
**Files scanned:** 16 primary (FileManager.hpp/.cpp, FileLoader.hpp, FileParser.hpp, LocalFileLoader.hpp/.cpp, LocalFileSaver.cpp, HTTPCommon.hpp/.cpp, WSCommon.cpp, IPFSCommon.cpp, filemanager_test.cpp, localfile_loader_test.cpp, ipfs_device_test.cpp, asio_helpers.hpp, temp_file.hpp, test_fixture.hpp, MNNExample.cpp)
**Special caveats carried into planning:**
1. **SFTP 3-of-4 files never compiled** (`src/SFTPLoader.cpp`, `include/SFTPCommon.hpp`, `src/SFTPCommon.cpp` absent from `add_library` in `src/CMakeLists.txt`) — grep gate + mechanical mirroring of WS/HTTP shapes is the ONLY verification; do not add them to the build.
2. **PARSE-02 is atomic** (base virtual + 9 aliases type-couple everything); PARSE-01 + D-20 is independently green — natural two-commit slicing.
3. Line numbers in this document were verified 2026-09-04; executors re-grep (`bool\s+parse\b`, `\bparse_\b`, `handle_read\( ioc`) rather than navigating by line (Pitfall 7).
**Pattern extraction date:** 2026-09-04
