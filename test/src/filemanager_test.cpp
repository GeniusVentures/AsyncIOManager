/**
 * Tests for FileManager — registration, dispatch, and operation counter lifecycle.
 */

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include "testutil/temp_file.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/test_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <string>

class FileManagerIntegrationTest : public FileManagerTestFixture
{
};

/// @brief RAII guard that swaps the process working directory for the duration
///        of a scope and restores it on exit (survives ASSERT failures).
///        LocalFileSaver writes its UUID directory relative to process CWD,
///        so the test redirects CWD into a TempDir to keep the repo clean.
struct CurrentPathGuard
{
    std::filesystem::path saved;

    explicit CurrentPathGuard( const std::filesystem::path &target )
    {
        std::error_code ec;
        saved = std::filesystem::current_path( ec );
        std::filesystem::current_path( target, ec );
    }

    ~CurrentPathGuard()
    {
        std::error_code ec;
        std::filesystem::current_path( saved, ec );
    }

    CurrentPathGuard( const CurrentPathGuard & )            = delete;
    CurrentPathGuard &operator=( const CurrentPathGuard & ) = delete;
};

// ---------------------------------------------------------------------------
// Registration tests
// ---------------------------------------------------------------------------

TEST_F( FileManagerIntegrationTest, InitializeSingletons_RegistersAllActiveHandlers )
{
    // InitializeSingletons is called in SetUp().
    // Verify by attempting to load/save with each registered prefix — no "No X registered" errors.

    TempFile tf( "registration test" );

    // file:// loader
    EXPECT_NO_THROW( FileManager::GetInstance().LoadFile( "file://" + tf.pathString() ) );

    // file:// saver (via SaveFile)
    TempDir     dir;
    std::string savePath = ( dir.path() / "reg_test.bin" ).string();
    auto        data     = std::make_shared<std::string>( "data" );
    EXPECT_NO_THROW( FileManager::GetInstance().SaveFile( "file://" + savePath, data ) );
}

// ---------------------------------------------------------------------------
// Dispatch tests — happy paths
// ---------------------------------------------------------------------------

TEST_F( FileManagerIntegrationTest, LoadFile_FilePrefixDispatchesToLocalFileLoader )
{
    const std::string expected = "dispatch load test";
    TempFile          tf( expected );

    auto result = FileManager::GetInstance().LoadFile( "file://" + tf.pathString() );

    ASSERT_NE( result, nullptr );
    auto content = std::static_pointer_cast<std::string>( result );
    EXPECT_EQ( *content, expected );
}

TEST_F( FileManagerIntegrationTest, SaveFile_FilePrefixDispatchesToLocalFileSaver )
{
    TempDir     dir;
    std::string filePath = ( dir.path() / "dispatch_save.bin" ).string();
    std::string content  = "dispatch save data";

    auto data = std::make_shared<std::string>( content );

    EXPECT_NO_THROW( FileManager::GetInstance().SaveFile( "file://" + filePath, data ) );

    std::ifstream ifs( filePath, std::ios::binary );
    ASSERT_TRUE( ifs.is_open() );
    std::string readBack( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    EXPECT_EQ( readBack, content );
}

// ---------------------------------------------------------------------------
// Dispatch tests — unhappy paths
// ---------------------------------------------------------------------------

TEST_F( FileManagerIntegrationTest, LoadFile_UnregisteredPrefixThrows )
{
    EXPECT_THROW( { FileManager::GetInstance().LoadFile( "unknown://some/path" ); }, std::range_error );
}

TEST_F( FileManagerIntegrationTest, SaveFile_UnregisteredPrefixThrows )
{
    auto data = std::make_shared<std::string>( "data" );
    EXPECT_THROW( { FileManager::GetInstance().SaveFile( "unknown://some/path", data ); }, std::range_error );
}

TEST_F( FileManagerIntegrationTest, SaveASync_UnregisteredPrefixThrows )
{
    IOContextRunner runner;
    auto            data = makeSingleFileResult( "test.bin", "content" );

    EXPECT_THROW(
        {
            FileManager::GetInstance().SaveASync(
                "unknown://some/path", data, runner.ioc(), []( FileManager::ResultType ) {} );
        },
        std::range_error );
}

TEST_F( FileManagerIntegrationTest, SaveASync_MnnPrefixThrows )
{
    IOContextRunner runner;
    auto            data = makeSingleFileResult( "test.bin", "content" );

    EXPECT_THROW(
        {
            FileManager::GetInstance().SaveASync(
                "mnn://some/path", data, runner.ioc(), []( FileManager::ResultType ) {} );
        },
        std::range_error );
}

// ---------------------------------------------------------------------------
// Operation counter lifecycle
// ---------------------------------------------------------------------------

TEST_F( FileManagerIntegrationTest, OutstandingOperations_IncrementAndDecrement )
{
    IOContextRunner runner;

    // Get initial count
    auto ptr = FileManager::GetInstance().GetOutstandingOperationsPointer();
    ASSERT_NE( ptr, nullptr );

    // Verify increment + decrement cycle via a simple load
    TempFile tf( "counter test" );

    bool completed = false;

    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(), false, false, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; },
        "" );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for async load";
}

