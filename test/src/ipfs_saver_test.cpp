/**
 * Tests for IPFSSaver — edge cases and error paths.
 * Gated behind ASYNC_IO_MANAGER_NETWORK_TESTS=ON.
 *
 * Integration happy path test is in ipfs_loader_test.cpp (Saver_PublishesAndReturnsCID).
 * This file covers saver-specific unhappy paths and edge cases.
 */

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/bitswap_node.hpp"
#include "testutil/test_fixture.hpp"

using namespace sgns;

class IPFSSaverEdgeTest : public FileManagerTestFixture
{
protected:
    void SetUp() override
    {
        FileManagerTestFixture::SetUp();

        try
        {
            node_ = std::make_unique<BitswapNode>();
        }
        catch ( const std::exception &e )
        {
            GTEST_SKIP() << "Cannot create BitswapNode: " << e.what();
        }

        FileManager::GetInstance().setBitswap( node_->getBitswap() );
    }

    void TearDown() override
    {
        node_.reset();
    }

    std::unique_ptr<BitswapNode> node_;
};

// ---------------------------------------------------------------------------
// Null / empty data
// ---------------------------------------------------------------------------

TEST_F( IPFSSaverEdgeTest, SaveASync_NullDataHandledGracefully )
{
    IOContextRunner runner;

    bool completed = false;

    // ResultType with null shared_ptr inside
    FileManager::ResultType nullResult = outcome::success(
        std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>( nullptr ) );

    // This should not crash; it should call handle_write
    FileManager::GetInstance().SaveASync(
        "ipfs://test", nullResult, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out — null data should still invoke callback";
}

TEST_F( IPFSSaverEdgeTest, SaveASync_BasicSaveCompletes )
{
    IOContextRunner runner;

    // bitswap is set in SetUp() — this test verifies the basic save path works.

    bool completed = false;

    auto data = makeSingleFileResult( "test.bin", "content" );

    FileManager::GetInstance().SaveASync(
        "ipfs://test", data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out — callback should always fire";
}

TEST_F( IPFSSaverEdgeTest, SaveASync_MismatchedPathsAndContentsHandled )
{
    IOContextRunner runner;

    // Different number of paths vs contents
    std::vector<std::string>       paths    = { "a.txt", "b.txt" };
    std::vector<std::vector<char>> contents = { { 'x' } };  // Only 1 content for 2 paths

    bool completed = false;

    auto data = makeMultiFileResult( paths, contents );

    FileManager::GetInstance().SaveASync(
        "ipfs://test", data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out — callback should fire even with mismatch";
}
