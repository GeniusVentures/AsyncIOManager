# Phase 1: LocalFile Rename & MNN Code Purge - Pattern Map

**Mapped:** 2026-09-03
**Files analyzed:** 22 (8 renames, 4 wiring/scrub modifications, 10 doc-scrub-only files; +2 deletions)
**Analogs found:** 22 / 22 — every file in this phase is a rename/scrub of an existing file, so each analog is its own current state. The patterns below capture the exact structures that must survive the rename verbatim.

> **Nature of this phase:** it is a *rename-and-purge*, not new-feature work. The "analog" for each renamed file is the file's own current body; the planner's job is to specify **what changes** (identifiers, includes, registration lines, comments) and **what must be copied byte-for-byte** (async state machines, error handling, quirks called out in RESEARCH.md's anti-patterns list). The excerpts below mark both.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/LocalFileLoader.hpp` (← `MNNLoader.hpp`) | strategy singleton header | file-I/O | `include/MNNLoader.hpp` (self) | exact (rename) |
| `src/LocalFileLoader.cpp` (← `MNNLoader.cpp`) | strategy singleton impl | file-I/O | `src/MNNLoader.cpp` (self) | exact (rename) |
| `include/LocalFileSaver.hpp` (← `MNNSaver.hpp`) | strategy singleton header | file-I/O | `include/MNNSaver.hpp` (self) | exact (rename) |
| `src/LocalFileSaver.cpp` (← `MNNSaver.cpp`) | strategy singleton impl | file-I/O | `src/MNNSaver.cpp` (self) | exact (rename, −`"mnn"` registration) |
| `include/LocalFileCommon.hpp` (← `FILECommon.hpp`) | async device header | file-I/O (streaming) | `include/FILECommon.hpp` (self) | exact (rename, D-01/D-03) |
| `src/LocalFileCommon.cpp` (← `FILECommon.cpp`) | async device impl | file-I/O (streaming) | `src/FILECommon.cpp` (self) | exact (rename) |
| `test/src/localfile_loader_test.cpp` (← `mnn_loader_test.cpp`) | test | file-I/O | `test/src/mnn_loader_test.cpp` (self) | exact (rename) |
| `test/src/localfile_saver_test.cpp` (← `mnn_saver_test.cpp`) | test | file-I/O | `test/src/mnn_saver_test.cpp` (self) | exact (rename) |
| `src/FileManager.cpp` (modified) | registry/dispatcher (controller) | request-response (URL dispatch) | current body + `src/FileManager.cpp:118-127` throw pattern | exact (partial edit) |
| `test/src/filemanager_test.cpp` (modified + new test) | test | request-response (dispatch contract) | `SaveASync_UnregisteredPrefixThrows` at `test/src/filemanager_test.cpp:83-98` | exact (mirror) |
| `src/CMakeLists.txt` (modified) | config | n/a | current source list | exact (3-line rename) |
| `test/src/CMakeLists.txt` (modified) | config | n/a | `addtest()` blocks at lines 12-30 | exact (2-block rename + 1 comment) |
| 10 doc-scrub headers (see Scrub Inventory) | interfaces/docs | n/a | n/a — comment-only edits | n/a |

**Deletions (no analog needed):** `include/MNNCommon.hpp`, `test/base_mnn_test.hpp` — verified zero live includers (only `src/FileManager.cpp:3-4` and the two renamed .cpp files include MNN headers; `example/MNNExample.cpp:9` include is commented out).

---

## Pattern Assignments

### `include/LocalFileLoader.hpp` + `src/LocalFileLoader.cpp` (strategy singleton, file-I/O)

**Analog:** the files' own current bodies (`include/MNNLoader.hpp`, `src/MNNLoader.cpp`)

**What changes vs. what stays** — header (current: `include/MNNLoader.hpp:1-66`):

```cpp
// Lines 1-3 BEFORE:
/**
 * Header file for the MNNLoader
 */
// AFTER: "Header file for the LocalFileLoader"

// Line 10 BEFORE: #include "MNNCommon.hpp"  → DELETE ENTIRELY (MNN-02; MNNCommon.hpp
// is deleted and nothing in this header uses MNN types — verified: class body uses
// only std/memory/string/boost::asio/FileLoader)

// Line 18: class MNNLoader : public FileLoader      → class LocalFileLoader : public FileLoader
// Line 20: SINGLETON_PTR( MNNLoader );              → SINGLETON_PTR( LocalFileLoader );
// Line 64: m_logger = sgns::asiomgr::createLogger( "MNNLoader" );  → "LocalFileLoader" (D-03 string scrub)

// Doc comments to reword (scrub): lines 34, 41-43, 50 — "(for MNN)" parentheticals dropped;
// "Load Data on the MNN file" → "Load data from the file"; "@param filename - MNN file part" → generic wording;
// "@return Interpreter of MNN file" → "@return String indicating init" or similar
```

**Singleton boilerplate** — copy this structure exactly, swap names (`src/MNNLoader.cpp:24-36` verified):

```cpp
namespace sgns
{
    LocalFileLoader *LocalFileLoader::_instance = nullptr;   // was MNNLoader (line 24)

    void LocalFileLoader::InitializeSingleton()              // was MNNLoader (line 26)
    {
        if ( _instance == nullptr )
        {
            _instance = new LocalFileLoader();               // was new MNNLoader() (line 30)
        }
    }

    LocalFileLoader::LocalFileLoader()                       // was MNNLoader (line 34)
    {
        FileManager::GetInstance().RegisterLoader( "file", this );   // prefix UNCHANGED
    }
```

**Error category boilerplate** — rename both the macro arg and every case label (`src/MNNLoader.cpp:10-21` verified); messages stay identical:

```cpp
OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, LocalFileLoader::Error, e )
{
    switch ( e )
    {
        case sgns::LocalFileLoader::Error::READ_ERROR:
            return "File could not be read";
        case sgns::LocalFileLoader::Error::FILE_OPEN_FAIL:
            return "File could not be opened";
    }
    return "Unknown error";
}
```

**Core async pattern — copy verbatim except the two `FILEDevice` references** (`src/MNNLoader.cpp:57-103` verified):

```cpp
// Line 65: auto fileDevice = std::make_shared<LocalFileDevice>( ioc, filename, 0 );  // was FILEDevice
// Line 8 (includes): #include "LocalFileCommon.hpp"  // was "FILECommon.hpp"
// KEEP byte-for-byte (anti-pattern list — do NOT fix):
//   - the error.value() == 2 EOF idiom (line 73) — pre-existing, tests depend on it
//   - handle_read( ioc, outcome::failure( Error::FILE_OPEN_FAIL ), false, false ) error posting (lines 68-70)
//   - the async_read continuation capturing this/fileDevice/ioc/handle_read/parse/save/buffer/filename
```

---

### `include/LocalFileSaver.hpp` + `src/LocalFileSaver.cpp` (strategy singleton, file-I/O)

**Analog:** the files' own current bodies (`include/MNNSaver.hpp`, `src/MNNSaver.cpp`)

**Header deltas** (`include/MNNSaver.hpp:1-34` verified):
- Line 2 banner `MNNSaver.hpp` → `LocalFileSaver.hpp`
- Line 13 `class MNNSaver : public FileSaver` → `class LocalFileSaver : public FileSaver`
- Line 15 `SINGLETON_PTR( MNNSaver );` → `SINGLETON_PTR( LocalFileSaver );`
- Line 11 doc comment `/// @brief class to handle "ipfs://" prefix...` — **pre-existing copy-paste error** (says ipfs); optional fix to `"file://"` wording at planner's discretion (Claude's Discretion covers comment wording)

