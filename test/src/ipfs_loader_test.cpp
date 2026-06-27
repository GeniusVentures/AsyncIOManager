/**
 * Tests for IPFSLoader and IPFSSaver.
 * Gated behind ASYNC_IO_MANAGER_NETWORK_TESTS=ON.
 *
 * Follows the pattern from ipfs-bitswap-cpp/test/bitswap_server_client_test.cpp:
 * - Two local BitswapNode fixtures (server + client) with full libp2p/noise/crypto
 * - Server publishes content, client retrieves it
 */

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/bitswap_node.hpp"
#include "testutil/temp_file.hpp"
#include "testutil/test_fixture.hpp"

#include <gsl/span>
#include <libp2p/peer/address_repository.hpp>

using namespace sgns;

// ---------------------------------------------------------------------------
// IPFS Tests — uses server + client BitswapNodes
// ---------------------------------------------------------------------------

class IPFSIntegrationTest : public FileManagerTestFixture
{
protected:
    void SetUp() override
    {
        FileManagerTestFixture::SetUp();

        try
        {
            serverNode_ = std::make_unique<BitswapNode>();
            clientNode_ = std::make_unique<BitswapNode>();
        }
        catch ( const std::exception &e )
        {
            GTEST_SKIP() << "Cannot create BitswapNode: " << e.what();
        }

        // Manual peer discovery: add server to client's address repo
        auto serverInfo = serverNode_->getPeerInfo();
        auto &addrRepo  = clientNode_->getHost()->getPeerRepository().getAddressRepository();
        addrRepo.upsertAddresses(
            serverInfo.id,
            gsl::span( serverInfo.addresses.data(), serverInfo.addresses.size() ),
            libp2p::peer::ttl::kDay );
    }

    void TearDown() override
    {
        clientNode_.reset();
        serverNode_.reset();
    }

    std::unique_ptr<BitswapNode> serverNode_;
    std::unique_ptr<BitswapNode> clientNode_;
};

// ---------------------------------------------------------------------------
// IPFSLoader: retrieve content from server
// ---------------------------------------------------------------------------

TEST_F( IPFSIntegrationTest, Loader_RetrievesPublishedContent )
{
    // 1. Server publishes a single file
    TempFile tf( "ipfs loader test content" );

    bool                       publishDone  = false;
    std::optional<ipfs_bitswap::CID> publishedCid;

    serverNode_->getBitswap()->PublishFile(
        tf.pathString(),
        [&]( libp2p::outcome::result<ipfs_bitswap::CID> result )
        {
            if ( result.has_value() )
            {
                publishedCid = result.value();
            }
            publishDone = true;
        } );

    ASSERT_TRUE( pollUntil( [&]() { return publishDone; }, std::chrono::seconds( 60 ) ) )
        << "Timed out waiting for publish";
    ASSERT_TRUE( publishedCid.has_value() ) << "Publish completed but no CID returned";

    // 2. Client retrieves it
    auto serverInfo = serverNode_->getPeerInfo();

    bool                         retrieveDone = false;
    ipfs_bitswap::UnixFSContent  retrievedContent;

    clientNode_->getBitswap()->RequestContent(
        serverInfo, *publishedCid,
        [&]( libp2p::outcome::result<ipfs_bitswap::UnixFSContent> result )
        {
            if ( result.has_value() )
            {
                retrievedContent = result.value();
            }
            retrieveDone = true;
        } );

    ASSERT_TRUE( pollUntil( [&]() { return retrieveDone; }, std::chrono::seconds( 60 ) ) )
        << "Timed out waiting for retrieval";

    // 3. Verify content
    ASSERT_FALSE( retrievedContent.files.empty() );
    std::string retrievedStr( retrievedContent.files[0].content.data(),
                              retrievedContent.files[0].content.size() );
    EXPECT_EQ( retrievedStr, "ipfs loader test content" );
}

// ---------------------------------------------------------------------------
// IPFSSaver: publish content and verify CID
// ---------------------------------------------------------------------------

TEST_F( IPFSIntegrationTest, Saver_PublishesAndReturnsCID )
{
    // Set bitswap via FileManager (propagates to both IPFSSaver and IPFSLoader)
    FileManager::GetInstance().setBitswap( serverNode_->getBitswap() );

    IOContextRunner runner;

    bool                         completed = false;
    std::shared_ptr<std::string> saveLoc   = std::make_shared<std::string>();

    auto data = makeSingleFileResult( "saver_test.bin", "ipfs saver test content" );

    FileManager::GetInstance().SaveASync(
        "ipfs://test", data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; },
        saveLoc );

    ASSERT_TRUE( pollUntil( [&]() { return completed; }, std::chrono::seconds( 60 ) ) )
        << "Timed out waiting for IPFS save";

    EXPECT_FALSE( saveLoc->empty() );
    // saveLoc should contain "ipfs://<CID>"
    EXPECT_NE( saveLoc->find( "ipfs://" ), std::string::npos );
}

// ---------------------------------------------------------------------------
// IPFSLoader: unhappy paths
// ---------------------------------------------------------------------------

TEST_F( IPFSIntegrationTest, Loader_BadCIDReturnsError )
{
    IOContextRunner runner;

    bool                                  completed = false;
    std::optional<FileManager::ResultType> received;

    // Set bitswap via FileManager (propagates to IPFSLoader)
    FileManager::GetInstance().setBitswap( clientNode_->getBitswap() );

    // Use an obviously invalid CID
    FileManager::GetInstance().LoadASync(
        "ipfs://invalid-cid/test.bin", false, false, runner.ioc(),
        [&]( FileManager::ResultType buf )
        {
            received  = std::move( buf );
            completed = true;
        },
        "" );

    ASSERT_TRUE( pollUntil( [&]() { return completed; }, std::chrono::seconds( 10 ) ) )
        << "Timed out waiting for error callback";

    ASSERT_TRUE( received.has_value() );
    EXPECT_FALSE( received->has_value() );
}

TEST_F( IPFSIntegrationTest, Loader_InvalidUrlReturnsError )
{
    IOContextRunner runner;

    bool                                  completed = false;
    std::optional<FileManager::ResultType> received;

    FileManager::GetInstance().setBitswap( clientNode_->getBitswap() );

    // Empty path should fail parsing inside IPFSLoader
    FileManager::GetInstance().LoadASync(
        "ipfs://", false, false, runner.ioc(),
        [&]( FileManager::ResultType buf )
        {
            received  = std::move( buf );
            completed = true;
        },
        "" );

    ASSERT_TRUE( pollUntil( [&]() { return completed; }, std::chrono::seconds( 10 ) ) )
        << "Timed out waiting for error callback";

    ASSERT_TRUE( received.has_value() );
    EXPECT_FALSE( received->has_value() );
}
