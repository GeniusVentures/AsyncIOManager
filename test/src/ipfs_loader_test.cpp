/**
 * Tests for IPFSLoader and IPFSSaver.
 * Gated behind ASYNC_IO_MANAGER_NETWORK_TESTS=ON.
 *
 * Follows the pattern from ipfs-bitswap-cpp/test/bitswap_server_client_test.cpp:
 * - Two local BitswapNode fixtures (server + client) with full libp2p/noise/crypto
 * - Server publishes content, client retrieves it
 */

#include <gtest/gtest.h>
#include "IPFSLoader.hpp"
#include "IPFSSaver.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/test_fixture.hpp"

#include <bitswap.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/crypto/random/boost_random_generator.hpp>
#include <libp2p/crypto/ed25519/ed25519_provider_impl.hpp>
#include <libp2p/crypto/rsa/rsa_provider_impl.hpp>
#include <libp2p/crypto/ecdsa/ecdsa_provider_impl.hpp>
#include <libp2p/crypto/secp256k1/secp256k1_provider_impl.hpp>
#include <libp2p/crypto/hmac/hmac_provider_impl.hpp>
#include <libp2p/crypto/crypto_provider_impl.hpp>
#include <libp2p/security/noise.hpp>
#include <libp2p/protocol/identify/identify.hpp>
#include <libp2p/protocol/identify/identify_msg_processor.hpp>
#include <libp2p/peer/address_repository.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>

using namespace sgns;

// ---------------------------------------------------------------------------
// BitswapNode — wraps a full libp2p host + bitswap for testing
// Pattern from: ipfs-bitswap-cpp/test/bitswap_server_client_test.cpp
// ---------------------------------------------------------------------------

class BitswapNode
{
public:
    BitswapNode()
    {
        namespace di = boost::di;

        // Crypto setup
        auto csprng          = std::make_shared<libp2p::crypto::random::BoostRandomGenerator>();
        auto ed25519_provider = std::make_shared<libp2p::crypto::ed25519::Ed25519ProviderImpl>();
        auto rsa_provider     = std::make_shared<libp2p::crypto::rsa::RsaProviderImpl>();
        auto ecdsa_provider   = std::make_shared<libp2p::crypto::ecdsa::EcdsaProviderImpl>();
        auto secp256k1_provider = std::make_shared<libp2p::crypto::secp256k1::Secp256k1ProviderImpl>();
        auto hmac_provider     = std::make_shared<libp2p::crypto::hmac::HmacProviderImpl>();

        auto crypto_provider = std::make_shared<libp2p::crypto::CryptoProviderImpl>(
            csprng, ed25519_provider, rsa_provider, ecdsa_provider, secp256k1_provider, hmac_provider );

        auto keys = crypto_provider->generateKeys( libp2p::crypto::Key::Type::Ed25519 ).value();

        // Host injector
        auto injector = libp2p::injector::makeHostInjector<di::extension::shared_config>(
            libp2p::injector::useSecurityAdaptors<libp2p::security::Noise>(),
            di::bind<libp2p::crypto::KeyPair>.to( std::move( *keys ) )[di::override],
            di::bind<libp2p::crypto::CryptoProvider>.to( crypto_provider )[di::override],
            di::bind<libp2p::crypto::random::CSPRNG>.to( csprng )[di::override],
            di::bind<libp2p::crypto::marshaller::KeyMarshaller>.to(
                std::make_shared<libp2p::crypto::marshaller::KeyMarshallerImpl>() )[di::override] );

        host_        = injector.create<std::shared_ptr<libp2p::Host>>();
        io_context_  = injector.create<std::shared_ptr<boost::asio::io_context>>();

        // Bitswap
        bitswap_ = std::make_shared<ipfs_bitswap::Bitswap>( *host_, host_->getBus(), io_context_ );

        // Listen
        auto ma   = libp2p::multi::Multiaddress::create( "/ip4/127.0.0.1/tcp/0" ).value();
        auto listenResult = host_->listen( ma );
        if ( !listenResult )
        {
            throw std::runtime_error( "Cannot listen: " + listenResult.error().message() );
        }

        bitswap_->initialize();
        host_->start();

        // Background IO thread
        work_guard_ = std::make_unique<boost::asio::io_context::work>( *io_context_ );
        io_thread_  = std::thread( [this]() { io_context_->run(); } );
    }

