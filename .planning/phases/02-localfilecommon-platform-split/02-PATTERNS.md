# Phase 2: LocalFileCommon Platform Split - Pattern Map

**Mapped:** 2026-09-03
**Files analyzed:** 9 (5 new, 3 modified, 1 deleted)
**Analogs found:** 8 / 8 files with in-repo precedent (the split is self-analogous — the current dual-branch files ARE the move source)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/LocalFileCommon.hpp` (modified → neutral umbrella) | header (device dispatch) | file-I/O (declarations) | itself, lines 1-26 (neutral prefix) + RESEARCH Pattern 1 target shape | exact (self) |
| `include/LocalFileCommon.win.hpp` (new) | header (device) | file-I/O | `include/LocalFileCommon.hpp` lines 65-110 (`#else` Windows class block) | exact (verbatim move source) |
| `include/LocalFileCommon.posix.hpp` (new) | header (device) | file-I/O | `include/LocalFileCommon.hpp` lines 27-64 (POSIX class block) | exact (move + D-12/D-13) |
| `src/LocalFileCommon.win.cpp` (new) | source (device impl) | file-I/O | `src/LocalFileCommon.cpp` lines 37-63 (`#else` Windows impl block) | exact (verbatim move source) |
| `src/LocalFileCommon.posix.cpp` (new) | source (device impl) | file-I/O | `src/LocalFileCommon.cpp` lines 10-36 (POSIX impl block) | exact (move + D-12/D-13) |
| `src/LocalFileCommon.cpp` (deleted) | — | — | — (deletion; discretion prefers outright `git rm`) | n/a |
| `src/CMakeLists.txt` (modified) | build config | compile-time platform selection | `src/CMakeLists.txt` lines 1-15 (own `add_library` list) + `test/src/CMakeLists.txt:83-85` (quoted `target_compile_definitions`) | role-match (no prior `if(WIN32)` source selection exists — first of its kind, but both halves of the pattern exist separately) |
| `test/src/no_ifdef_test.cpp` (new) | test (static-guard) | file-I/O (text scan) | `test/src/localfile_loader_test.cpp` (gtest include + TEST shape) | role-match (guard test is simpler: bare `TEST`, no fixture, no library link) |
| `test/src/CMakeLists.txt` (modified) | test config | — | own `localfile_loader_test` block (lines 19-27) + `TEST_CERT_DIR` define (lines 83-85) | exact |

**Data-flow note:** the whole phase is file-I/O at the device layer. The only novel data flow is `no_ifdef_test`'s read-sources-as-text scan — no precedent test does this, but the path-injection mechanic (`TEST_CERT_DIR`) and registration (`addtest()`) both exist verbatim.

## Pattern Assignments

### `include/LocalFileCommon.hpp` → neutral umbrella (header, dispatch)

**Analog:** itself — the platform-neutral prefix (lines 1-26) survives intact; the two class blocks and all three platform branches (lines 27, 65, 111) die.

**What stays verbatim** (current lines 1-26):

```cpp
/**
 * Header file for the LocalFileCommon
 */
#pragma once
#include <iostream>
#include <memory>
#include "boost/asio.hpp"
#include <asiomgr-logger.hpp>
#include <libp2p/outcome/outcome.hpp>

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    using namespace boost::asio;
```

**What gets added** (selector, per D-08/D-10/D-15) — placed AFTER the `namespace sgns { using namespace boost::asio; }` block is closed, outside any namespace; each platform header re-opens `namespace sgns` itself:

```cpp
// Platform selection is injected by CMake (src/CMakeLists.txt):
//   WIN32 -> ASIOMGR_LOCALFILE_HEADER="LocalFileCommon.win.hpp"
//   UNIX  -> ASIOMGR_LOCALFILE_HEADER="LocalFileCommon.posix.hpp"
#include ASIOMGR_LOCALFILE_HEADER
```

