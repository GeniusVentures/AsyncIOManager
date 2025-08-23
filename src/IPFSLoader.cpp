#include <sstream>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>
#include "FileManager.hpp"
#include "IPFSLoader.hpp"
#include "URLStringUtil.h"
#include "logger.hpp"
#include <bitswap.hpp>
#include <boost/asio/io_context.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/protocol/identify/identify.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/protocol/ping/ping.hpp>

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns, IPFSLoader::Error, e)
{
    switch (e)
    {
    case sgns::IPFSLoader::Error::CANNOT_LISTEN:
        return "Cannot listen on address";
    case sgns::IPFSLoader::Error::BAD_CID:
        return "IPFS CID is invalid";
    }
    return "Unknown error";
}

namespace sgns
{
    using libp2p::Host;
    //using sgns::ipfspeer;
    IPFSLoader* IPFSLoader::_instance = nullptr;
    void IPFSLoader::InitializeSingleton() {
        if (_instance == nullptr) {
            _instance = new IPFSLoader();
        }
    }
    IPFSLoader::IPFSLoader()
    {
        FileManager::GetInstance().RegisterLoader("ipfs", this);
    }

    std::shared_ptr<void> IPFSLoader::LoadFile(std::string filename)
    {
        std::shared_ptr<string> result = std::make_shared < string>("init");
        return result;
    }
    std::shared_ptr<libp2p::protocol::PingClientSession> pingSession_;

    void OnSessionPing(libp2p::outcome::result<std::shared_ptr<libp2p::protocol::PingClientSession>> session)
    {
        if (session)
        {
            pingSession_ = std::move(session.value());
        }
    }

    void OnNewConnection(
        const std::weak_ptr<libp2p::connection::CapableConnection>& conn,
        std::shared_ptr<libp2p::protocol::Ping> ping) {
        if (conn.expired()) {
            return;
        }
        auto sconn = conn.lock();
        ping->startPinging(sconn, &OnSessionPing);
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
        level: critical
        children:
          - name: libp2p
          - name: kademlia
    # ----------------
      )");

    std::shared_ptr<void> IPFSLoader::LoadASync(std::string filename, bool parse, bool save, std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback handle_read)
    {
        auto logging_system = std::make_shared<soralog::LoggingSystem>(
            std::make_shared<soralog::ConfiguratorFromYAML>(
                // Original LibP2P logging config
                std::make_shared<libp2p::log::Configurator>(),
                // Additional logging config for application
                logger_config));
        auto r = logging_system->configure();
        libp2p::log::setLoggingSystem(logging_system);

        auto loggerIdentifyMsgProcessor = libp2p::log::createLogger("IdentifyMsgProcessor");
        loggerIdentifyMsgProcessor->setLevel(soralog::Level::OFF);
        auto loggerProcessingEngine = sgns::ipfs_bitswap::createLogger("Bitswap");
        loggerProcessingEngine->set_level(spdlog::level::off);
        std::shared_ptr<string> result = std::make_shared < string>("init");

        //Get CID and Filename
        std::string ipfs_cid;
        std::string ipfs_file;
        parseIPFSUrl(filename, ipfs_cid, ipfs_file);
        //Create Host
        auto ipfsDeviceResult = IPFSDevice::getInstance(ioc);
        if (!ipfsDeviceResult)
        {   
            //Error Listening
            m_logger->error("Cannot listen to address: {}", ipfsDeviceResult.error().message());
            boost::asio::post(*ioc, [handle_read, ioc]() {
                handle_read(ioc, outcome::failure(Error::CANNOT_LISTEN), false, false);
                });
            return result;
        }
        auto ipfsDevice = ipfsDeviceResult.value();
        //auto ma = libp2p::multi::Multiaddress::create("/ip4/127.0.0.1/tcp/40000").value();
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/ip4/3.92.45.153/tcp/4001/p2p/12D3KooWP6R6XVCBK7t76o8VDwZdxpzAqVeDtHYQNmntP2y8NHvK").value());
        ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/ip4/127.0.0.1/tcp/4001/p2p/12D3KooWFMdNiBFk5ojGNzWjqSTL1HGLu8rXns5kwqUPTrbFNtEN").value());
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/dnsaddr/fra1-1.hostnodes.pinata.cloud/ipfs/QmWaik1eJcGHq1ybTWe7sezRfqKNcDRNkeBaLnGwQJz1Cj").value());
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/dnsaddr/fra1-2.hostnodes.pinata.cloud/ipfs/QmNfpLrQQZr5Ns9FAJKpyzgnDL2GgC6xBug1yUZozKFgu4").value());
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/dnsaddr/fra1-3.hostnodes.pinata.cloud/ipfs/QmPo1ygpngghu5it8u4Mr3ym6SEU2Wp2wA66Z91Y1S1g29").value());
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/dnsaddr/nyc1-1.hostnodes.pinata.cloud/ipfs/QmRjLSisUCHVpFa5ELVvX3qVPfdxajxWJEHs9kN3EcxAW6").value());
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/dnsaddr/nyc1-2.hostnodes.pinata.cloud/ipfs/QmPySsdmbczdZYBpbi2oq2WMJ8ErbfxtkG8Mo192UHkfGP").value());
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/dnsaddr/nyc1-3.hostnodes.pinata.cloud/ipfs/QmSarArpxemsPESa6FNkmuu9iSE1QWqPX2R3Aw6f5jq4D5").value());
        //CID of File
        auto maybe_cid = libp2p::multi::ContentIdentifierCodec::fromString(ipfs_cid);
        if (!maybe_cid)
        {
            m_logger->error("Bad CID: {}", maybe_cid.error().message());
            boost::asio::post(*ioc, [handle_read, ioc]() {
                handle_read(ioc, outcome::failure(Error::BAD_CID), false, false);
                });
            return result;
        }
        auto cid = maybe_cid.value();
        ioc->post([=] {
            ipfsDevice->RequestBlockMain(ioc, cid, ipfs_file, 0, parse, save, handle_read);
            //ipfsDevice->StartFindingPeers(ioc, cid, ipfs_file, 0, parse, save, handle_read, status);
            });
        
        return result;
    }

} // End namespace sgns
