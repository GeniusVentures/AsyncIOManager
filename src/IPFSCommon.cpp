//IPFSCommon.cpp
#include "IPFSCommon.hpp"
OUTCOME_CPP_DEFINE_CATEGORY_3(sgns, IPFSDevice::Error, e)
{
    switch (e)
    {
    case sgns::IPFSDevice::Error::CANNOT_DECODE:
        return "Cannot decode bitswap data";
    case sgns::IPFSDevice::Error::NO_SOURCE:
        return "Cannot decode bitswap data";
    }
    return "Unknown error";
}

namespace sgns
{
    std::shared_ptr<IPFSDevice> IPFSDevice::instance_;
    std::mutex IPFSDevice::mutex_;

    outcome::result<std::shared_ptr<IPFSDevice>> IPFSDevice::getInstance(std::shared_ptr<boost::asio::io_context> ioc) 
    {
        //Create IPFSDevice if needed
        std::lock_guard<std::mutex> lock(mutex_);

        if (!instance_) {
            instance_ = std::shared_ptr<IPFSDevice>(new IPFSDevice(ioc));
            auto ma = libp2p::multi::Multiaddress::create("/ip4/127.0.0.1/tcp/40000").value();

            //Create DHT
            auto listenresult = instance_->host_->listen(ma);
            if (!listenresult)
            {
                instance_.reset();
                return listenresult.error();
            }
            instance_->bitswap_->start();
            instance_->host_->start();
            instance_->dht_->Start();
        }

        return instance_;
    }

    outcome::result<std::shared_ptr<IPFSDevice>> IPFSDevice::createWithBitswap(
        std::shared_ptr<boost::asio::io_context> ioc,
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap)
    {
        try {
            // Create a new IPFSDevice instance (not singleton) with external bitswap
            auto device = std::shared_ptr<IPFSDevice>(new IPFSDevice(ioc, bitswap));
            return device;
        }
        catch (const std::exception& e) {
            return outcome::failure(IPFSDevice::Error::CANNOT_DECODE);
        }
    }

    IPFSDevice::IPFSDevice(std::shared_ptr<boost::asio::io_context> ioc) : dhtretry_(*ioc)
    {
        //Make Kademlia Injector
        libp2p::protocol::kademlia::Config kademlia_config;
        kademlia_config.randomWalk.enabled = true;
        kademlia_config.randomWalk.interval = std::chrono::seconds(300);
        kademlia_config.requestConcurency = 20;
        //auto injector = libp2p::injector::makeHostInjector();
        auto injector = libp2p::injector::makeHostInjector(
            // libp2p::injector::useKeyPair(kp), // Use predefined keypair
            libp2p::injector::makeKademliaInjector(
                libp2p::injector::useKademliaConfig(kademlia_config)));
        host_ = injector.create<std::shared_ptr<libp2p::Host>>();

        // Initialize Bitswap using the created host
        bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>(*host_, host_->getBus(), ioc);
        //Initialize address holder
        peerAddresses_ = std::make_shared<std::vector<libp2p::peer::PeerInfo>>();

        //Create Kademlia
        auto kademlia =
            injector
            .create<std::shared_ptr<libp2p::protocol::kademlia::Kademlia>>();

        //Initialize DHT
        dht_ = std::make_shared<sgns::ipfs_lite::ipfs::dht::IpfsDHT>(kademlia, bootstrapAddresses_,ioc);
    }

    IPFSDevice::IPFSDevice(std::shared_ptr<boost::asio::io_context> ioc, std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap) 
        : dhtretry_(*ioc), bitswap_(bitswap)
    {
        // Use external bitswap and its host
        // Extract host from bitswap (assuming bitswap has a way to get its host)
        // For now, we'll set host_ to nullptr and rely on bitswap for operations
        host_ = nullptr;
        
        //Initialize address holder
        peerAddresses_ = std::make_shared<std::vector<libp2p::peer::PeerInfo>>();
        
        // No DHT creation since we're using external bitswap
        dht_ = nullptr;
        
        m_logger->info("IPFSDevice created with external bitswap instance");
    }

    bool IPFSDevice::StartFindingPeers(
        std::shared_ptr<boost::asio::io_context> ioc,
        const sgns::ipfs_bitswap::CID& cid,
        std::string filename,
        int addressoffset,
        bool parse,
        bool save,
        CompletionCallback handle_read
    )
    {
        auto peer_id =
            libp2p::peer::PeerId::fromHash(cid.content_address).value();
        dht_->FindProviders(cid, [=](libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> res) {
            if (!res) {
                m_logger->error("Cannot find providers: {}", res.error().message());
                return false;
            }
            auto& providers = res.value();
            if (!providers.empty())
            {
                addAddresses(providers);
                
                return RequestBlockMain(ioc, cid, filename, 0, parse, save, handle_read);
            }
            else
            {
                m_logger->error("Empty provider list received");
                StartFindingPeersWithRetry(ioc, cid, filename, addressoffset, parse, save, handle_read);
                return false;
            }
            });
            //});
        return false;
    }

    void IPFSDevice::StartFindingPeersWithRetry(
        std::shared_ptr<boost::asio::io_context> ioc,
        const sgns::ipfs_bitswap::CID& cid,
        std::string filename,
        int addressoffset,
        bool parse,
        bool save,
        CompletionCallback handle_read)
    {
        boost::asio::deadline_timer dhtretry(*ioc.get());
        boost::posix_time::time_duration timeout(boost::posix_time::milliseconds(10000));
        dhtretry_.expires_from_now(timeout);
        dhtretry_.async_wait([ioc, cid, filename, addressoffset, parse, save, handle_read, this](const boost::system::error_code& ec) {
            if (!ec) {
                // Timer expired, call StartFindingPeers again with captured parameters
                this->StartFindingPeers(ioc, cid, filename, addressoffset, parse, save, handle_read);
            }
            else {
                // Handle error
                m_logger->error("Error: {}", ec.message());
            }
            });
    }

