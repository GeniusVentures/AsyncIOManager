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

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, IPFSLoader::Error, e )
{
    switch ( e )
    {
        case sgns::IPFSLoader::Error::CANNOT_LISTEN:
            return "Cannot listen on address";
        case sgns::IPFSLoader::Error::BAD_CID:
            return "IPFS CID is invalid";
        case sgns::IPFSLoader::Error::INVALID_URL:
            return "Invalid URL";
    }
    return "Unknown error";
}

namespace sgns
{
    using libp2p::Host;
    //using sgns::ipfspeer;
    IPFSLoader *IPFSLoader::_instance = nullptr;

    void IPFSLoader::InitializeSingleton()
    {
        if ( _instance == nullptr )
        {
            _instance = new IPFSLoader();
        }
    }

    IPFSLoader::IPFSLoader()
    {
        FileManager::GetInstance().RegisterLoader( "ipfs", this );
    }

    std::shared_ptr<void> IPFSLoader::LoadFile( std::string filename )
    {
        std::shared_ptr<string> result = std::make_shared<string>( "init" );
        return result;
    }

    std::shared_ptr<libp2p::protocol::PingClientSession> pingSession_;

    void OnSessionPing( libp2p::outcome::result<std::shared_ptr<libp2p::protocol::PingClientSession>> session )
    {
        if ( session )
        {
            pingSession_ = std::move( session.value() );
        }
    }

    void OnNewConnection( const std::weak_ptr<libp2p::connection::CapableConnection> &conn,
                          std::shared_ptr<libp2p::protocol::Ping>                     ping )
    {
        if ( conn.expired() )
        {
            return;
        }
        auto sconn = conn.lock();
        ping->startPinging( sconn, &OnSessionPing );
    }

    const std::string logger_config( R"(
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
      )" );