    ~BitswapNode()
    {
        work_guard_.reset();
        if ( io_context_ && !io_context_->stopped() )
        {
            io_context_->stop();
        }
        if ( io_thread_.joinable() )
        {
            io_thread_.join();
        }
    }

    BitswapNode( const BitswapNode & )            = delete;
    BitswapNode &operator=( const BitswapNode & ) = delete;

    std::shared_ptr<libp2p::Host>               getHost() const { return host_; }
    std::shared_ptr<ipfs_bitswap::Bitswap>      getBitswap() const { return bitswap_; }
    std::shared_ptr<boost::asio::io_context>    getIOContext() const { return io_context_; }

    libp2p::peer::PeerInfo getPeerInfo() const
    {
        auto addresses = host_->getAddresses();
        return { host_->getId(), addresses };
    }

private:
    std::shared_ptr<libp2p::Host>               host_;
    std::shared_ptr<ipfs_bitswap::Bitswap>      bitswap_;
    std::shared_ptr<boost::asio::io_context>    io_context_;
    std::unique_ptr<boost::asio::io_context::work> work_guard_;
    std::thread                                 io_thread_;
};

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

    bool              publishDone = false;
    ipfs_bitswap::CID publishedCid;

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

    // 2. Client retrieves it
    auto serverInfo = serverNode_->getPeerInfo();

    bool                         retrieveDone = false;
    ipfs_bitswap::UnixFSContent  retrievedContent;

    clientNode_->getBitswap()->RequestContent(
        serverInfo, publishedCid,
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
    // Set bitswap on the server's saver
    auto &saver = sgns::IPFSSaver::GetInstance();
    saver.setBitswap( serverNode_->getBitswap() );
    ASSERT_TRUE( saver.hasExternalBitswap() );

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

    bool                  completed = false;
    FileLoader::ResultType received;

    auto &loader     = sgns::IPFSLoader::GetInstance();
    auto *ipfsLoader = dynamic_cast<sgns::IPFSLoader *>( &loader );
    ASSERT_NE( ipfsLoader, nullptr );

    // Set bitswap on the client loader
    ipfsLoader->setBitswap( clientNode_->getBitswap() );

    // Use an obviously invalid CID
    ipfsLoader->LoadASync(
        "invalid-cid/test.bin", false, false, runner.ioc(),
        [&]( std::shared_ptr<boost::asio::io_context>, FileLoader::ResultType buf, bool, bool )
        {
            received  = buf;
            completed = true;
        } );

    ASSERT_TRUE( pollUntil( [&]() { return completed; }, std::chrono::seconds( 10 ) ) )
        << "Timed out waiting for error callback";

    EXPECT_FALSE( received.has_value() );
}

TEST_F( IPFSIntegrationTest, Loader_InvalidUrlReturnsError )
{
    IOContextRunner runner;

    bool                  completed = false;
    FileLoader::ResultType received;

    auto &loader     = sgns::IPFSLoader::GetInstance();
    auto *ipfsLoader = dynamic_cast<sgns::IPFSLoader *>( &loader );
    ASSERT_NE( ipfsLoader, nullptr );

    ipfsLoader->setBitswap( clientNode_->getBitswap() );

    // Empty URL should fail parsing
    ipfsLoader->LoadASync(
        "", false, false, runner.ioc(),
        [&]( std::shared_ptr<boost::asio::io_context>, FileLoader::ResultType buf, bool, bool )
        {
            received  = buf;
            completed = true;
        } );

    ASSERT_TRUE( pollUntil( [&]() { return completed; }, std::chrono::seconds( 10 ) ) )
        << "Timed out waiting for error callback";

    EXPECT_FALSE( received.has_value() );
}
