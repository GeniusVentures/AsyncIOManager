//IPFSCommon.cpp
#include "IPFSCommon.hpp"


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


            //Create Kademlia for DHT


            //Create DHT
            

            auto listenresult = instance_->host_->listen(ma);
            if (!listenresult)
            {
                instance_.reset();
                return listenresult.error();
            }
            instance_->host_->start();
            instance_->bitswap_->start();
            instance_->dht_->Start();
        }

        return instance_;
    }

    IPFSDevice::IPFSDevice(std::shared_ptr<boost::asio::io_context> ioc) 
    {
        // Use libp2p::injector::makeHostInjector()to create the host
        auto injector = libp2p::injector::makeHostInjector();
        host_ = injector.create<std::shared_ptr<libp2p::Host>>();

        // Initialize Bitswap using the created host
        bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>(*host_, host_->getBus(), ioc);
        //Initialize address holder
        peerAddresses_ = std::make_shared<std::vector<libp2p::multi::Multiaddress>>();

        //Make Kademlia Injector
        libp2p::protocol::kademlia::Config kademlia_config;
        kademlia_config.randomWalk.enabled = true;
        kademlia_config.randomWalk.interval = std::chrono::seconds(300);
        kademlia_config.requestConcurency = 20;

        auto dhtinjector = libp2p::injector::makeHostInjector(
            // libp2p::injector::useKeyPair(kp), // Use predefined keypair
            libp2p::injector::makeKademliaInjector(
                libp2p::injector::useKademliaConfig(kademlia_config)));
        auto kademlia =
            dhtinjector
            .create<std::shared_ptr<libp2p::protocol::kademlia::Kademlia>>();

        //Initialize DHT
        dht_ = std::make_shared<sgns::IpfsDHT>(kademlia, bootstrapAddresses_);
    }

    bool IPFSDevice::RequestBlockMain(
        std::shared_ptr<boost::asio::io_context> ioc,
        const sgns::ipfs_bitswap::CID& cid,
        int addressoffset,
        bool parse,
        bool save,
        CompletionCallback handle_read,
        StatusCallback status)
    {
        if (addressoffset < peerAddresses_->size())
        {
            auto peerId = libp2p::peer::PeerId::fromBase58(peerAddresses_->at(addressoffset).getPeerId().value()).value();
            auto address = peerAddresses_->at(addressoffset);
            bitswap_->RequestBlock({ peerId, { address } }, cid,
                [=](libp2p::outcome::result<std::string> data)
                {
                    if (data)
                    {
                        //std::cout << "Bitswap data received: " << data.value() << std::endl;
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
                            status(-14);
                            handle_read(ioc, std::make_shared<std::vector<char>>(), false, false);
                            return false;
                        }
                        //std::cout << "ContentTest" << decoder.getContent() << std::endl;
                        status(15);
                        //Start Adding to list
                        CIDInfo cidInfo(maincid.value());
                        for (size_t i = 0; i < decoder.getLinksCount(); ++i) {
                            auto subcid = libp2p::multi::ContentIdentifierCodec::decode(gsl::span((uint8_t*)decoder.getLinkCID(i).data(), decoder.getLinkCID(i).size()));
                            auto scid = libp2p::multi::ContentIdentifierCodec::fromString(libp2p::multi::ContentIdentifierCodec::toString(subcid.value()).value()).value();
                            CIDInfo::LinkedCIDInfo linkedCID(subcid.value());
                            cidInfo.linkedCIDs.push_back(linkedCID);
                            RequestBlockSub(ioc, cid, scid, 0, parse, save, handle_read, status);
                        }
                        addCID(cidInfo);
                        if (decoder.getLinksCount() <= 0)
                        {
                            auto bindata = std::make_shared<std::vector<char>>(decoder.getContent().begin()+4, decoder.getContent().end()-2);
                            std::cout << "IPFS Finish" << std::endl;
                            status(0);
                            handle_read(ioc, bindata, parse, save);
                        }
                        return true;
                    }
                    else
                    {
                        return RequestBlockMain(ioc, cid, addressoffset + 1, parse, save, handle_read, status);
                    }
                });
        }
        return false;
    }

    bool IPFSDevice::RequestBlockSub(
        std::shared_ptr<boost::asio::io_context> ioc,
        const sgns::ipfs_bitswap::CID& cid,
        const sgns::ipfs_bitswap::CID& scid,
        int addressoffset,
        bool parse,
        bool save,
        CompletionCallback handle_read,
        StatusCallback status)
    {
        if (addressoffset < peerAddresses_->size())
        {
            auto peerId = libp2p::peer::PeerId::fromBase58(peerAddresses_->at(addressoffset).getPeerId().value()).value();
            auto address = peerAddresses_->at(addressoffset);
            bitswap_->RequestBlock({ peerId, { address } }, scid,
                [=](libp2p::outcome::result<std::string> data)
                {
                    if (data)
                    {
                        //std::cout << "Bitswap subdata received: " << std::endl;
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
                            status(-15);
                            handle_read(ioc, std::make_shared<std::vector<char>>(), false, false);
                            return false;
                        }
                        
                        auto bindata = std::vector<char>(decoder.getContent().begin()+6, decoder.getContent().end()-4);
                        bool allset = setContentForLinkedCID(cid, scid, bindata);
                        if (allset)
                        {
                            auto finaldata = combineLinkedCIDs(cid);
                            std::cout << "IPFS Finish" << std::endl;
                            status(0);
                            handle_read(ioc, finaldata, parse, save);
                        }
                        
                        return true;
                    }
                    else
                    {
                        return RequestBlockSub(ioc, cid, scid, addressoffset + 1, parse, save, handle_read, status);
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
            it->setContentForLinkedCID(linkedCID, content);
            // Check if all linkedCIDs have content
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

    void IPFSDevice::addCID(const CIDInfo& cidInfo)
    {
        // Acquire lock to safely modify the list
        std::lock_guard<std::mutex> lock(mutex_);

        // Add the CIDInfo to the list
        requestedCIDs_.push_back(cidInfo);
    }

    void IPFSDevice::addAddress(
        libp2p::multi::Multiaddress address
    )
    {
        peerAddresses_->push_back(address);
    }

    std::shared_ptr<sgns::ipfs_bitswap::Bitswap> IPFSDevice::getBitswap() const {
        return bitswap_;
    }
    std::shared_ptr<libp2p::Host> IPFSDevice::getHost() const {
        return host_;
    }

    IpfsDHT::IpfsDHT(
        std::shared_ptr<libp2p::protocol::kademlia::Kademlia> kademlia,
        std::vector<std::string> bootstrapAddresses)
        : kademlia_(std::move(kademlia))
        , bootstrapAddresses_(bootstrapAddresses)
    {
    }

    void IpfsDHT::Start()
    {
        auto&& bootstrapNodes = GetBootstrapNodes();
        for (auto& bootstrap_node : bootstrapNodes) {
            std::cout << "node" << std::endl;
            kademlia_->addPeer(bootstrap_node, true);
        }

        kademlia_->start();
    }

    void IpfsDHT::FindProviders(
        const libp2p::multi::ContentIdentifier& cid,
        std::function<void(libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> onProvidersFound)> onProvidersFound)
    {
        auto kadCID = libp2p::protocol::kademlia::ContentId::fromWire(
            libp2p::multi::ContentIdentifierCodec::encode(cid).value());
        if (!kadCID)
        {
            //logger_->error("Wrong CID {}",
            //    libp2p::peer::PeerId::fromHash(cid.content_address).value().toBase58());
            std::cerr << "Wrong CID" << std::endl;
            // TODO: pass an error to callback
            //onProvidersFound(ERROR);
        }
        else
        {
            [[maybe_unused]] auto res = kademlia_->findProviders(
                kadCID.value(), 0, onProvidersFound);
        }
    }

    std::vector<libp2p::peer::PeerInfo> IpfsDHT::GetBootstrapNodes() const
    {
        std::unordered_map<libp2p::peer::PeerId,
            std::vector<libp2p::multi::Multiaddress>>
            addresses_by_peer_id;

        for (auto& address : bootstrapAddresses_) {
            auto ma = libp2p::multi::Multiaddress::create(address).value();
            auto peer_id_base58 = ma.getPeerId().value();
            auto peer_id = libp2p::peer::PeerId::fromBase58(peer_id_base58).value();

            addresses_by_peer_id[std::move(peer_id)].emplace_back(std::move(ma));
        }

        std::vector<libp2p::peer::PeerInfo> v;
        v.reserve(addresses_by_peer_id.size());
        for (auto& i : addresses_by_peer_id) {
            v.emplace_back(libp2p::peer::PeerInfo{
                /*.id =*/ i.first, /*.addresses =*/ {std::move(i.second)} });
        }

        return v;
    }

    void IpfsDHT::FindPeer(
        const libp2p::peer::PeerId& peerId,
        std::function<void(libp2p::outcome::result<libp2p::peer::PeerInfo>)> onPeerFound)
    {
        [[maybe_unused]] auto res = kademlia_->findPeer(peerId, onPeerFound);
    }
}
