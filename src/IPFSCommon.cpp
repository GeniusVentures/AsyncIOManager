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

    IPFS::outcome::result<std::shared_ptr<IPFSDevice>> IPFSDevice::getInstance(std::shared_ptr<boost::asio::io_context> ioc) 
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
                std::cerr << "Cannot find providers: " << res.error().message() << std::endl;
                return false;
            }
            std::cout << "Providers: " << std::endl;
            auto& providers = res.value();
            if (!providers.empty())
            {
                addAddresses(providers);
                //for (auto& provider : providers) {
                //    std::cout << provider.id.toBase58() << std::endl;
                //    auto providerid = provider.id.toBase58();

                    //for (const auto& address : provider.addresses) {

                        // Assuming addAddress function accepts a multiaddress as argument
                        //bool hasPeerId = address.hasProtocol(libp2p::multi::Protocol::Code::P2P);
                        //if (hasPeerId) {
                        //    std::cout << "Address: " << address.getStringAddress() << std::endl;
                        //    addAddress(address);
                        //}
                    //}
                //}
                
                return RequestBlockMain(ioc, cid, filename, 0, parse, save, handle_read);
            }
            else
            {
                std::cout << "Empty providers list received" << std::endl;
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
                std::cout << "Error: " << ec.message() << std::endl;
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
        //std::cout << "request main block" << filename << std::endl;
        if (addressoffset < peerAddresses_->size())
        {
            bitswap_->RequestBlock(peerAddresses_->at(addressoffset), cid,
                [=](libp2p::outcome::result<std::string> data)
                {
                    if (data)
                    {
                        auto cidV0 = libp2p::multi::ContentIdentifierCodec::encodeCIDV0(data.value().data(), data.value().size());
                        auto maincid = libp2p::multi::ContentIdentifierCodec::decode(gsl::span((uint8_t*)cidV0.data(), cidV0.size()));

                        //Convert data content into usable span uint8_t
                        gsl::span<const uint8_t> byteSpan(
                            reinterpret_cast<const uint8_t*>(data.value().data()),
                            data.value().size());
                        //Create a PB Decoder to handle the data
                        auto decoder = ipfs_lite::ipld::IPLDNodeDecoderPB();
                        //Attempt to decode
                        auto diddecode = decoder.decode(byteSpan);
                        if (diddecode.has_error())
                        {
                            //Handle Error
                            handle_read(ioc, outcome::failure(Error::CANNOT_DECODE), false, false);
                            return false;
                        }
                        //std::cout << "ContentTest" << decoder.getContent() << std::endl;
                        //Start Adding to list
                        CIDInfo cidInfo(maincid.value());
                        for (size_t i = 0; i < decoder.getLinksCount(); ++i) {
                            auto subcid = libp2p::multi::ContentIdentifierCodec::decode(gsl::span((uint8_t*)decoder.getLinkCID(i).data(), decoder.getLinkCID(i).size()));
                            auto scid = libp2p::multi::ContentIdentifierCodec::fromString(libp2p::multi::ContentIdentifierCodec::toString(subcid.value()).value()).value();
                            //If we have a link name, this is a file CID, and not a linked CID for that file
                            //If we don't have a link name, this is a linked CID. This shouldn't happen here in the main request though.
                            std::string passfilename = filename;
                            if (!decoder.getLinkName(i).empty())
                            {
                                cidInfo.directories.push_back(decoder.getLinkName(i));
                                cidInfo.mainCIDs.push_back(subcid.value());
                                passfilename = decoder.getLinkName(i);
                            }
                            else
                            {
                                CIDInfo::LinkedCIDInfo linkedCID(subcid.value(), maincid.value(), passfilename);
                                std::cout << "add Linked CID: nothing here" << std::endl;
                                cidInfo.linkedCIDs.push_back(linkedCID); 
                            }
                            //Increment Outstanding
                            cidInfo.outstandingRequests_++;
                            //Request Additional CID
                            RequestBlockSub(ioc, cid, cid, scid, passfilename, 0, parse, save, handle_read);
                        }

                        //Add to list in IPFSDevice
                        addCID(cidInfo);

                        //If there are no links, this was a single file with 1 block containing all the data, so we can write it out
                        if (decoder.getLinksCount() <= 0)
                        {
                            //Get data, ignoring bytes at beginning or end TODO: need a better way to do this, some contexts the offset is not 6/4.
                            //auto bindata = std::make_shared<std::vector<char>>(decoder.getContent().begin() + 4, decoder.getContent().end() - 2);
                            ::unixfs_pb::Data unixfs;
                            //unixfs.set_data(decoder.getContent());
                            unixfs.ParseFromString(decoder.getContent());
                            auto bindata = std::vector<char>(unixfs.data().begin(), unixfs.data().end());
                            std::cout << "REQCIDS: " << requestedCIDs_.size() << std::endl;
                            std::string passfilename = filename;
                            size_t mainindex = findRequestedCIDIndex(cid);
                            requestedCIDs_[mainindex].finalcontents->first.push_back(passfilename);
                            requestedCIDs_[mainindex].finalcontents->second.push_back(bindata);
                            //bool allset = CheckIfAllSet(cid);
                            if (requestedCIDs_[mainindex].outstandingRequests_ <= 0)
                            {
                                requestedCIDs_[mainindex].groupLinkedCIDs();
                                handle_read(ioc, requestedCIDs_[mainindex].finalcontents, parse, save);
                            }
                        }
                        return true;
                    }
                    else
                    {
                        return RequestBlockMain(ioc, cid, filename, addressoffset + 1, parse, save, handle_read);
                    }
                });
        }
        else {
            handle_read(ioc, outcome::failure(Error::NO_SOURCE), false, false);
            return false;
        }
        return false;
    }

    bool IPFSDevice::RequestBlockSub(
        std::shared_ptr<boost::asio::io_context> ioc,
        const sgns::ipfs_bitswap::CID& cid,
        const sgns::ipfs_bitswap::CID& parentcid,
        const sgns::ipfs_bitswap::CID& scid,
        std::string directory,
        int addressoffset,
        bool parse,
        bool save,
        CompletionCallback handle_read)
    {
        //std::cout << "directory: " << directory << std::endl;
        if (addressoffset < peerAddresses_->size())
        {
            bitswap_->RequestBlock(peerAddresses_->at(addressoffset), scid,
                [=](libp2p::outcome::result<std::string> data)
                {
                    if (data)
                    {
                        //Get CIDInfo Index
                        size_t mainindex = findRequestedCIDIndex(cid);
                        //Decrement 
                        requestedCIDs_[mainindex].outstandingRequests_--;


                        auto cidV0 = libp2p::multi::ContentIdentifierCodec::encodeCIDV0(data.value().data(), data.value().size());
                        auto maincid = libp2p::multi::ContentIdentifierCodec::decode(gsl::span((uint8_t*)cidV0.data(), cidV0.size()));

                        //Convert data content into usable span uint8_t
                        gsl::span<const uint8_t> byteSpan(
                            reinterpret_cast<const uint8_t*>(data.value().data()),
                            data.value().size());

                        //Create a PB Decoder to handle the data
                        auto decoder = ipfs_lite::ipld::IPLDNodeDecoderPB();
                        
                        
                        //Attempt to decode
                        auto diddecode = decoder.decode(byteSpan);
                        if (diddecode.has_error())
                        {
                            //Handle Error
                            handle_read(ioc, outcome::failure(Error::CANNOT_DECODE), false, false);
                            return false;
                        }
                        for (size_t i = 0; i < decoder.getLinksCount(); ++i) {
                            auto subcid = libp2p::multi::ContentIdentifierCodec::decode(gsl::span((uint8_t*)decoder.getLinkCID(i).data(), decoder.getLinkCID(i).size()));
                            auto sscid = libp2p::multi::ContentIdentifierCodec::fromString(libp2p::multi::ContentIdentifierCodec::toString(subcid.value()).value()).value();
                            std::string newdir = directory + "/" + decoder.getLinkName(i);
                            if (!decoder.getLinkName(i).empty())
                            {
                                requestedCIDs_[mainindex].directories.push_back(newdir);
                                requestedCIDs_[mainindex].mainCIDs.push_back(subcid.value());
                            }
                            else {
                                newdir = directory;
                                CIDInfo::LinkedCIDInfo linkedCID(subcid.value(), scid, newdir);
                                requestedCIDs_[mainindex].linkedCIDs.push_back(linkedCID);
                            }
                            requestedCIDs_[mainindex].outstandingRequests_++;
                            RequestBlockSub(ioc, cid, scid, sscid, newdir, 0, parse, save, handle_read);
                        }
                        //If there are no links, this block is complete and we can see if we have all blocks for writing
                        if (decoder.getLinksCount() <= 0)
                        {
                            //Get data, ignoring bytes at beginning or end TODO: need a better way to do this, some contexts the offset is not 6/4.
                            ::unixfs_pb::Data unixfs;
                            unixfs.ParseFromString(decoder.getContent());
                            auto bindata = std::vector<char>(unixfs.data().begin(), unixfs.data().end());
                            //Set Content for linked CID, or otherwise push data to final contents if it has none
                            bool setsubdata = setContentForLinkedCID(cid, scid, bindata);
                            if (!setsubdata)
                            {
                                requestedCIDs_[mainindex].finalcontents->first.push_back(directory);
                                requestedCIDs_[mainindex].finalcontents->second.push_back(bindata);
                            }
                            //bool allset = CheckIfAllSet(cid);
                            if (requestedCIDs_[mainindex].outstandingRequests_ <= 0)
                            {
                                requestedCIDs_[mainindex].groupLinkedCIDs();
                                //requestedCIDs_[mainindex].writeFinalContentsToDirectories();
                                //std::cout << "IPFS Finish" << std::endl;
                                handle_read(ioc, requestedCIDs_[mainindex].finalcontents, parse, save);
                            }
                        }


                        return true;
                    }
                    else
                    {
                        //Request Block on next address
                        return RequestBlockSub(ioc, cid, parentcid, scid, directory, addressoffset + 1, parse, save, handle_read);
                    }
                });
        }
        return false;
    }

    bool IPFSDevice::setContentForLinkedCID(const sgns::ipfs_bitswap::CID& mainCID,
        const sgns::ipfs_bitswap::CID& linkedCID,
        const std::vector<char>& content)
    {
        auto it = std::find_if(requestedCIDs_.begin(), requestedCIDs_.end(),
            [&mainCID](const CIDInfo& info) {
                return info.mainCID == mainCID;
            });

        if (it != requestedCIDs_.end())
        {
            // Update the content for the linked CID within the found CIDInfo
            return it->setContentForLinkedCID(linkedCID, content);
        }
        return false;
    }

    bool IPFSDevice::CheckIfAllSet(const sgns::ipfs_bitswap::CID& mainCID)
    {
        auto it = std::find_if(requestedCIDs_.begin(), requestedCIDs_.end(),
            [&mainCID](const CIDInfo& info) {
                return info.mainCID == mainCID;
            });
        if (it != requestedCIDs_.end())
        {
            return it->allLinkedCIDsHaveContent();
        }
        return false;
    }

    std::shared_ptr<std::vector<char>> IPFSDevice::combineLinkedCIDs(const sgns::ipfs_bitswap::CID& mainCID)
    {
        auto it = std::find_if(requestedCIDs_.begin(), requestedCIDs_.end(),
            [&mainCID](const CIDInfo& info) {
                return info.mainCID == mainCID;
            });
        auto combinedContent = std::make_shared<std::vector<char>>();
        if (it != requestedCIDs_.end())
        {
            // Get the combined content
            combinedContent = it->combineContents();
        }
        return combinedContent;
    }

    size_t IPFSDevice::addCID(CIDInfo& cidInfo)
    {
        // Acquire lock to safely modify the list
        std::lock_guard<std::mutex> lock(mutex_);

        // Add the CIDInfo to the list
        requestedCIDs_.push_back(std::move(cidInfo));

        return requestedCIDs_.size() - 1;
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
