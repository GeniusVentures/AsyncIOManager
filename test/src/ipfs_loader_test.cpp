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
#include "testutil/temp_file.hpp"
#include "testutil/test_fixture.hpp"

#include <bitswap.hpp>
#include <boost/di/extension/scopes/shared.hpp>
#include <gsl/span>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/crypto/random_generator/boost_generator.hpp>
#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>
#include <libp2p/crypto/rsa_provider/rsa_provider_impl.hpp>
#include <libp2p/crypto/ecdsa_provider/ecdsa_provider_impl.hpp>
#include <libp2p/crypto/secp256k1_provider/secp256k1_provider_impl.hpp>
#include <libp2p/crypto/hmac_provider/hmac_provider_impl.hpp>
#include <libp2p/crypto/crypto_provider/crypto_provider_impl.hpp>
#include <libp2p/crypto/key_validator/key_validator_impl.hpp>
#include <libp2p/security/noise.hpp>
#include <libp2p/protocol/factory/protocol_factory.hpp>
#include <libp2p/peer/peer_repository.hpp>
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

        // Crypto setup — matching bitswap_server_client_test.cpp pattern
        auto csprng             = std::make_shared<libp2p::crypto::random::BoostRandomGenerator>();
        auto ed25519_provider   = std::make_shared<libp2p::crypto::ed25519::Ed25519ProviderImpl>();
        auto rsa_provider       = std::make_shared<libp2p::crypto::rsa::RsaProviderImpl>();
        auto ecdsa_provider     = std::make_shared<libp2p::crypto::ecdsa::EcdsaProviderImpl>();
        auto secp256k1_provider = std::make_shared<libp2p::crypto::secp256k1::Secp256k1ProviderImpl>();
        auto hmac_provider      = std::make_shared<libp2p::crypto::hmac::HmacProviderImpl>();

        std::shared_ptr<libp2p::crypto::CryptoProvider> crypto_provider =
            std::make_shared<libp2p::crypto::CryptoProviderImpl>(
                csprng, ed25519_provider, rsa_provider, ecdsa_provider,
                secp256k1_provider, hmac_provider );

        auto validator = std::make_shared<libp2p::crypto::validator::KeyValidatorImpl>( crypto_provider );

        // Use std::optional so std::move(*keys) compiles (matches reference)
        std::optional<libp2p::crypto::KeyPair> keys;
        keys = crypto_provider->generateKeys(
                   libp2p::crypto::Key::Type::Ed25519,
                   libp2p::crypto::common::RSAKeyType::RSA2048 )
                   .value();

        // Event bus (required by Bitswap constructor)
        event_bus_ = std::make_shared<libp2p::event::Bus>();

        // Host injector — matching reference pattern with TEMPLATE_TO bindings
        auto injector = libp2p::injector::makeHostInjector<di::extension::shared_config>(
            libp2p::injector::useSecurityAdaptors<libp2p::security::Noise>(),
            di::bind<libp2p::crypto::KeyPair>().to( std::move( *keys ) )[di::override],
            di::bind<libp2p::crypto::CryptoProvider>().to( crypto_provider )[di::override],
            di::bind<libp2p::crypto::random::CSPRNG>().to( std::move( csprng ) )[di::override],
            di::bind<libp2p::crypto::marshaller::KeyMarshaller>()
                .TEMPLATE_TO<libp2p::crypto::marshaller::KeyMarshallerImpl>()[di::override],
            di::bind<libp2p::crypto::validator::KeyValidator>()
                .TEMPLATE_TO( std::move( validator ) )[di::override] );

        host_       = injector.create<std::shared_ptr<libp2p::Host>>();
        io_context_ = injector.create<std::shared_ptr<boost::asio::io_context>>();

        // Keep IO context alive during startup
        work_guard_ = std::make_unique<boost::asio::io_context::work>( *io_context_ );

        // Protocol configuration — matching reference
        libp2p::protocol::factory::ProtocolFactory::ProtocolConfig protocol_config;
        protocol_config.enable_identify         = true;
        protocol_config.enable_autonat          = false;
        protocol_config.enable_relay            = false;
        protocol_config.enable_holepunch_server = false;
        protocol_config.enable_holepunch_client = false;

        auto protocols = libp2p::protocol::factory::ProtocolFactory::createProtocols(
            host_, protocol_config, injector );
        protocols.identify->start();

        // Listen on random port
        auto ma = libp2p::multi::Multiaddress::create( "/ip4/127.0.0.1/tcp/0" ).value();
        auto listenResult = host_->listen( ma );
        if ( !listenResult )
        {
            throw std::runtime_error( "Cannot listen: " + listenResult.error().message() );
        }

        host_->start();

        // Bitswap — uses event_bus_ (matching reference)
        bitswap_ = std::make_shared<ipfs_bitswap::Bitswap>( *host_, *event_bus_, io_context_ );
        bitswap_->initialize();

        // Background IO thread
        io_thread_ = std::thread( [this]() { io_context_->run(); } );
    }

    ~BitswapNode()
    {
        if ( host_ )
        {
            host_->stop();
        }
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

    std::shared_ptr<libp2p::Host>            getHost() const { return host_; }
    std::shared_ptr<ipfs_bitswap::Bitswap>   getBitswap() const { return bitswap_; }
    std::shared_ptr<boost::asio::io_context> getIOContext() const { return io_context_; }

    libp2p::peer::PeerInfo getPeerInfo() const
    {
        auto addresses = host_->getAddresses();
        return { host_->getId(), addresses };
    }

private:
    std::shared_ptr<libp2p::Host>                  host_;
    std::shared_ptr<ipfs_bitswap::Bitswap>         bitswap_;
    std::shared_ptr<libp2p::event::Bus>            event_bus_;
    std::shared_ptr<boost::asio::io_context>       io_context_;
    std::unique_ptr<boost::asio::io_context::work> work_guard_;
    std::thread                                    io_thread_;
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
