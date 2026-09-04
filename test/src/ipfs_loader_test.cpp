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
#include "testutil/test_fixture.hpp"

#include <gsl/span>
#include <libp2p/peer/address_repository.hpp>

using namespace sgns;

// ---------------------------------------------------------------------------
// IPFS Tests — uses server + client BitswapNodes (created once for the suite,
// matching bitswap_server_client_test.cpp which reuses one node pair).
// ---------------------------------------------------------------------------

class IPFSIntegrationTest : public FileManagerTestFixture
{
protected:
    static void SetUpTestSuite()
    {
        try
        {
            s_serverNode = std::make_unique<BitswapNode>();
            // Give server time to initialize (matching reference test pattern)
            std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
            s_clientNode = std::make_unique<BitswapNode>();
        }
        catch ( const std::exception &e )
        {
            GTEST_SKIP() << "Cannot create BitswapNode: " << e.what();
        }

        // Manual peer discovery: add server to client's address repo
        auto serverInfo = s_serverNode->getPeerInfo();
        auto &addrRepo  = s_clientNode->getHost()->getPeerRepository().getAddressRepository();
        addrRepo.upsertAddresses(
            serverInfo.id,
            gsl::span( serverInfo.addresses.data(), serverInfo.addresses.size() ),
            libp2p::peer::ttl::kDay );

        // Start the DHT: only the client bootstraps to the server (the
        // provider). The server runs as a pure DHT server — provide() records
        // the CID in its local routing table and it answers the client's
        // GET_PROVIDERS from there. Bootstrapping both ways makes the nodes
        // dial each other simultaneously, which stalls every dial for the
        // full connection timeout.
        s_serverNode->startDHT();
        s_clientNode->startDHT( { serverInfo } );

        // Warm up the client->server connection: kademlia's newStream then
        // reuses it instead of paying a full cold Noise handshake inside the
        // FindProviders deadline (which can take 10+s in Debug on Windows).
        {
            auto streamOpened = std::make_shared<bool>( false );
            s_clientNode->getHost()->newStream(
                serverInfo, { "/ipfs/kad/1.0.0" },
                [streamOpened]( libp2p::StreamAndProtocolOrError stream ) {
                    if ( stream )
                    {
                        // Leaving the stream open keeps the connection reuse;
                        // resetting it would tear the connection back down.
                        stream.value().stream->close( []( libp2p::outcome::result<void> ) {} );
                    }
                    *streamOpened = true;
                },
                std::chrono::milliseconds( 30000 ) );
            pollUntil( [&]() { return *streamOpened; }, std::chrono::seconds( 35 ) );
        }
    }

    static void TearDownTestSuite()
    {
        s_clientNode.reset();
        s_serverNode.reset();
    }

    void SetUp() override
    {
        FileManagerTestFixture::SetUp();
    }

    static BitswapNode &serverNode() { return *s_serverNode; }
    static BitswapNode &clientNode() { return *s_clientNode; }

private:
    static std::unique_ptr<BitswapNode> s_serverNode;
    static std::unique_ptr<BitswapNode> s_clientNode;
};

std::unique_ptr<BitswapNode> IPFSIntegrationTest::s_serverNode;
std::unique_ptr<BitswapNode> IPFSIntegrationTest::s_clientNode;

// ---------------------------------------------------------------------------
// IPFSLoader: retrieve content from server
// ---------------------------------------------------------------------------

TEST_F( IPFSIntegrationTest, Loader_RetrievesPublishedContent )
{
    // Set bitswap on server side via FileManager
    FileManager::GetInstance().setBitswap( serverNode().getBitswap() );

    // 1. Server publishes content via FileManager → IPFSSaver
    {
        IOContextRunner             runner;
        bool                        saveDone = false;
        std::shared_ptr<std::string> saveLoc  = std::make_shared<std::string>();

        auto data = makeSingleFileResult( "published.bin", "ipfs loader test content" );

        FileManager::GetInstance().SaveASync(
            "ipfs://published.bin", data, runner.ioc(),
            [&]( FileManager::ResultType ) { saveDone = true; },
            saveLoc );

        ASSERT_TRUE( pollUntil( [&]() { return saveDone; }, std::chrono::seconds( 60 ) ) )
            << "Timed out waiting for IPFS publish via FileManager";
        ASSERT_FALSE( saveLoc->empty() ) << "saveLoc should contain ipfs://<CID>";
        ASSERT_NE( saveLoc->find( "ipfs://" ), std::string::npos );

        // Extract CID from saveLoc ("ipfs://<CID>") and register server as provider
        std::string cidStr = saveLoc->substr( 7 );  // strip "ipfs://"
        auto        cid    = libp2p::multi::ContentIdentifierCodec::fromString( cidStr );
        ASSERT_TRUE( cid.has_value() ) << "Failed to decode CID from saveLoc";

        // Register server as provider on client bitswap so IPFSLoader can find it
        FileManager::GetInstance().setBitswap( clientNode().getBitswap() );
        clientNode().getBitswap()->AddProvider( cid.value(), serverNode().getPeerInfo() );

        // 2. Client retrieves content via FileManager → IPFSLoader
        // Use a fresh IOContextRunner — the save's runner may have stopped its context
        IOContextRunner                  loadRunner;
        bool                             loadDone = false;
        std::optional<FileManager::ResultType> received;

        // LoadASync needs "ipfs://<CID>/<filename>" — parseIPFSUrl requires the '/'
        std::string loadUrl = *saveLoc + "/published.bin";

        FileManager::GetInstance().LoadASync(
            loadUrl,
            false, loadRunner.ioc(),
            [&]( FileManager::ResultType result )
            {
                received = std::move( result );
                loadDone = true;
            },
            "" );

        ASSERT_TRUE( pollUntil( [&]() { return loadDone; }, std::chrono::seconds( 60 ) ) )
            << "Timed out waiting for IPFS retrieve via FileManager";

        // 3. Verify content
        ASSERT_TRUE( received.has_value() );
        ASSERT_TRUE( received->has_value() );
        ASSERT_NE( received->value(), nullptr );
        ASSERT_FALSE( received->value()->second.empty() );

        std::string retrievedStr( received->value()->second[0].data(),
                                  received->value()->second[0].size() );
        EXPECT_EQ( retrievedStr, "ipfs loader test content" );
    }
}

