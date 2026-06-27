#pragma once

/// @brief BitswapNode — wraps a full libp2p host + bitswap for integration testing.
/// Pattern from: ipfs-bitswap-cpp/test/bitswap_server_client_test.cpp

#include <bitswap.hpp>
#include <boost/di/extension/scopes/shared.hpp>
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

#include <memory>
#include <optional>
#include <thread>
#include <stdexcept>

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
        bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>( *host_, *event_bus_, io_context_ );
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

    std::shared_ptr<libp2p::Host>                 getHost() const { return host_; }
    std::shared_ptr<sgns::ipfs_bitswap::Bitswap>  getBitswap() const { return bitswap_; }
    std::shared_ptr<boost::asio::io_context>      getIOContext() const { return io_context_; }

    libp2p::peer::PeerInfo getPeerInfo() const
    {
        auto addresses = host_->getAddresses();
        return { host_->getId(), addresses };
    }

private:
    std::shared_ptr<libp2p::Host>                  host_;
    std::shared_ptr<sgns::ipfs_bitswap::Bitswap>   bitswap_;
    std::shared_ptr<libp2p::event::Bus>            event_bus_;
    std::shared_ptr<boost::asio::io_context>       io_context_;
    std::unique_ptr<boost::asio::io_context::work> work_guard_;
    std::thread                                    io_thread_;
};