**The one behavioral delta of the phase — registration** (`src/MNNSaver.cpp:40-44` verified):

```cpp
// BEFORE:
    MNNSaver::MNNSaver()
    {
        FileManager::GetInstance().RegisterSaver( "file", this );
        FileManager::GetInstance().RegisterSaver( "mnn", this );   // DELETE THIS LINE (MNN-04)
    }
// AFTER:
    LocalFileSaver::LocalFileSaver()
    {
        FileManager::GetInstance().RegisterSaver( "file", this );
    }
```

**Everything else copies verbatim with name swaps only** (`src/MNNSaver.cpp`):
- Error category block (lines 16-27) — same rename recipe as loader above
- `_instance` / `InitializeSingleton()` (lines 30-38)
- `SaveFile` sync path with null-data throw (lines 46-60) — keep the unqualified `range_error`/`string`/`ofstream` (relies on inherited `using namespace std;` from `FileSaver.hpp` — anti-pattern list says don't fix)
- `SaveASync` (lines 62-129): keep the MSVC null-data check `data.value()->second.data() == nullptr` (line 64), the empty-filename UUID fallback (lines 68-71), the `save_location` out-param report (lines 74-77), the `std::make_shared<size_t> remainingWrites` counter, the dead `std::ofstream file(...)` at line 98 (anti-pattern: pre-existing, out of scope), and line 96: `std::make_shared<LocalFileDevice>( ioc, directoryWithFile, 1 )` (was `FILEDevice`)
- Line 14 include: `#include "LocalFileCommon.hpp"` (was `"FILECommon.hpp"`)

---

### `include/LocalFileCommon.hpp` + `src/LocalFileCommon.cpp` (async device, streaming file-I/O)

**Analog:** the files' own current bodies (`include/FILECommon.hpp`, `src/FILECommon.cpp`)

**Class rename touches every declaration site in BOTH platform branches** (D-01) — `include/FILECommon.hpp` verified touchpoints: banner (line 2), `class FILEDevice : public std::enable_shared_from_this<FILEDevice>` ×2 (lines 30, 59), ctor decls ×2 (lines 39, 68), logger tag `createLogger( "FILECommon" )` ×2 (lines 53, 77) → `"LocalFileCommon"` (D-03). `src/FILECommon.cpp` verified: banner (line 2), `#include "LocalFileCommon.hpp"` (line 4), ctor definitions + `Open()` definitions in both `#ifndef _WIN32` branches (lines 10-19, 40-53).

```cpp
// POSIX branch (include/FILECommon.hpp:30-56) — structure preserved exactly:
#ifndef _WIN32
    class LocalFileDevice : public std::enable_shared_from_this<LocalFileDevice>   // was FILEDevice
    {
    public:
        LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode );
        ~LocalFileDevice() { file_.close( ec_ ); close( fd_ ); }
        boost::system::error_code Open();
        boost::asio::posix::stream_descriptor &getFile() { return file_; }
    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "LocalFileCommon" );  // D-03
        ...members unchanged...
    };
#else
    class LocalFileDevice : public std::enable_shared_from_this<LocalFileDevice>   // was FILEDevice
    { ... Windows branch: stream_file, same rename points ... };
#endif
```

**Do NOT split the two branches into separate files** — the `#ifndef _WIN32` dual-class structure stays in one header/one cpp this phase (D-02 reserves the split for Phase 2). Platform `#ifdef`s in this file are exempt from the boss's no-`#ifdef` rule until Phase 2.

**Open() error handling — copy verbatim** (`src/FILECommon.cpp:21-38, 55-73`): POSIX `::open`/`errno`/`file_.assign` flow and Windows `stream_file::flags` mode-mapping flow, including the string-concatenation logger calls (pre-existing outlier; rename only).

---

### `src/FileManager.cpp` (registry/dispatcher, request-response)

**Analog:** current body — three targeted edits, nothing else.

**Edit 1 — includes** (lines 3-4):

```cpp
#include "LocalFileLoader.hpp"   // was "MNNLoader.hpp"
#include "LocalFileSaver.hpp"    // was "MNNSaver.hpp"
```

**Edit 2 — InitializeSingletons** (lines 31-42); exact final body per RESEARCH.md Pitfall 5:

```cpp
void FileManager::InitializeSingletons()
{
    sgns::LocalFileLoader::InitializeSingleton();     // was sgns::MNNLoader
    //sgns::MNNParser::InitializeSingleton();         // DELETED (contains mnn; blocks zero-grep)
    //sgns::SFTPLoader::InitializeSingleton();        // stays
    sgns::HTTPLoader::InitializeSingleton();
    //sgns::WSLoader::InitializeSingleton();          // stays
    sgns::IPFSLoader::InitializeSingleton();
    sgns::IPFSSaver::InitializeSingleton();
    sgns::LocalFileSaver::InitializeSingleton();      // was sgns::MNNSaver
    sgns::SFTPSaver::InitializeSingleton();
}
```

**Edit 3 — dead parse block deletion** (D-04, lines ~74-79 inside `handle_read` lambda):

```cpp
// BEFORE (verified src/FileManager.cpp:71-79):
        if ( buffers )
        {
            //Parse Data
            if ( parse )
            {
                auto parserIter = parsers.find( "mnn" );
                auto parser     = dynamic_cast<FileParser *>( parserIter->second );
                //shared_ptr<void> data = parser->ParseASync(buffer);
            }
            //Save data or otherwise decrement counter of operations
            if ( save ) {

// AFTER — the if(parse) block AND its "//Parse Data" comment are gone entirely;
// the `parse` lambda param becomes unused (harmless — precedent: AsyncHandler at
// src/FileManager.cpp:26 already ships unused params):
        if ( buffers )
        {
            //Save data or otherwise decrement counter of operations
            if ( save ) {
```

**Boundary guard (D-05):** the `parse` bool parameter, `include/FileParser.hpp`, the `parsers` map, `RegisterParser`, and `ParseData` all stay untouched until Phase 3.

**Throw pattern to mirror in the new test** (lines 118-127 — prefix lookup precedes async work, so `SaveASync` throws synchronously):

```cpp
    auto saverIter = savers.find( prefix );
    if ( saverIter == savers.end() )
    {
        throw std::range_error( "No saver registered for prefix " + prefix );
    }
```

---

### `test/src/localfile_loader_test.cpp` (← `mnn_loader_test.cpp`) (test, file-I/O)

**Analog:** the file's own current body — pure rename, **same behaviors** (TEST-01: sync load happy, nonexistent-file throw, async load happy, async nonexistent returns error).

Rename map for the file: header comment line 2 (`MNNLoader` → `LocalFileLoader`, drop `"file://" prefix`? no — keep prefix wording), fixture `class MNNLoaderTest : public FileManagerTestFixture` (line 12) → `LocalFileLoaderTest`, and all five `TEST_F( MNNLoaderTest, ... )` first args (lines 20, 36, 46, 83).

**Async test skeleton — copy structure verbatim** (lines 46-79):

```cpp
TEST_F( LocalFileLoaderTest, LoadASync_ReadsExistingFile )
{
    const std::string expected = "async file content";
    TempFile          tf( expected );
    IOContextRunner   runner;

    bool                                   completed = false;
    std::optional<FileManager::ResultType> received;

    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(), false, false, runner.ioc(),
        [&]( FileManager::ResultType result ) { received = std::move( result ); completed = true; },
        "" );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for async load";
    // ... assertions unchanged ...
}
```

---

### `test/src/localfile_saver_test.cpp` (← `mnn_saver_test.cpp`) (test, file-I/O)

**Analog:** the file's own current body — pure rename, same behaviors (sync save, null-data throw, async single-file with `save_location` verification, async multi-file with subdirs, async null-data throw). Keep `using namespace sgns;` (line 11) and the trailing NOTE comment about the removed test (lines 137-138, itself a pattern: document why coverage was dropped).

Renames: header line 2 (also drop `and "mnn://"` from the comment — the mnn:// coverage moves to `filemanager_test` per D-06), fixture `MNNSaverTest` → `LocalFileSaverTest` (line 15), all `TEST_F` first args (lines 23, 44, 54, 95, 133), comment line 67 (`MNNSaver appends` → `LocalFileSaver appends`).

**Header comment shape** (Claude's Discretion on exact wording):

```cpp
/**
 * Tests for LocalFileSaver — local file saving via "file://" prefix.
 */
```

---

### `test/src/filemanager_test.cpp` — modified + NEW rejection test (test, dispatch contract)

**Analog for the new test:** `SaveASync_UnregisteredPrefixThrows` (`test/src/filemanager_test.cpp:83-98` verified) — D-07 requires mirroring this exact shape with the `mnn` prefix:

```cpp
TEST_F( FileManagerIntegrationTest, SaveASync_MnnPrefixThrows )
{
    IOContextRunner runner;
    auto            data = makeSingleFileResult( "test.bin", "content" );

    EXPECT_THROW(
        {
            FileManager::GetInstance().SaveASync(
                "mnn://some/path", data, runner.ioc(), []( FileManager::ResultType ) {} );
        },
        std::range_error );   // "No saver registered for prefix mnn" — synchronous throw, no polling
}
```

**Rename edits in the existing body:** test names at lines 40 (`...DispatchesToMNNLoader` → `...ToLocalFileLoader`), 52 and 122 (`...DispatchesToMNNSaver` → `...ToLocalFileSaver`), comment at line 133 (`MNNSaver appends` → `LocalFileSaver appends`). Place the new test in the "Dispatch tests — unhappy paths" section, directly after `SaveASync_UnregisteredPrefixThrows` (line 98) — it is that test's sibling.

---

### `src/CMakeLists.txt` (config)

**Analog:** current source list (lines 2-15). Three entry swaps, alphabetical position preserved:

```cmake
add_library(AsyncIOManager STATIC
    LocalFileCommon.cpp      # was FILECommon.cpp
    FileManager.cpp
    ...
    LocalFileLoader.cpp      # was MNNLoader.cpp
    LocalFileSaver.cpp       # was MNNSaver.cpp
    ...
```

**Guard:** NO MNN CMake purge — `find_package(MNN)` etc. lives in root `CMakeLists.txt`/`build/CommonBuildParameters.cmake` and stays until Phase 4.

### `test/src/CMakeLists.txt` (config)

**Analog:** the `addtest()` blocks (lines 12-30) — each test is a **3-part rename** (Pitfall 6): .cpp filename, `addtest(name …)` target, and its `target_link_libraries(name …)`:

```cmake
addtest(localfile_loader_test        # was mnn_loader_test
    localfile_loader_test.cpp        # was mnn_loader_test.cpp
)
target_link_libraries(localfile_loader_test   # was mnn_loader_test
    spdlog::spdlog
    protobuf::libprotobuf
    AsyncIOManager
    asiomgr_testutil
)
# ...identical recipe for localfile_saver_test (was mnn_saver_test)...
```

Plus line 196 comment scrub: `same conformance drift as example/MNNExample.cpp` → reword to `the example app` (sits inside the grep scope).

---

## Shared Patterns

### Singleton self-registration (applies to: both renamed loader/saver pairs)

**Source:** `src/MNNLoader.cpp:24-36`, `src/MNNSaver.cpp:30-44`; macro at `include/ASIOSingleton.hpp`
The contract: `SINGLETON_PTR( C )` in the class body → `C *C::_instance = nullptr;` in the .cpp (note: existing files do NOT use `SINGLETON_PTR_INIT`) → idempotent `InitializeSingleton()` with the null guard → ctor self-registers. `FileManagerTestFixture::SetUp()` calls `InitializeSingletons()` before every test, so re-registration is impossible; but **leaving the `"mnn"` line in `LocalFileSaver`'s ctor makes the rejection test fail with a successful save instead of a throw** (Pitfall 5 — the warning sign).

### Error categories (applies to: both renamed .cpp files)

**Source:** `src/MNNLoader.cpp:10-21`, `src/MNNSaver.cpp:16-27`
Rename recipe: `OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, X::Error, e )` macro arg + both `case sgns::X::Error::` labels. Message strings stay identical (asserted nowhere; minimizing diff noise). Miss either the macro arg or a case label → link errors mentioning the old category (Pitfall 4).

### Logger tags — string-level, invisible to the compiler (applies to: all renamed files)

**Source:** `include/MNNLoader.hpp:64`, `include/MNNSaver.hpp` (no logger — saver header has none), `include/FILECommon.hpp:53,77`, `src/FILECommon.cpp` (uses member from header)
`createLogger("MNNLoader")` → `"LocalFileLoader"`, `createLogger("FILECommon")` → `"LocalFileCommon"` (×2, both platform branches). Only the scoped grep catches these — the compiler cannot (Pitfall 4).

### Async test harness (applies to: both renamed test files + new rejection test)

**Source:** `test/testutil/test_fixture.hpp`, `test/testutil/asio_helpers.hpp`
Derive from `FileManagerTestFixture`; use `makeSingleFileResult`/`makeMultiFileResult` for save payloads; `IOContextRunner runner;` + `pollUntil( [&]{ return completed; }, std::chrono::seconds(5) )` + `ASSERT_TRUE(ok) << "Timed out..."` for every async path. Exception: the rejection test needs **no polling** — `SaveASync` throws synchronously before any async work begins.

### Registry rejection contract (applies to: the new test)

**Source:** `src/FileManager.cpp:118-127`
Unknown saver prefix → synchronous `std::range_error("No saver registered for prefix " + prefix)`. After MNN-04, `mnn` is such a prefix. Assert with `EXPECT_THROW(..., std::range_error)` exactly as the three existing `*UnregisteredPrefixThrows` tests do (`filemanager_test.cpp:62-98`).

---

## Doc-Scrub Inventory (MNN-05) — comment-only edits in surviving files

Verified this session by case-insensitive grep; line numbers exact. Suggested wording: drop `(for MNN)` parentheticals entirely (the `parse` param itself survives until Phase 3 — D-05); "Load Data on the MNN file" → "Load data from the file".

| File:Line | Current text (fragment) | Action |
|-----------|------------------------|--------|
| `include/FileLoader.hpp:25,41` | `(for MNN)` | drop parenthetical |
| `include/FileLoader.hpp:35` | `'ipfs://testme.mnn'` | → `'ipfs://testfile.bin'` |
| `include/FileManager.hpp:67,90,94,101` | `(for MNN)` ×2; `".mnn", ".jpg"`; `"mnn", "file"` … `mnn://xxxxx` | → `".bin", ".jpg"`; `"https", "file"` … `https://xxxxx` |
| `include/HTTPCommon.hpp:53,64` | `(for MNN)` / `(for MNN currently)` | drop parenthetical |
| `include/HTTPLoader.hpp:33,40-42,48` | "Load Data on the MNN file" / "MNN file part" / "Interpreter of MNN file" | generic wording |
| `include/IPFSCommon.hpp:66,112,137` | `(for MNN...)` ×3 | drop parenthetical |
| `include/IPFSCommon.hpp:210` | **`QmNnooDu7...` bootstrap CID (commented)** | **false positive** — base58 `mNn`, not an MNN ref; delete the commented line (or whole dead list, lines 209-216); zero behavior risk |
| `include/IPFSLoader.hpp:56,63-65,72` | same trio as HTTPLoader | generic wording |
| `include/SFTPCommon.hpp:39,64` | `(for MNN...)` | drop parenthetical |
| `include/SFTPLoader.hpp:29,36-38,45` | same trio | generic wording |
| `include/WSCommon.hpp:51,62` | `(for MNN...)` | drop parenthetical |
| `include/WSLoader.hpp:16,35,42-44,51` | "parsing the information in an MNN model file" + trio | generic wording |

(`src/FileManager.cpp` and `test/src/CMakeLists.txt` scrubs are covered in their Pattern Assignments above.)

**Grep contract (Pitfall 1):** criterion 5 must be verified as `git grep -iI mnn -- include src test ":(exclude)test/src/filemanager_test.cpp"` → empty; unexcluded `git grep -iI mnn -- include src test` → hits ONLY in `test/src/filemanager_test.cpp` (the deliberate rejection-test strings). The binary fixtures `test/1.mnn`/`test/2.mnn` are skipped by `-I`. **Warning signs:** an executor "cleans" the grep by deleting the rejection test or renaming it to avoid the string — both violate criterion 1/D-06.

## No Analog Found

None. Every file in this phase has an exact analog (its own current state or a directly verified sibling). No new patterns are introduced; RESEARCH.md fallback not needed.

## Metadata

**Analog search scope:** `include/`, `src/`, `test/src/`, `test/testutil/` — full read of all 6 renamed file pairs, `FileManager.cpp`, all 3 affected test files, both CMakeLists, `test_fixture.hpp`; case-insensitive grep over `include/`, `src/`, `test/src/` for the scrub inventory.
**Files scanned:** 19 read in full/targeted + grep across 24 files
**Pattern extraction date:** 2026-09-03
**Key verification traps carried into patterns:** CID false-positive (`IPFSCommon.hpp:210`), synchronous-throw rejection test, `TESTING` vs `BUILD_TESTING` build gate (wrapper `-DTESTING=ON` is the proven path), 3-part test rename in CMake, string-level logger tags.