    std::shared_ptr<void> IPFSLoader::LoadASync( std::string                              filename,
                                                 bool                                     save,
                                                 std::shared_ptr<boost::asio::io_context> ioc,
                                                 CompletionCallback                       handle_read )
    {
        auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config ) );
        auto r              = logging_system->configure();
        libp2p::log::setLoggingSystem( logging_system );

        auto loggerIdentifyMsgProcessor = libp2p::log::createLogger( "IdentifyMsgProcessor" );
        loggerIdentifyMsgProcessor->setLevel( soralog::Level::OFF );
        auto loggerProcessingEngine = sgns::ipfs_bitswap::createLogger( "Bitswap" );
        loggerProcessingEngine->set_level( spdlog::level::off );
        std::shared_ptr<string> result = std::make_shared<string>( "init" );

        //Get CID and Filename
        std::string ipfs_cid;
        std::string ipfs_file;
        if ( !parseIPFSUrl( filename, ipfs_cid, ipfs_file ) )
        {
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::INVALID_URL ), false ); } );
            return result;
        }

        // Check if we have an external bitswap instance
        auto bitswap = std::atomic_load( &externalBitswap_ );
        if ( bitswap )
        {
            m_logger->info( "Using external bitswap instance for IPFS request" );

            // Parse CID
            auto maybe_cid = libp2p::multi::ContentIdentifierCodec::fromString( ipfs_cid );
            if ( !maybe_cid )
            {
                m_logger->error( "Bad CID: {}", maybe_cid.error().message() );
                boost::asio::post( *ioc,
                                   [handle_read, ioc]()
                                   { handle_read( ioc, outcome::failure( Error::BAD_CID ), false ); } );
                return result;
            }
            auto cid = maybe_cid.value();

            // Create IPFSDevice with external bitswap (and optional external DHT)
            auto dht                 = std::atomic_load( &externalDht_ );
            auto ipfsDeviceResult    = IPFSDevice::createWithBitswap( ioc, std::move( bitswap ), dht );
            if ( !ipfsDeviceResult )
            {
                m_logger->error( "Failed to create IPFSDevice with external bitswap: {}",
                                 ipfsDeviceResult.error().message() );
                boost::asio::post( *ioc,
                                   [handle_read, ioc]()
                                   { handle_read( ioc, outcome::failure( Error::CANNOT_LISTEN ), false ); } );
                return result;
            }
            auto ipfsDevice = ipfsDeviceResult.value();

            if ( ipfsDevice->getDHT() )
            {
                // DHT available - discover providers for this CID before requesting the block
                m_logger->info( "External DHT available, finding providers for CID" );
                ioc->post(
                    [=] { ipfsDevice->StartFindingPeers( ioc, cid, ipfs_file, 0, save, handle_read ); } );
            }
            else
            {
                // Use the device to request the block
                ioc->post( [=] { ipfsDevice->RequestBlockMain( ioc, cid, ipfs_file, 0, save, handle_read ); } );
            }

            return result;
        }

        // Fall back to IPFSDevice creation (existing behavior)
        m_logger->info( "No external bitswap available, creating IPFSDevice" );

        //Create Host
        auto ipfsDeviceResult = IPFSDevice::getInstance( ioc );
        if ( !ipfsDeviceResult )
        {
            //Error Listening
            m_logger->error( "Cannot listen to address: {}", ipfsDeviceResult.error().message() );
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::CANNOT_LISTEN ), false ); } );
            return result;
        }
        auto ipfsDevice = ipfsDeviceResult.value();
        //auto ma = libp2p::multi::Multiaddress::create("/ip4/127.0.0.1/tcp/40000").value();
        //ipfsDevice->addAddress(libp2p::multi::Multiaddress::create("/ip4/3.92.45.153/tcp/4001/p2p/12D3KooWP6R6XVCBK7t76o8VDwZdxpzAqVeDtHYQNmntP2y8NHvK").value());

        //CID of File
        auto maybe_cid = libp2p::multi::ContentIdentifierCodec::fromString( ipfs_cid );
        if ( !maybe_cid )
        {
            m_logger->error( "Bad CID: {}", maybe_cid.error().message() );
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::BAD_CID ), false ); } );
            return result;
        }
        auto cid = maybe_cid.value();

        // Add addresses for this specific CID
        // ipfsDevice->addAddress(
        //     cid,
        //     libp2p::multi::Multiaddress::create(
        //         "/ip4/192.168.46.124/tcp/4001/p2p/12D3KooWHsD2QEUS5FzHEyq2bTuwMSEuEvV86wVAc7VaDDKK1NwJ" )
        //         .value() );
        ioc->post(
            [=]
            {
                //ipfsDevice->RequestBlockMain( ioc, cid, ipfs_file, 0, save, handle_read );
                ipfsDevice->StartFindingPeers(ioc, cid, ipfs_file, 0, save, handle_read);
            } );

        return result;
    }

    void IPFSLoader::setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap>          bitswap,
                                 std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht )
    {
        std::atomic_store( &externalBitswap_, std::move( bitswap ) );
        std::atomic_store( &externalDht_, std::move( dht ) );
        m_logger->info( "External bitswap instance set for IPFS loader" );
    }

    bool IPFSLoader::clearBitswap( const std::shared_ptr<sgns::ipfs_bitswap::Bitswap> &bitswap )
    {
        auto expected = bitswap;
        if ( std::atomic_compare_exchange_strong(
                 &externalBitswap_, &expected, std::shared_ptr<sgns::ipfs_bitswap::Bitswap>{} ) )
        {
            // Clear the associated DHT as well so a stale DHT is never left behind
            std::atomic_store( &externalDht_, std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT>{} );
            return true;
        }
        return false;
    }

    bool IPFSLoader::hasExternalBitswap() const
    {
        return std::atomic_load( &externalBitswap_ ) != nullptr;
    }

    bool IPFSLoader::hasExternalDHT() const
    {
        return std::atomic_load( &externalDht_ ) != nullptr;
    }

    void IPFSLoader::addSeedProvider( const CID &cid, const libp2p::peer::PeerInfo &peerInfo )
    {
        // Check if we have an external bitswap instance
        auto bitswap = std::atomic_load( &externalBitswap_ );
        if ( bitswap )
        {
            bitswap->AddProvider( cid, peerInfo );
        }
        else
        {
            m_logger->info( "no bitswap instance to set provider for {}",
                            libp2p::multi::ContentIdentifierCodec::toString( cid ).value() );
        }
    }

} // End namespace sgns
