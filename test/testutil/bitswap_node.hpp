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
#include <libp2p/crypto/key_marshaller/key_marshaller_impl.hpp>
#include <libp2p/security/noise.hpp>
#include <libp2p/protocol/factory/protocol_factory.hpp>
#include <libp2p/peer/peer_repository.hpp>
#include <libp2p/peer/address_repository.hpp>
#include <libp2p/peer/impl/identity_manager_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/protocol/kademlia/config.hpp>
#include <libp2p/protocol/kademlia/impl/kademlia_impl.hpp>
#include <libp2p/protocol/kademlia/impl/content_routing_table_impl.hpp>
#include <libp2p/protocol/kademlia/impl/peer_routing_table_impl.hpp>
#include <libp2p/protocol/kademlia/impl/storage_backend_default.hpp>
#include <libp2p/protocol/kademlia/impl/storage_impl.hpp>
#include <libp2p/protocol/kademlia/impl/validator_default.hpp>
#include <ipfs_lite/dht/kademlia_dht.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <stdexcept>

class BitswapNode
{
public:
    BitswapNode()
    {
        // Initialize libp2p logging once (matching bitswap_server_client_test.cpp)
        static std::once_flag logInitFlag;
        std::call_once( logInitFlag, []()
        {
            const std::string logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: false
groups:
  - name: main
    sink: console
    level: info
    children:
      - name: libp2p
        level: debug
# ----------------
            )" );

            auto logging_system = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>(
                    std::make_shared<libp2p::log::Configurator>(),
                    logger_config ) );
            auto r = logging_system->configure();
            libp2p::log::setLoggingSystem( logging_system );
        } );

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
        // Keep our own copy — the injector binding below moves from *keys,
        // and the DHT's IdentityManager needs the same keypair as the host.
        auto keyPair = *keys;

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

        // Listen on random port — use 0.0.0.0 (matching reference) so getAddresses()
        // returns usable addresses for peer-to-peer connections.
        auto ma = libp2p::multi::Multiaddress::create( "/ip4/0.0.0.0/tcp/0" ).value();
        auto listenResult = host_->listen( ma );
        if ( !listenResult )
        {
            throw std::runtime_error( "Cannot listen: " + listenResult.error().message() );
        }

        host_->start();

        // Bitswap — uses event_bus_ (matching reference)
        bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>( *host_, *event_bus_, io_context_ );
        bitswap_->initialize();

        // Kademlia DHT on the same host — both nodes act as DHT servers
        // (enableServer=true) so a private 2-node cluster can provide/find
        // CIDs without bootstrapping to the public IPFS network.
        // NOTE: KademliaImpl & friends hold const& to the config, so it must
        // outlive them — hence the by-value member.
        kademliaConfig_               = std::make_shared<libp2p::protocol::kademlia::Config>();
        kademliaConfig_->enableServer = true;
        // Debug builds on Windows are too slow for the default 3s connection
        // timeout — kademlia executors give up just as the Noise handshake
        // completes. Random walk is needless dial traffic in a 2-node test
        // cluster (startDHT() seeds the routing table directly).
        kademliaConfig_->connectionTimeout = std::chrono::seconds( 15 );
        kademliaConfig_->randomWalk.enabled = false;
        // FindProvidersExecutor uses randomWalk.timeout as its overall
        // deadline — the default 10s can expire mid-handshake on a cold
        // dial in Debug builds.
        kademliaConfig_->randomWalk.timeout = std::chrono::seconds( 30 );
        auto schedulerBackend = std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_context_ );
        scheduler_            = std::make_shared<libp2p::basic::SchedulerImpl>(
            schedulerBackend, libp2p::basic::SchedulerImpl::Config{} );
        auto storage = std::make_shared<libp2p::protocol::kademlia::StorageImpl>(
            *kademliaConfig_,
            std::make_shared<libp2p::protocol::kademlia::StorageBackendDefault>(),
            scheduler_ );
        auto contentRoutingTable = std::make_shared<libp2p::protocol::kademlia::ContentRoutingTableImpl>(
            *kademliaConfig_, *scheduler_, event_bus_ );
        auto identityManager = std::make_shared<libp2p::peer::IdentityManagerImpl>(
            keyPair,
            std::make_shared<libp2p::crypto::marshaller::KeyMarshallerImpl>(
                std::make_shared<libp2p::crypto::validator::KeyValidatorImpl>( crypto_provider ) ) );
        auto peerRoutingTable = std::make_shared<libp2p::protocol::kademlia::PeerRoutingTableImpl>(
            *kademliaConfig_, identityManager, event_bus_ );
        kademlia_ = std::make_shared<libp2p::protocol::kademlia::KademliaImpl>(
            *kademliaConfig_,
            host_,
            storage,
            contentRoutingTable,
            peerRoutingTable,
            std::make_shared<libp2p::protocol::kademlia::ValidatorDefault>(),
            scheduler_,
            event_bus_,
            std::make_shared<libp2p::crypto::random::BoostRandomGenerator>() );

        // Background IO thread
        io_thread_ = std::thread( [this]() { io_context_->run(); } );
    }

    /// @brief Start the DHT. bootstrapPeers are dialed into kademlia's
    ///        routing table (in tests: the other node of the cluster).
    void startDHT( const std::vector<libp2p::peer::PeerInfo> &bootstrapPeers = {} )
    {
        std::vector<std::string> bootstrapAddresses;
        for ( auto &peer : bootstrapPeers )
        {
            for ( auto &addr : peer.addresses )
            {
                bootstrapAddresses.push_back( std::string( addr.getStringAddress() ) + "/p2p/" + peer.id.toBase58() );
            }
        }
        dht_ = std::make_shared<sgns::ipfs_lite::ipfs::dht::IpfsDHT>(
            kademlia_, std::move( bootstrapAddresses ), io_context_ );
        dht_->Start();
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
        // Explicit, dependency-safe teardown order (member-declaration order
        // alone destroys io_context_ before its users): DHT (its timer and
        // kademlia refs) -> bitswap -> kademlia -> host -> scheduler -> bus,
        // io_context_ last.
        dht_.reset();
        bitswap_.reset();
        kademlia_.reset();
        kademliaConfig_.reset();
        host_.reset();
        scheduler_.reset();
        event_bus_.reset();
        io_context_.reset();
    }

    BitswapNode( const BitswapNode & )            = delete;
    BitswapNode &operator=( const BitswapNode & ) = delete;

    std::shared_ptr<libp2p::Host>                 getHost() const { return host_; }
    std::shared_ptr<sgns::ipfs_bitswap::Bitswap>  getBitswap() const { return bitswap_; }
    std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> getDHT() const { return dht_; }
    std::shared_ptr<boost::asio::io_context>      getIOContext() const { return io_context_; }

    libp2p::peer::PeerInfo getPeerInfo() const
    {
        auto addresses = host_->getAddressesInterfaces();
        return { host_->getId(), addresses };
    }

private:
    std::shared_ptr<libp2p::Host>                             host_;
    std::shared_ptr<sgns::ipfs_bitswap::Bitswap>              bitswap_;
    std::shared_ptr<libp2p::protocol::kademlia::Config>       kademliaConfig_;
    std::shared_ptr<libp2p::protocol::kademlia::KademliaImpl> kademlia_;
    std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT>      dht_;
    std::shared_ptr<libp2p::event::Bus>                       event_bus_;
    std::shared_ptr<boost::asio::io_context>                  io_context_;
    std::shared_ptr<libp2p::basic::SchedulerImpl>             scheduler_;
    std::unique_ptr<boost::asio::io_context::work>            work_guard_;
    std::thread                                               io_thread_;
};