// ---------------------------------------------------------------------------
// IPFSLoader + DHT: saver provides via DHT, loader discovers the provider
// automatically over the DHT (setBitswap overload with a DHT)
// ---------------------------------------------------------------------------

TEST_F( IPFSIntegrationTest, Loader_FindsProviderViaDHT )
{
    // Saver side: server node's bitswap with its DHT (the second setBitswap
    // overload) — the saver announces the published CID via DHT provide.
    FileManager::GetInstance().setBitswap( serverNode().getBitswap(), serverNode().getDHT() );

    std::string cidStr;
    {
        IOContextRunner              runner;
        bool                         saveDone = false;
        std::shared_ptr<std::string> saveLoc  = std::make_shared<std::string>();

        auto data = makeSingleFileResult( "dht_published.bin", "ipfs dht test content" );

        FileManager::GetInstance().SaveASync(
            "ipfs://published", data, runner.ioc(),
            [&]( FileManager::ResultType ) { saveDone = true; },
            saveLoc );

        ASSERT_TRUE( pollUntil( [&]() { return saveDone; }, std::chrono::seconds( 60 ) ) )
            << "Timed out waiting for IPFS publish via FileManager";
        ASSERT_FALSE( saveLoc->empty() );
        ASSERT_NE( saveLoc->find( "ipfs://" ), std::string::npos );

        cidStr = saveLoc->substr( 7 ); // strip "ipfs://"

        auto cid = libp2p::multi::ContentIdentifierCodec::fromString( cidStr );
        ASSERT_TRUE( cid.has_value() ) << "Failed to decode CID from saveLoc";
    }

    // Loader side: client node's bitswap with its DHT — provider discovery
    // happens automatically over the DHT; no AddProvider() call here.
    FileManager::GetInstance().setBitswap( clientNode().getBitswap(), clientNode().getDHT() );

    {
        IOContextRunner                         loadRunner;
        bool                                    loadDone = false;
        std::optional<FileManager::ResultType>  received;

        std::string loadUrl = "ipfs://" + cidStr + "/dht_published.bin";

        FileManager::GetInstance().LoadASync(
            loadUrl,
            false, loadRunner.ioc(),
            [&]( FileManager::ResultType result )
            {
                received = std::move( result );
                loadDone = true;
            },
            "" );

        // Generous budget: DHT retries (15s connection timeout + 10s retry
        // interval) plus a slow Debug-build Noise handshake can take a while.
        ASSERT_TRUE( pollUntil( [&]() { return loadDone; }, std::chrono::seconds( 120 ) ) )
            << "Timed out waiting for DHT-based IPFS retrieve via FileManager";

        ASSERT_TRUE( received.has_value() );
        ASSERT_TRUE( received->has_value() );
        ASSERT_NE( received->value(), nullptr );
        ASSERT_FALSE( received->value()->second.empty() );

        std::string retrievedStr( received->value()->second[0].data(),
                                  received->value()->second[0].size() );
        EXPECT_EQ( retrievedStr, "ipfs dht test content" );
    }
}

// ---------------------------------------------------------------------------
// IPFSSaver: publish content and verify CID
// ---------------------------------------------------------------------------

TEST_F( IPFSIntegrationTest, Saver_PublishesAndReturnsCID )
{
    // Set bitswap via FileManager (propagates to both IPFSSaver and IPFSLoader)
    FileManager::GetInstance().setBitswap( serverNode().getBitswap() );

    {
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
}

// ---------------------------------------------------------------------------
// IPFSLoader: unhappy paths
// ---------------------------------------------------------------------------

TEST_F( IPFSIntegrationTest, Loader_BadCIDReturnsError )
{
    {
        IOContextRunner runner;

        bool                                  completed = false;
        std::optional<FileManager::ResultType> received;

        // Set bitswap via FileManager (propagates to IPFSLoader)
        FileManager::GetInstance().setBitswap( clientNode().getBitswap() );

        // Use an obviously invalid CID
        FileManager::GetInstance().LoadASync(
            "ipfs://invalid-cid/test.bin", false, runner.ioc(),
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
}

TEST_F( IPFSIntegrationTest, Loader_InvalidUrlReturnsError )
{
    {
        IOContextRunner runner;

        bool                                  completed = false;
        std::optional<FileManager::ResultType> received;

        FileManager::GetInstance().setBitswap( clientNode().getBitswap() );

        // Empty path should fail parsing inside IPFSLoader
        FileManager::GetInstance().LoadASync(
            "ipfs://", false, runner.ioc(),
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
}
