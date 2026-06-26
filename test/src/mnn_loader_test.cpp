/**
 * Tests for MNNLoader — local file loading via "file://" prefix.
 */

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include "testutil/temp_file.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/test_fixture.hpp"
#include <optional>

class MNNLoaderTest : public FileManagerTestFixture
{
};

// ---------------------------------------------------------------------------
// Synchronous LoadFile — happy path
// ---------------------------------------------------------------------------

TEST_F( MNNLoaderTest, LoadFile_ReadsExistingFile )
{
    const std::string expected = "hello world from temp file";
    TempFile          tf( expected );

    auto result = FileManager::GetInstance().LoadFile( "file://" + tf.pathString() );

    ASSERT_NE( result, nullptr );
    auto content = std::static_pointer_cast<std::string>( result );
    ASSERT_EQ( *content, expected );
}

// ---------------------------------------------------------------------------
// Synchronous LoadFile — unhappy paths
// ---------------------------------------------------------------------------

TEST_F( MNNLoaderTest, LoadFile_NonexistentFileThrows )
{
    EXPECT_THROW( { FileManager::GetInstance().LoadFile( "file:///nonexistent/path/xyz.abc" ); },
                  std::range_error );
}

// ---------------------------------------------------------------------------
// Asynchronous LoadASync — happy path
// ---------------------------------------------------------------------------

TEST_F( MNNLoaderTest, LoadASync_ReadsExistingFile )
{
    const std::string expected = "async file content";
    TempFile          tf( expected );
    IOContextRunner   runner;

    bool                                   completed = false;
    std::optional<FileManager::ResultType> received;

    FileManager::GetInstance().LoadASync(
        "file://" + tf.pathString(),
        false,       // parse
        false,       // save
        runner.ioc(),
        [&]( FileManager::ResultType result )
        {
            received  = std::move( result );
            completed = true;
        },
        "" );        // savetype (not used for load-only)

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for async load";

    ASSERT_TRUE( received.has_value() );
    ASSERT_TRUE( received->has_value() );
    ASSERT_NE( received->value(), nullptr );
    ASSERT_FALSE( received->value()->second.empty() );

    std::string loaded( received->value()->second[0].data(), received->value()->second[0].size() );
    EXPECT_EQ( loaded, expected );
}

// ---------------------------------------------------------------------------
// Asynchronous LoadASync — unhappy paths
// ---------------------------------------------------------------------------

TEST_F( MNNLoaderTest, LoadASync_NonexistentFileReturnsError )
{
    IOContextRunner runner;

    bool                                   completed = false;
    std::optional<FileManager::ResultType> received;

    FileManager::GetInstance().LoadASync(
        "file:///nonexistent/path/xyz.abc",
        false, false, runner.ioc(),
        [&]( FileManager::ResultType result )
        {
            received  = std::move( result );
            completed = true;
        },
        "" );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for async load";

    // Should return an error
    ASSERT_TRUE( received.has_value() );
    ASSERT_FALSE( received->has_value() );
}