TEST_F( FileManagerIntegrationTest, SaveASync_FilePrefixDispatchesToLocalFileSaver )
{
    IOContextRunner runner;
    TempDir         dir;
    std::string     content  = "filemanager async save";
    std::string     fileName = "fm_async_save.bin";

    bool completed = false;

    auto data = makeSingleFileResult( fileName, content );

    // Pass directory as URL; LocalFileSaver appends the data filename
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

// ---------------------------------------------------------------------------
// Auto-save chain (D-19): FileManager::LoadASync(save=true) → LocalFileSaver
// ---------------------------------------------------------------------------

TEST_F( FileManagerIntegrationTest, LoadASync_SaveTrueAutoSavesLoadedDataToDisk )
{
    const std::string expected = "auto-save chain content";
    TempFile          tf( expected );   // absolute path under temp_directory_path — unaffected by CWD swap
    TempDir           dir;              // becomes process CWD (LocalFileSaver writes <uuid>/ relative to CWD)
    IOContextRunner   runner;
    CurrentPathGuard  cwdGuard( dir.path() );

    // finalcall deliberately empty: it fires at load completion, BEFORE the
    // auto-save lands — it must NOT be the done-signal. Poll the filesystem.
    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(), false, true, runner.ioc(), []( FileManager::ResultType ) {}, "file" );

    const auto basename = tf.path().filename().string();
    bool       ok       = pollUntil(
              [&]()
              {
                  std::error_code ec;
                  for ( auto &e : std::filesystem::directory_iterator( dir.path(), ec ) )
                  {
                      auto name = e.path().filename().string();
                      if ( name.size() == 36 && std::count( name.begin(), name.end(), '-' ) == 4
                           && std::filesystem::exists( e.path() / basename ) )
                      {
                          return true; // UUID subdir (36 chars, 4 dashes) containing the original filename
                      }
                  }
                  return false;
              },
              std::chrono::seconds( 10 ) );
    ASSERT_TRUE( ok ) << "auto-save UUID directory never appeared";

    // Locate the UUID directory again and read the auto-saved bytes back.
    std::filesystem::path savedFile;
    {
        std::error_code ec;
        for ( auto &e : std::filesystem::directory_iterator( dir.path(), ec ) )
        {
            auto name = e.path().filename().string();
            if ( name.size() == 36 && std::count( name.begin(), name.end(), '-' ) == 4
                 && std::filesystem::exists( e.path() / basename ) )
            {
                savedFile = e.path() / basename;
                break;
            }
        }
    }
    ASSERT_FALSE( savedFile.empty() ) << "UUID directory disappeared between poll and read-back";

    std::ifstream ifs( savedFile, std::ios::binary );
    ASSERT_TRUE( ifs.is_open() ) << "File not found: " << savedFile;
    std::string readBack( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    EXPECT_EQ( readBack, expected );
}