**Critical mechanics** (verified in RESEARCH Pattern 1 + Pitfall 1/7):
- The POSIX-only includes (`<fcntl.h>`, `"boost/asio/posix/stream_descriptor.hpp"`, current lines 5-7) move OUT of the umbrella INTO `LocalFileCommon.posix.hpp` — the umbrella must not include them.
- The umbrella's `namespace sgns { using namespace boost::asio; }` block stays (D-15); it is legal for it to coexist with the same using-directive repeated in each platform `.cpp` (Pitfall 7 — the current `src/LocalFileCommon.cpp` line 7 already duplicates it).
- `#pragma once` stays (current guard style; NOT an `#ifdef` violation per D-17).
- Optional `#error` guard: if added, use `#if !defined( ASIOMGR_LOCALFILE_HEADER )` — NOT `#ifndef`, which would match the D-17 scan pattern `#ifndef _WIN32` only for `_WIN32` but `#ifdef`/`#ifndef` generally is the first banned pattern; `#if !defined(...)` matches none of the three banned forms.

---

### `include/LocalFileCommon.win.hpp` (header, device — Windows half)

**Analog:** `include/LocalFileCommon.hpp` lines 65-110 — the `#else` Windows class block, moved verbatim (D-11: byte-identical behavior, zero "improvements").

**Move source** (current lines 65-110, branch markers stripped):

```cpp
    class LocalFileDevice : public std::enable_shared_from_this<LocalFileDevice>
    {
    public:
        /**
         * Create a FILE Device to load a file from local.
         * @param ioc - Boost asio io_context to use
         * @param filename - Path to location of file to load
         * @param writemode - 0 for read, 1 for write
         */
        LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode );

        ~LocalFileDevice()
        {
            // Cleanup
            file_.close();
        }

        boost::system::error_code Open();

        /**
         * Get the current file pointer for async operations
         */
        boost::asio::stream_file &getFile()
        {
            return file_;
        }

    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "LocalFileCommon" );
        //Common vars used for file loading
        boost::asio::stream_file  file_;
        boost::system::error_code ec_;
        std::string               filename_;
        int                       flags_;
    };
```

**File skeleton** — wrap the moved class in the standard device-header shape (copy from `include/HTTPCommon.hpp` lines 1-26: banner → `#pragma once` → includes → outcome re-export → `namespace sgns` + `using namespace boost::asio` → class):

```cpp
/**
 * Header file for the LocalFileCommon (Windows)
 */
#pragma once
#include <iostream>
#include <memory>
#include "boost/asio.hpp"
#include <asiomgr-logger.hpp>
#include <libp2p/outcome/outcome.hpp>

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    using namespace boost::asio;

    // ... class LocalFileDevice (moved block above) ...

} // End namespace sgns
```

Notes:
- D-16: fully self-contained declaration — platform `getFile()` return type (`boost::asio::stream_file&`) and private members (NO `fd_` on Windows) all live here.
- D-14: logger tag stays exactly `"LocalFileCommon"`.
- Keep the class doc comment currently at umbrella lines 23-26 ("This class creates a FILE Device...") — move it onto the class in each platform header, reworded per platform or kept as-is.

---

### `include/LocalFileCommon.posix.hpp` (header, device — POSIX half)

**Analog:** `include/LocalFileCommon.hpp` lines 5-7 (POSIX includes) + lines 27-64 (POSIX class block).

**POSIX-only includes to carry over** (current lines 5-7 — these were already correctly gated to POSIX; they become unconditional in the POSIX-only file):

```cpp
#include <fcntl.h>
#include "boost/asio/posix/stream_descriptor.hpp"
```

**Move source with D-13 fix marked** (current lines 28-64):

```cpp
    class LocalFileDevice : public std::enable_shared_from_this<LocalFileDevice>
    {
    public:
        /**
         * Create a FILE Device to load a file from local. 
         * @param ioc - Boost asio io_context to use
         * @param filename - Path to location of file to load
         * @param writemode - 0 for read, 1 for write
         */
        LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode );

        ~LocalFileDevice()
        {
            // Cleanup
            file_.close( ec_ );
            // D-13: `close( fd_ );` REMOVED — stream_descriptor::close() already
            // closes the underlying descriptor (Boost 1.85 basic_descriptor.hpp:
            // "even if the function indicates an error, the underlying descriptor
            // is closed"); the second ::close(fd_) is a latent EBADF/double-close.
        }

        boost::system::error_code Open();

        /**
         * Get the current file pointer for async operations
         */
        boost::asio::posix::stream_descriptor &getFile()
        {
            return file_;
        }

    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "LocalFileCommon" );
        //Common vars used for file loading
        boost::asio::posix::stream_descriptor file_;
        boost::system::error_code             ec_;
        std::string                           filename_;
        int                                   flags_;
        int                                   fd_ = -1;   // stays: used by Open()'s ::open/assign
    };
```

