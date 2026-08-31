//IPFSCommon.cpp
#include "IPFSCommon.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, IPFSDevice::Error, e )
{
    switch ( e )
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
    std::mutex                  IPFSDevice::mutex_;

    outcome::result<std::shared_ptr<IPFSDevice>> IPFSDevice::getInstance( std::shared_ptr<boost::asio::io_context> ioc )
    {
        //Create IPFSDevice if needed
        std::lock_guard<std::mutex> lock( mutex_ );

        if ( !instance_ )
        {
            instance_ = std::shared_ptr<IPFSDevice>( new IPFSDevice( ioc ) );
            auto ma   = libp2p::multi::Multiaddress::create( "/ip4/127.0.0.1/tcp/40000" ).value();

            //Create DHT
            auto listenresult = instance_->host_->listen( ma );
            if ( !listenresult )
            {
                instance_.reset();
                return listenresult.error();
            }
            instance_->bitswap_->start();
            instance_->host_->start();
            BOOST_OUTCOME_TRY(instance_->dht_->Start());
        }

        return instance_;
    }

    outcome::result<std::shared_ptr<IPFSDevice>> IPFSDevice::createWithBitswap(
        std::shared_ptr<boost::asio::io_context>          ioc,
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap>      bitswap,
        std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht )
    {
        try
        {
            // Create a new IPFSDevice instance (not singleton) with external bitswap
            auto device = std::shared_ptr<IPFSDevice>( new IPFSDevice( ioc, bitswap, dht ) );
            return device;
        }
        catch ( const std::exception &e )
        {
            return outcome::failure( IPFSDevice::Error::CANNOT_DECODE );
        }
    }

    IPFSDevice::IPFSDevice( std::shared_ptr<boost::asio::io_context> ioc ) : dhtretry_( *ioc )
    {
        //Make Kademlia Injector
        libp2p::protocol::kademlia::Config kademlia_config;
        kademlia_config.randomWalk.enabled  = true;
        kademlia_config.randomWalk.interval = std::chrono::seconds( 300 );
        kademlia_config.requestConcurency   = 20;
        //auto injector = libp2p::injector::makeHostInjector();
        auto injector = libp2p::injector::makeHostInjector(
            // libp2p::injector::useKeyPair(kp), // Use predefined keypair
            libp2p::injector::makeKademliaInjector( libp2p::injector::useKademliaConfig( kademlia_config ) ) );
        host_ = injector.create<std::shared_ptr<libp2p::Host>>();

        // Initialize Bitswap using the created host
        bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>( *host_, host_->getBus(), ioc );

        //Create Kademlia
        auto kademlia = injector.create<std::shared_ptr<libp2p::protocol::kademlia::Kademlia>>();

        //Initialize DHT
        dht_ = std::make_shared<sgns::ipfs_lite::ipfs::dht::IpfsDHT>( kademlia, bootstrapAddresses_, ioc );
    }

    IPFSDevice::IPFSDevice( std::shared_ptr<boost::asio::io_context>          ioc,
                            std::shared_ptr<sgns::ipfs_bitswap::Bitswap>      bitswap,
                            std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht ) :
        dhtretry_( *ioc ), bitswap_( bitswap ), dht_( dht )
    {
        // Use external bitswap and its host
        // Extract host from bitswap (assuming bitswap has a way to get its host)
        // For now, we'll set host_ to nullptr and rely on bitswap for operations
        host_ = nullptr;

        // Use the provided external DHT (may be null) - no DHT creation since
        // we're using the external bitswap
        m_logger->info( "IPFSDevice created with external bitswap instance{}",
                        dht_ ? " and external DHT" : "" );
    }

    bool IPFSDevice::StartFindingPeers( std::shared_ptr<boost::asio::io_context> ioc,
                                        const sgns::ipfs_bitswap::CID           &cid,
                                        std::string                              filename,
                                        int                                      addressoffset,
                                        bool                                     parse,
                                        bool                                     save,
                                        CompletionCallback                       handle_read )
    {
        if ( !dht_ )
        {
            // No DHT available - fall back to a direct bitswap request
            m_logger->warn( "No DHT available, skipping peer discovery for CID" );
            return RequestBlockMain( ioc, cid, filename, addressoffset, parse, save, handle_read );
        }

        if ( !bitswap_->GetProviders( cid ).empty() )
        {
            // A seed provider is registered for this CID - the registry is
            // authoritative, skip DHT discovery entirely
            m_logger->info( "Seed provider registered for CID, skipping DHT discovery" );
            return RequestBlockMain( ioc, cid, filename, addressoffset, parse, save, handle_read );
        }

        auto peer_id = libp2p::peer::PeerId::fromHash( cid.content_address ).value();
        // Capture shared_ptr to keep this object alive during callback
        auto self = shared_from_this();
        auto findResult = dht_->FindProviders(
            cid,
            [self, ioc, cid, filename, addressoffset, parse, save, handle_read]( libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> res )
            {
                if ( !res )
                {
                    self->m_logger->error( "Cannot find providers: {}", res.error().message() );
                    return false;
                }
                auto &providers = res.value();
                if ( !providers.empty() )
                {
                    // Convert PeerInfo vector to Multiaddress vector and add providers for this CID
                    std::vector<libp2p::multi::Multiaddress> addresses;
                    for ( const auto &provider : providers )
                    {
                        if ( !provider.addresses.empty() )
                        {
                            addresses.insert( addresses.end(), provider.addresses.begin(), provider.addresses.end() );
                        }
                    }
                    self->addAddresses( cid, addresses );

                    return self->RequestBlockMain( ioc, cid, filename, 0, parse, save, handle_read );
                }
                else
                {
                    self->m_logger->error( "Empty provider list received" );
                    self->StartFindingPeersWithRetry( ioc, cid, filename, addressoffset, parse, save, handle_read );
                    return false;
                }
            } );
        if ( !findResult )
        {
            m_logger->error( "Failed to start FindProviders: {}", findResult.error().message() );
        }
        //});
        return false;
    }

    void IPFSDevice::StartFindingPeersWithRetry( std::shared_ptr<boost::asio::io_context> ioc,
                                                 const sgns::ipfs_bitswap::CID           &cid,
                                                 std::string                              filename,
                                                 int                                      addressoffset,
                                                 bool                                     parse,
                                                 bool                                     save,
                                                 CompletionCallback                       handle_read )
    {
        boost::posix_time::time_duration timeout( boost::posix_time::milliseconds( 10000 ) );
        dhtretry_.expires_from_now( timeout );
        // Capture shared_ptr to keep this object alive during callback
        auto self = shared_from_this();
        dhtretry_.async_wait(
            [ioc, cid, filename, addressoffset, parse, save, handle_read, self]( const boost::system::error_code &ec )
            {
                if ( !ec )
                {
                    // Timer expired, call StartFindingPeers again with captured parameters
                    self->StartFindingPeers( ioc, cid, filename, addressoffset, parse, save, handle_read );
                }
                else
                {
                    // Handle error
                    self->m_logger->error( "Error: {}", ec.message() );
                }
            } );
    }

    bool IPFSDevice::RequestBlockMain( std::shared_ptr<boost::asio::io_context> ioc,
                                       const sgns::ipfs_bitswap::CID           &cid,
                                       std::string                              filename,
                                       int                                      addressoffset,
                                       bool                                     parse,
                                       bool                                     save,
                                       CompletionCallback                       handle_read )
    {
        m_logger->info( "Requesting content for CID: {}",
                        libp2p::multi::ContentIdentifierCodec::toString( cid ).value() );

        // Capture shared_ptr to keep this object alive during callback
        auto self = shared_from_this();
        bitswap_->RequestContent(
            cid,
            [self, ioc, filename, parse, save, handle_read](
                libp2p::outcome::result<sgns::ipfs_bitswap::UnixFSContent> contentResult )
            {
                if ( !contentResult )
                {
                    self->m_logger->error( "Failed to retrieve content: {}", contentResult.error().message() );
                    boost::asio::post( *ioc,
                                       [handle_read, ioc]() {
                                           handle_read( ioc, outcome::failure( Error::CANNOT_DECODE ), false, false );
                                       } );
                    return;
                }

                // Convert UnixFSContent to AsyncIOManager format
                auto unixfsContent = contentResult.value();
                self->convertUnixFSContentToResult( ioc, unixfsContent, filename, parse, save, handle_read );
            } );
        return true;
    }

    void IPFSDevice::convertUnixFSContentToResult( std::shared_ptr<boost::asio::io_context> ioc,
                                                   const sgns::ipfs_bitswap::UnixFSContent &unixfsContent,
                                                   const std::string                       &filename,
                                                   bool                                     parse,
                                                   bool                                     save,
                                                   CompletionCallback                       handle_read )
    {
        try
        {
            // Convert UnixFSContent to the expected AsyncIOManager format
            auto paths    = std::make_shared<std::vector<std::string>>();
            auto contents = std::make_shared<std::vector<std::vector<char>>>();

            if ( unixfsContent.type == sgns::ipfs_bitswap::UnixFSContent::SINGLE_FILE )
            {
                // Single file case
                if ( !unixfsContent.files.empty() )
                {
                    paths->push_back( filename.empty() ? unixfsContent.files[0].path : filename );
                    contents->push_back( unixfsContent.files[0].content );
                }
            }
            else
            {
                // Directory or multi-file archive case
                for ( const auto &file : unixfsContent.files )
                {
                    paths->push_back( file.path );
                    contents->push_back( file.content );
                }
            }

            auto result = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(
                std::make_pair( *paths, *contents ) );

            std::string totalSize = "unknown";
            if ( unixfsContent.metadata.count( "total_size" ) > 0 )
            {
                totalSize = unixfsContent.metadata.at( "total_size" );
            }
            m_logger->info( "Successfully converted UnixFS content: {} files, total size: {}",
                            static_cast<size_t>( paths->size() ),
                            totalSize );

            boost::asio::post( *ioc,
                               [handle_read, ioc, result, parse, save]()
                               { handle_read( ioc, outcome::success( result ), parse, save ); } );
        }
        catch ( const std::exception &e )
        {
            m_logger->error( "Error converting UnixFS content: {}", e.what() );
            boost::asio::post( *ioc,
                               [handle_read, ioc, parse, save]()
                               { handle_read( ioc, outcome::failure( Error::CANNOT_DECODE ), parse, save ); } );
        }
    }

    void IPFSDevice::addAddress( const sgns::ipfs_bitswap::CID &cid, const libp2p::multi::Multiaddress &address )
    {
        // Extract peer ID from multiaddress
        auto peer_id_bytes = address.getPeerId();
        if ( !peer_id_bytes )
        {
            m_logger->error( "Failed to extract peer ID from multiaddress: {}", address.getStringAddress() );
            return;
        }

        auto peer_id = libp2p::peer::PeerId::fromBase58( peer_id_bytes.value() );
        if ( !peer_id )
        {
            m_logger->error( "Failed to create PeerID from base58 string" );
            return;
        }

        // Create PeerInfo with constructor
        libp2p::peer::PeerInfo peerInfo{ peer_id.value(), { address } };

        // Add provider to bitswap
        bitswap_->AddProvider( cid, peerInfo );

        m_logger->info( "Added provider for CID: {}, Peer: {}",
                        libp2p::multi::ContentIdentifierCodec::toString( cid ).value(),
                        peerInfo.id.toBase58() );
    }

    void IPFSDevice::addAddresses( const sgns::ipfs_bitswap::CID                  &cid,
                                   const std::vector<libp2p::multi::Multiaddress> &addresses )
    {
        std::vector<libp2p::peer::PeerInfo> peerInfos;

        for ( const auto &address : addresses )
        {
            // Extract peer ID from multiaddress
            auto peer_id_bytes = address.getPeerId();
            if ( !peer_id_bytes )
            {
                m_logger->warn( "Failed to extract peer ID from multiaddress: {}", address.getStringAddress() );
                continue;
            }

            auto peer_id = libp2p::peer::PeerId::fromBase58( peer_id_bytes.value() );
            if ( !peer_id )
            {
                m_logger->warn( "Failed to create PeerID from base58 for address: {}", address.getStringAddress() );
                continue;
            }

            // Create PeerInfo with constructor
            libp2p::peer::PeerInfo peerInfo{ peer_id.value(), { address } };
            peerInfos.push_back( peerInfo );
        }

        // Add all providers to bitswap
        bitswap_->AddProviders( cid, peerInfos );

        m_logger->info( "Added {} providers for CID: {}",
                        peerInfos.size(),
                        libp2p::multi::ContentIdentifierCodec::toString( cid ).value() );
    }

    std::shared_ptr<sgns::ipfs_bitswap::Bitswap> IPFSDevice::getBitswap() const
    {
        return bitswap_;
    }

    std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> IPFSDevice::getDHT() const
    {
        return dht_;
    }

    std::shared_ptr<libp2p::Host> IPFSDevice::getHost() const
    {
        return host_;
    }
}
