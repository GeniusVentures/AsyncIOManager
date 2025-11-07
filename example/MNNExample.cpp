//#define BOOST_ASIO_HAS_IO_URING 1 //Linux
//#define BOOST_ASIO_DISABLE_EPOLL 0 //Maybe linux?
#include <iostream>
#include <string>
#include <vector>
#include "ASIOSingleton.hpp"
#include "FileManager.hpp"
#include <asiomgr-logger.hpp>
//#include "MNNLoader.hpp"
//#include "MNNParser.hpp"
//#include "IPFSLoader.hpp"
//#include "HTTPLoader.hpp"
//#include "SFTPLoader.hpp"
//#include "WSLoader.hpp"
#include "URLStringUtil.h"
#include <libp2p/injector/host_injector.hpp>
#include "libp2p/injector/kademlia_injector.hpp"
#include <boost/di/extension/scopes/shared.hpp>
#include <libp2p/protocol/factory/protocol_factory.hpp>
#include <boost/format.hpp>
#include <bitswap.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/peer/peer_info.hpp>
/**
 * This program is example to loading MNN model file
 */
//#define IPFS_FILE_PATH_NAME "ipfs://example.mnn"
#define FILE_PATH_NAME "file://../test/1.mnn"
#define FILE_SAVE_NAME "file://test.mnn"
void loadhandler(boost::system::error_code ec, std::size_t n, std::vector<char>& buffer) {
    if (!ec) {
        std::cout << "Received data: ";
        std::cout << std::endl;
        std::cout << "Handler Out Size:" << n << std::endl;
    }
    else {
        std::cerr << "Error in async_read: " << ec.message() << std::endl;
    }
}