Same file skeleton as the win half (banner "…(POSIX)", `#pragma once`, neutral includes + the two POSIX includes, outcome re-export, `namespace sgns`, class, closing comment).

---

### `src/LocalFileCommon.win.cpp` (source, device impl — Windows half)

**Analog:** `src/LocalFileCommon.cpp` lines 1-7 (banner/include/using) + lines 37-63 (`#else` Windows impl).

**File skeleton + move source** (verbatim per D-11):

```cpp
/**
 * Source file for the LocalFileCommon (Windows)
 */
#include "LocalFileCommon.hpp"

namespace sgns
{
    using namespace boost::asio;

    LocalFileDevice::LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode ) :
        file_( *ioc ), filename_( filename ), flags_( writemode )
    {
    }

    boost::system::error_code LocalFileDevice::Open()
    {
        // Windows-specific: Use Boost.Asio stream_file with mode based on flags_
        boost::asio::stream_file::flags mode = ( flags_ == 0 ) ? boost::asio::stream_file::read_only
                                                               : boost::asio::stream_file::write_only;
        // Add create if writing
        if ( flags_ == 1 )
        {
            mode |= boost::asio::stream_file::create;
        }

        file_.open( filename_, mode, ec_ );
        if ( ec_ )
        {
            m_logger->error( "Failed to open file (Windows): " + ec_.message() );
            return ec_;
        }
        m_logger->debug( "File opened successfully (Windows)" );
        return ec_; // Success: ec_.value() == 0
    }

} // End namespace sgns
```