    bool IPFSDevice::RequestBlockMain(
        std::shared_ptr<boost::asio::io_context> ioc,
        const sgns::ipfs_bitswap::CID& cid,
        std::string filename,
        int addressoffset,
        bool parse,
        bool save,
        CompletionCallback handle_read)
    {
        // Check if we have any peers to connect to
        if (peerAddresses_->empty()) {
            m_logger->error("No peer addresses available for request");
            boost::asio::post(*ioc, [handle_read, ioc]() {
                handle_read(ioc, outcome::failure(Error::NO_SOURCE), false, false);
            });
            return false;
        }

        // Use modern bitswap RequestContent instead of manual block handling
        if (addressoffset < peerAddresses_->size()) {
            auto& peerInfo = peerAddresses_->at(addressoffset);
            
            m_logger->info("Requesting content for CID: {} from peer: {}", 
                libp2p::multi::ContentIdentifierCodec::toString(cid).value(),
                peerInfo.id.toBase58());
            
            bitswap_->RequestContent(peerInfo, cid,
                [=](libp2p::outcome::result<sgns::ipfs_bitswap::UnixFSContent> contentResult) {
                    if (!contentResult) {
                        m_logger->error("Failed to retrieve content: {}", contentResult.error().message());
                        
                        // Try next peer if available
                        if (addressoffset + 1 < peerAddresses_->size()) {
                            m_logger->info("Trying next peer (offset {})", addressoffset + 1);
                            RequestBlockMain(ioc, cid, filename, addressoffset + 1, parse, save, handle_read);
                        } else {
                            // No more peers to try
                            boost::asio::post(*ioc, [handle_read, ioc]() {
                                handle_read(ioc, outcome::failure(Error::CANNOT_DECODE), false, false);
                            });
                        }
                        return;
                    }
                    
                    // Convert UnixFSContent to AsyncIOManager format
                    auto unixfsContent = contentResult.value();
                    convertUnixFSContentToResult(ioc, unixfsContent, filename, parse, save, handle_read);
                }
            );
            return true;
        }
        
        m_logger->error("Peer address offset {} out of range (size: {})", addressoffset, peerAddresses_->size());
        boost::asio::post(*ioc, [handle_read, ioc]() {
            handle_read(ioc, outcome::failure(Error::NO_SOURCE), false, false);
        });
        return false;
    }

    void IPFSDevice::convertUnixFSContentToResult(
        std::shared_ptr<boost::asio::io_context> ioc,
        const sgns::ipfs_bitswap::UnixFSContent& unixfsContent,
        const std::string& filename,
        bool parse,
        bool save,
        CompletionCallback handle_read)
    {
        try {
            // Convert UnixFSContent to the expected AsyncIOManager format
            auto paths = std::make_shared<std::vector<std::string>>();
            auto contents = std::make_shared<std::vector<std::vector<char>>>();
            
            if (unixfsContent.type == sgns::ipfs_bitswap::UnixFSContent::SINGLE_FILE) {
                // Single file case
                if (!unixfsContent.files.empty()) {
                    paths->push_back(filename.empty() ? unixfsContent.files[0].path : filename);
                    contents->push_back(unixfsContent.files[0].content);
                }
            } else {
                // Directory or multi-file archive case
                for (const auto& file : unixfsContent.files) {
                    paths->push_back(file.path);
                    contents->push_back(file.content);
                }
            }
            
            auto result = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(
                std::make_pair(*paths, *contents));
            
            m_logger->info("Successfully converted UnixFS content: {} files, total size: {}",
                paths->size(), unixfsContent.metadata.count("total_size") ? unixfsContent.metadata.at("total_size") : "unknown");
            
            boost::asio::post(*ioc, [handle_read, ioc, result, parse, save]() {
                handle_read(ioc, outcome::success(result), parse, save);
            });
            
        } catch (const std::exception& e) {
            m_logger->error("Error converting UnixFS content: {}", e.what());
            boost::asio::post(*ioc, [handle_read, ioc, parse, save]() {
                handle_read(ioc, outcome::failure(Error::CANNOT_DECODE), parse, save);
            });
        }
    }

    void IPFSDevice::addAddress(
        libp2p::multi::Multiaddress address
    )
    {
        std::vector<libp2p::multi::Multiaddress> addresses;
        addresses.push_back(address);
        auto peerId = libp2p::peer::PeerId::fromBase58(address.getPeerId().value());
        auto peerInfo = sgns::Peer{
            libp2p::peer::PeerInfo{peerId.value(), std::move(addresses)}
        };
        peerAddresses_->push_back(peerInfo.info);
    }

    void IPFSDevice::addAddresses(const std::vector<libp2p::peer::PeerInfo>& addresses) {
        peerAddresses_->insert(peerAddresses_->end(), addresses.begin(), addresses.end());
    }

    std::shared_ptr<sgns::ipfs_bitswap::Bitswap> IPFSDevice::getBitswap() const {
        return bitswap_;
    }
    std::shared_ptr<libp2p::Host> IPFSDevice::getHost() const {
        return host_;
    }
}