int main(int argc, char **argv)
{
    std::string file_name = "";
    std::vector<string> file_names(0);
    std::cout << "argcount" << argc << std::endl;
    if (argc < 2)
    {
        std::cout << argv[0] << " [MNN extension file]" << std::endl;
        std::cout << "E.g:" << std::endl;
        std::cout << "\t " << argv[0] << " file://../test/1.mnn" << std::endl;
        return 1;
    }
    else
    {
        file_name = std::string(argv[1]);
        for (int i = 1; i < argc; i++)
        {
            std::cout << "file: " << argv[i] << std::endl;
            file_names.push_back(argv[i]);
        }
    }
    const std::string logger_config(R"(
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
        level: trace
      - name: bitswap
        level: trace
# ----------------
        )");

    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            std::make_shared<libp2p::log::Configurator>(),
            logger_config));
    auto r = logging_system->configure();
    libp2p::log::setLoggingSystem(logging_system);
   // return 1;
    // this should all be moved to GTest
    // Break out the URL prefix, file path, and extension.
    std::string url_prefix;
    std::string file_path;
    std::string extension;
    std::string http_host;
    std::string http_path;

    getURLComponents(
            "https://www.example.com/test.jpg",
            url_prefix, file_path, extension);

    // test plugins
    if (file_name.empty())
    {
        file_name = FILE_PATH_NAME;
    }
    auto loggerHttpCommon = sgns::asiomgr::createLogger("HTTPCommon");
    auto loggerFileManager = sgns::asiomgr::createLogger("FileManager");
    auto loggerIpfsLoader = sgns::asiomgr::createLogger("IPFSLoader");
    auto loggerWSCommon = sgns::asiomgr::createLogger("WSCommon");
    auto loggerIPFSCommon = sgns::asiomgr::createLogger("IPFSCommon");
    auto loggerFILECommon = sgns::asiomgr::createLogger("FILECommon");
    auto loggerIPFSSaver = sgns::asiomgr::createLogger("IPFSSaver");
    auto loggerMNNLoader = sgns::asiomgr::createLogger("MNNLoader");

    loggerHttpCommon->set_level(spdlog::level::trace);
    loggerFileManager->set_level(spdlog::level::trace);
    loggerIpfsLoader->set_level(spdlog::level::trace);
    loggerWSCommon->set_level(spdlog::level::trace);
    loggerIPFSCommon->set_level(spdlog::level::trace);
    loggerFILECommon->set_level(spdlog::level::trace);
    loggerIPFSSaver->set_level(spdlog::level::trace);
    loggerMNNLoader->set_level(spdlog::level::trace);
    
    // ===================================================================
    // LIBP2P HOST AND BITSWAP SETUP
    // ===================================================================
    
    // Create crypto providers for libp2p
    auto createCryptoProviders = []() {
        auto csprng = std::make_shared<libp2p::crypto::random::BoostRandomGenerator>();
        auto ed25519_provider = std::make_shared<libp2p::crypto::ed25519::Ed25519ProviderImpl>();
        auto rsa_provider = std::make_shared<libp2p::crypto::rsa::RsaProviderImpl>();
        auto ecdsa_provider = std::make_shared<libp2p::crypto::ecdsa::EcdsaProviderImpl>();
        auto secp256k1_provider = std::make_shared<libp2p::crypto::secp256k1::Secp256k1ProviderImpl>();
        auto hmac_provider = std::make_shared<libp2p::crypto::hmac::HmacProviderImpl>();
        
        std::shared_ptr<libp2p::crypto::CryptoProvider> crypto_provider = std::make_shared<libp2p::crypto::CryptoProviderImpl>(
            csprng, ed25519_provider, rsa_provider, ecdsa_provider, secp256k1_provider, hmac_provider);
        auto validator = std::make_shared<libp2p::crypto::validator::KeyValidatorImpl>(crypto_provider);
        
        return std::make_tuple(crypto_provider, validator, csprng);
    };
    
    auto [crypto_provider, validator, csprng] = createCryptoProviders();
    
    // Generate keypair and create event bus
    std::optional<libp2p::crypto::KeyPair> keys;
    keys = crypto_provider->generateKeys(libp2p::crypto::Key::Type::Ed25519).value();
    auto event_bus = std::make_shared<libp2p::event::Bus>();
    
    // Create libp2p host injector with crypto setup
    namespace di = boost::di;
    auto injector = libp2p::injector::makeHostInjector<di::extension::shared_config>(
        libp2p::injector::useSecurityAdaptors<libp2p::security::Noise>(),
        di::bind<libp2p::crypto::KeyPair>().to(std::move(*keys))[di::override],
        di::bind<libp2p::crypto::CryptoProvider>().to(crypto_provider)[di::override],
        di::bind<libp2p::crypto::random::CSPRNG>().to(std::move(csprng))[di::override],
        di::bind<libp2p::crypto::marshaller::KeyMarshaller>().TEMPLATE_TO<libp2p::crypto::marshaller::KeyMarshallerImpl>()[di::override],
        di::bind<libp2p::crypto::validator::KeyValidator>().TEMPLATE_TO(std::move(validator))[di::override]
    );
    
    // Create host and io_context
    auto libp2phost = injector.create<std::shared_ptr<libp2p::Host>>();
    auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();
    
    // Configure protocols (only enable identify)
    libp2p::protocol::factory::ProtocolFactory::ProtocolConfig protocol_config;
    protocol_config.enable_identify = true;
    protocol_config.enable_autonat = false;
    protocol_config.enable_relay = false;
    protocol_config.enable_holepunch_server = false;
    protocol_config.enable_holepunch_client = false;
    
    auto protocols = libp2p::protocol::factory::ProtocolFactory::createProtocols(libp2phost, protocol_config, injector);
    protocols.identify->start();
    
    // Setup host listening address
    auto bindaddress = (boost::format("/ip4/%s/tcp/%d/p2p/%s") % "0.0.0.0" % 40000 % libp2phost->getId().toBase58()).str();
    std::cout << "Listening on address: " << bindaddress << std::endl;
    
    auto ma = libp2p::multi::Multiaddress::create(bindaddress).value();
    auto peer_id = libp2p::peer::PeerId::fromBase58(*ma.getPeerId()).value();
    auto peerInfo = libp2p::peer::PeerInfo{peer_id, {ma}};
    
    // Start host
    libp2phost->listen(peerInfo.addresses[0]);
    libp2phost->start();
    
    // Create and initialize bitswap
    auto bitswap = std::make_shared<sgns::ipfs_bitswap::Bitswap>(*libp2phost, *event_bus, ioc);
    bitswap->initialize();
    
    // Setup bitswap logger
    auto loggerBitSwap = sgns::ipfs_bitswap::createLogger("Bitswap");
    loggerBitSwap->set_level(spdlog::level::trace);
    
    // Create work guard to keep io_context alive
    auto workGuard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
        ioc->get_executor());

    
    // ===================================================================
    // SETUP BITSWAP PROVIDER AND ASYNCIOMANAGER
    // ===================================================================
    
    // Initialize AsyncIOManager with bitswap
    FileManager::GetInstance().InitializeSingletons();
    FileManager::GetInstance().setBitswap(bitswap);
    
    // Parse file names and add IPFS providers for any IPFS URLs
    auto server_addr = libp2p::multi::Multiaddress::create("/ip4/192.168.46.124/tcp/4001/p2p/12D3KooWHsD2QEUS5FzHEyq2bTuwMSEuEvV86wVAc7VaDDKK1NwJ").value();
    auto server_peer_id = libp2p::peer::PeerId::fromBase58(*server_addr.getPeerId()).value();
    auto server_peer_info = libp2p::peer::PeerInfo{server_peer_id, {server_addr}};
    
    for (const auto& file_name : file_names) {
        std::string prefix, base, extension;
        if (getURLComponents(file_name, prefix, base, extension) && prefix == "ipfs") {
            // Parse IPFS URL to get CID
            std::string cid_str, file_component;
            if (parseIPFSUrl(base, cid_str, file_component)) {
                auto cid_result = libp2p::multi::ContentIdentifierCodec::fromString(cid_str);
                if (cid_result.has_value()) {
                    bitswap->AddProvider(cid_result.value(), server_peer_info);
                    std::cout << "Added IPFS provider for CID: " << cid_str << " (file: " << file_name << ")" << std::endl;
                } else {
                    std::cout << "Warning: Invalid CID in IPFS URL: " << file_name << std::endl;
                }
            } else {
                std::cout << "Warning: Could not parse IPFS URL: " << file_name << std::endl;
            }
        } else {
            std::cout << "Skipping non-IPFS URL: " << file_name << std::endl;
        }
    }
    
    // ===================================================================
    // LOAD FILES ASYNCHRONOUSLY
    // ===================================================================
    
    for (int i = 0; i < file_names.size(); i++) {
        std::cout << "LoadASync: " << file_names[i] << std::endl;
        
        FileManager::GetInstance().LoadASync(
            file_names[i], 
            false,  // don't parse 
            true,   // use IPFS
            ioc, 
            [file_name = file_names[i]](auto buffers) {
                if (buffers) {
                    std::cout << "Successfully loaded: " << file_name << std::endl;
                } else {
                    std::cout << "Failed to load " << file_name << ": " << buffers.error().message() << std::endl;
                }
            },
            "file"
        );
    }
    
    // Run the io_context to process async operations
    std::cout << "Starting io_context..." << std::endl;
    ioc->run();

    return 0;
}