Notes:
- `#include "LocalFileCommon.hpp"` resolves via `include_directories(../include)` (`src/CMakeLists.txt:47`); the umbrella then macro-expands to `"LocalFileCommon.win.hpp"` which resolves in the same `include/` dir (quoted-include searches the includer's directory first).
- Keep the string-concatenation `m_logger->error( "...: " + ec_.message() )` as-is (D-11 fidelity — this file's concat style is the pre-existing outlier; do NOT modernize to `{}` placeholders in this phase).
- Do NOT add `stream_file::truncate` (Pitfall 3: Windows `create` maps to `OPEN_ALWAYS` which does not truncate; the observable truncate comes from the stray `std::ofstream` in `src/LocalFileSaver.cpp:94`, which stays untouched).
- Note the flags asymmetry: Windows stores raw `writemode` in `flags_` and translates inside `Open()` — preserve verbatim, do not unify with POSIX.

---

### `src/LocalFileCommon.posix.cpp` (source, device impl — POSIX half)

**Analog:** `src/LocalFileCommon.cpp` lines 1-7 + lines 10-36 (POSIX impl).

**Move source with the two locked fixes** (D-12 marked in ctor; D-13 was header-side):

```cpp
/**
 * Source file for the LocalFileCommon (POSIX)
 */
#include "LocalFileCommon.hpp"

namespace sgns
{
    using namespace boost::asio;

    LocalFileDevice::LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode ) :
        file_( *ioc ), filename_( filename ),
        flags_( writemode == 0 ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC )  // D-12: added | O_TRUNC
    {
    }

    boost::system::error_code LocalFileDevice::Open()
    {
        // POSIX-specific: Use file descriptor with ::open
        fd_ = ::open( filename_.c_str(),
                      flags_,
                      0666 ); // 0666 for creation mode (rw-rw-rw); adjust permissions if needed
        if ( fd_ == -1 )
        {
            ec_ = boost::system::error_code( errno, boost::system::system_category() );
            m_logger->error( "Failed to open file (POSIX): " + ec_.message() );
            return ec_;
        }
        file_.assign( fd_, ec_ );
        if ( ec_ )
        {
            m_logger->error( "Failed to assign file descriptor: " + ec_.message() );
            ::close( fd_ );   // this ::close STAYS — it is the error-cleanup path, not the destructor
            fd_ = -1;
            return ec_;
        }
        m_logger->debug( "File opened successfully (POSIX)" );
        return ec_; // Success: ec_.value() == 0
    }

} // End namespace sgns
```

Notes:
- D-12: `O_TRUNC` requires `<fcntl.h>` — already carried into `LocalFileCommon.posix.hpp` (RESEARCH verified Boost 1.85 `file_base.hpp` maps `truncate = O_TRUNC`; POSIX `open(2)` without it does not truncate).
- D-13 nuance: the `::close( fd_ )` inside `Open()`'s assign-error path is NOT the double-close — it stays. Only the destructor's extra close dies.
- POSIX stores translated POSIX open flags in `flags_` (asymmetry with Windows preserved per D-11).

---

### `src/LocalFileCommon.cpp` (deleted)

**Analog:** none needed — discretion decision: outright deletion (`git rm src/LocalFileCommon.cpp`) preferred over comment stub. Nothing shared remains to compile once the two halves exist. Pitfall 5: the deletion must be atomic with the `src/CMakeLists.txt` source-list replacement or configure fails with "Cannot find source file".

---

### `src/CMakeLists.txt` (modified — platform selection + compile definition)

**Analogs:** own `add_library` block (lines 1-15) for where the selection lands; `test/src/CMakeLists.txt:83-85` for the quoted-definition syntax.

**Current shape to modify** (lines 1-15):

```cmake
add_library(AsyncIOManager STATIC
    LocalFileCommon.cpp          # ← this entry is REPLACED by ${ASIOMGR_LOCALFILE_SOURCE}
    FileManager.cpp
    HTTPCommon.cpp
    ...
```

**Selection + definition block** (insert before `add_library`; per PLAT-03 CMake is the ONLY selection mechanism):

```cmake
# Platform selection for the local-file device layer (PLAT-03):
# CMake is the ONLY mechanism — zero platform branching in sources.
if(WIN32)
    set(ASIOMGR_LOCALFILE_SOURCE LocalFileCommon.win.cpp)
    set(ASIOMGR_LOCALFILE_HEADER_NAME "LocalFileCommon.win.hpp")
elseif(UNIX)
    set(ASIOMGR_LOCALFILE_SOURCE LocalFileCommon.posix.cpp)
    set(ASIOMGR_LOCALFILE_HEADER_NAME "LocalFileCommon.posix.hpp")
endif()

add_library(AsyncIOManager STATIC
    ${ASIOMGR_LOCALFILE_SOURCE}
    FileManager.cpp
    ...
)

# Full header token WITH literal quotes — umbrella's `#include ASIOMGR_LOCALFILE_HEADER`
# expands to #include "LocalFileCommon.win.hpp". PRIVATE suffices: the only TUs
# including the umbrella are library sources (verified: LocalFileLoader.cpp:8,
# LocalFileSaver.cpp:14, and the two new platform .cpp files — no test/example TU does).
target_compile_definitions(AsyncIOManager PRIVATE
    ASIOMGR_LOCALFILE_HEADER="${ASIOMGR_LOCALFILE_HEADER_NAME}"
)
```

**Quoting pattern to copy exactly** (`test/src/CMakeLists.txt:83-85` — proven on this MSVC toolchain):

```cmake
target_compile_definitions(http_loader_test PRIVATE
    TEST_CERT_DIR="${TEST_CERT_DIR}"
)
```

Notes:
- Quotes literal inside the CMake string, no `\"` backslash escaping (Pitfall 1).
- Keep PRIVATE (D-10). Making it PUBLIC would export a Windows-specific header name into `AsyncIOManagerTargets.cmake`. Known limitation (RESEARCH Q1): installed-package consumers compiling the umbrella must define the macro themselves — record in SUMMARY, do not change the decision.
- Placement of `target_compile_definitions` after `add_library`, anywhere before the `BUILD_TESTING` block (lines 49-51, untouched).

---

### `test/src/no_ifdef_test.cpp` (new — zero-#ifdef guard test)

**Analog:** `test/src/localfile_loader_test.cpp` for file shape (gtest include, banner comment, TEST macro style) — but SIMPLER: bare `TEST` (no fixture), no `FileManager.hpp` include, no library link.

**Style to copy** (from `localfile_loader_test.cpp` lines 1-15):

```cpp
/**
 * Tests for LocalFileLoader — local file loading via "file://" prefix.
 */

#include <gtest/gtest.h>
...
```

**Core scan pattern** (sketch; details at planner's discretion per CONTEXT):

```cpp
/**
 * no_ifdef_test — permanent guard: the local-file layer must contain zero
 * platform branching (boss requirement). Fails on #ifdef, #ifndef _WIN32,
 * or #if defined(_WIN32) in the five local-file files. #pragma once and
 * own-name include guards are NOT violations (D-17).
 */

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static bool containsIfdefViolation( const std::string &path )
{
    std::ifstream ifs( path );
    if ( !ifs.is_open() )
    {
        ADD_FAILURE() << "cannot open " << path;
        return true;
    }
    std::string line;
    while ( std::getline( ifs, line ) )
    {
        // strip leading whitespace, then match exactly the three D-17 patterns
        // (own-name guards like #ifndef LOCALFILECOMMON_WIN_HPP do not match
        //  "#ifndef _WIN32"; #pragma once matches nothing)
        ...
    }
    return false;
}

TEST( NoIfdefTest, LocalFileLayerHasNoPlatformBranching )
{
    const std::vector<std::string> files = {
        std::string( LOCALFILE_INCLUDE_DIR ) + "/LocalFileCommon.hpp",
        std::string( LOCALFILE_INCLUDE_DIR ) + "/LocalFileCommon.win.hpp",
        std::string( LOCALFILE_INCLUDE_DIR ) + "/LocalFileCommon.posix.hpp",
        std::string( LOCALFILE_SRC_DIR ) + "/LocalFileCommon.win.cpp",
        std::string( LOCALFILE_SRC_DIR ) + "/LocalFileCommon.posix.cpp",
    };
    for ( const auto &f : files )
    {
        EXPECT_FALSE( containsIfdefViolation( f ) ) << f;
    }
}
```

Notes:
- `addtest()` links `GTest::gtest_main` (cmake/functions.cmake:13-16) — bare `TEST` without main works.
- Match ONLY the three D-17 patterns after whitespace-strip: `#ifdef <any>`, `#ifndef _WIN32`, `#if defined(_WIN32)` (Pitfall 4).
- Scan scope is exactly the five files (D-18) — other devices' `#ifdef`s are out of scope and must not be flagged.
- Use `#pragma once` in the new headers (current style) so own-name include guards never even arise.

---

### `test/src/CMakeLists.txt` (modified — register the guard test)

**Analog:** own file-based test blocks (lines 19-27) + `TEST_CERT_DIR` path-define (lines 83-85).

**Registration block to copy** (mirror the `localfile_loader_test` shape, lines 19-27):

```cmake
addtest(localfile_loader_test
    localfile_loader_test.cpp
)
target_link_libraries(localfile_loader_test
    spdlog::spdlog 
    protobuf::libprotobuf
    AsyncIOManager
    asiomgr_testutil
)
```

**New block** (place in the "Phase 2: File-based tests" section alongside the other always-built tests; simplest legal form needs NO library links — it only reads text files):

```cmake
addtest(no_ifdef_test
    no_ifdef_test.cpp
)
# Absolute source-dir paths injected at compile time (TEST_CERT_DIR pattern);
# CMake keeps forward slashes on Windows — safe for std::ifstream from test_bin/Debug/.
target_compile_definitions(no_ifdef_test PRIVATE
    LOCALFILE_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    LOCALFILE_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../src"
)
```

Notes:
- Do NOT gate behind `ASYNC_IO_MANAGER_NETWORK_TESTS` (line 47) — the guard is a file-based test, always built.
- `CMAKE_CURRENT_SOURCE_DIR` = `<repo>/test/src`, so `../../include` / `../../src` reach the repo roots.

---

## Shared Patterns

### Device-header skeleton (banner → pragma once → includes → outcome re-export → namespace)

**Source:** `include/HTTPCommon.hpp` lines 1-26 (canonical cross-protocol shape); identical block in current `include/LocalFileCommon.hpp` lines 1-26.

**Apply to:** `include/LocalFileCommon.hpp` (retain), `LocalFileCommon.win.hpp`, `LocalFileCommon.posix.hpp` (new files wrap their moved class in this skeleton).

```cpp
/**
 * Header file for the <NAME>
 */
#pragma once
#include <iostream>
#include <memory>
#include "boost/asio.hpp"
#include <asiomgr-logger.hpp>
#include <libp2p/outcome/outcome.hpp>

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    using namespace boost::asio;
```

Every header using outcome re-exports it this way (repo-wide convention). Close with `} // End namespace sgns` per CONVENTIONS (note: current LocalFileCommon.hpp lacks the closing comment — the new files should add it; that is cosmetic, not behavior).

### CMake quoted compile definition (path/string injection)

**Source:** `test/src/CMakeLists.txt:83-85`.

**Apply to:** `src/CMakeLists.txt` (`ASIOMGR_LOCALFILE_HEADER="..."`) and `test/src/CMakeLists.txt` (`LOCALFILE_INCLUDE_DIR`/`LOCALFILE_SRC_DIR`).

```cmake
target_compile_definitions(<target> PRIVATE
    <NAME>="${<value>}"
)
```

Quotes literal, no backslash escaping — the one proven-on-MSVC form in this repo.

### Test registration via addtest()

**Source:** `cmake/functions.cmake` lines 6-33.

**Apply to:** `no_ifdef_test` (and any future test). The function wires `GTest::gtest_main` + `gmock_main`, xunit XML output to `${CMAKE_BINARY_DIR}/xunit/`, `add_test`, binary placement in `${CMAKE_BINARY_DIR}/test_bin`, and clang-tidy disable. Never hand-roll a test block.

### Phase-end grep gate (one-shot verification seal)

**Source:** `.planning/phases/01-localfile-rename-mnn-code-purge/01-02-PLAN.md` lines 22-23, 89, 154-155 (the zero-mnn gate contract).

**Apply to:** Phase 2's zero-#ifdef gate — same structure: scoped `git grep` that MUST return empty (exit code 1 = pass), evidence captured into the plan SUMMARY.

Phase 1 form (adapt the pattern list, keep the shape):

```powershell
# Phase 1 precedent: empty output + exit code 1 = PASS
git grep -iI mnn -- include src test ":(exclude)test/src/filemanager_test.cpp"

# Phase 2 form (D-17 one-shot gate):
git grep -nE '#[ \t]*(ifdef|ifndef[ \t]+_WIN32|if[ \t]+defined\([ \t]*_WIN32[ \t]*\))' -- 'include/LocalFileCommon*.hpp' 'src/LocalFileCommon*.cpp'
# → must output nothing (exit code 1 from git grep = no matches = PASS)
```

### Logger member pattern

**Source:** current `include/LocalFileCommon.hpp` lines 50, 91.

**Apply to:** both platform headers, unchanged.

```cpp
sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "LocalFileCommon" );
```

D-14: identical tag `"LocalFileCommon"` in both halves — no log-filtering change for consumers.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| — | — | — | Every mechanic has an in-repo precedent: split sources are self-analogous (verbatim moves), header skeleton from `HTTPCommon.hpp`, quoted defines from `TEST_CERT_DIR`, test registration from `addtest()`, grep gate from Phase 1. The only first-of-its-kind element is the `if(WIN32)/elseif(UNIX)` source-selection block in `src/CMakeLists.txt` — no prior platform-conditional source selection exists in the repo, but this is intentional (this phase CREATES the pattern; PLAT-03 mandates it) and both halves (source-list edit + compile definition) have exact precedents. |

## Metadata

**Analog search scope:** `include/`, `src/`, `test/src/`, `cmake/functions.cmake`, `.planning/phases/01-*/`
**Files scanned:** 8 source/build files read in full or targeted; verified umbrella includers via workspace grep (exactly 3: `src/LocalFileCommon.cpp:4`, `src/LocalFileLoader.cpp:8`, `src/LocalFileSaver.cpp:14`)
**Pattern extraction date:** 2026-09-03
