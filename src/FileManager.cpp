#include "FileManager.hpp"
#include "URLStringUtil.h"
#include "LocalFileLoader.hpp"
#include "LocalFileSaver.hpp"
#include "IPFSLoader.hpp"
#include "IPFSSaver.hpp"
#include "HTTPLoader.hpp"
#include "SFTPLoader.hpp"
#include "SFTPSaver.hpp"
#include "WSLoader.hpp"
#include <bitswap.hpp>

void FileManager::RegisterLoader( const std::string &prefix, FileLoader *handlerLoader )
{
    loaders[prefix] = handlerLoader;
}

void FileManager::RegisterSaver( const std::string &prefix, FileSaver *handlerSaver )
{
    savers[prefix] = handlerSaver;
}

void AsyncHandler( boost::system::error_code ec, std::size_t n, std::vector<char> &buffer ) {}

void FileManager::InitializeSingletons()
{
    sgns::LocalFileLoader::InitializeSingleton();
    //sgns::SFTPLoader::InitializeSingleton();
    sgns::HTTPLoader::InitializeSingleton();
    //sgns::WSLoader::InitializeSingleton();
    sgns::IPFSLoader::InitializeSingleton();
    sgns::IPFSSaver::InitializeSingleton();
    sgns::LocalFileSaver::InitializeSingleton();
    sgns::SFTPSaver::InitializeSingleton();
}

shared_ptr<void> FileManager::LoadASync( const std::string                       &url,
                                         bool                                     parse,
                                         bool                                     save,
                                         std::shared_ptr<boost::asio::io_context> ioc,
                                         FinalCallback                            finalcall,
                                         std::string                              savetype )
{
    std::string prefix;
    std::string filePath;
    std::string suffix;

    getURLComponents( url, prefix, filePath, suffix );
    m_logger->debug( "URL: {} -prefix: {} -filePath: {} -suffix: {}", url, prefix, filePath, suffix );
    auto loaderIter = loaders.find( prefix );
    if ( loaderIter == loaders.end() )
    {
        throw std::range_error( "No loader registered for prefix " + prefix );
    }
    //Increment Operations
    IncrementOutstandingOperations();
    //Create a handler
    auto handle_read = [this, savetype, suffix, finalcall]( std::shared_ptr<boost::asio::io_context> ioc,
                                                            ResultType                               buffers,
                                                            bool                                     parse,
                                                            bool                                     save )
    {
        if ( buffers )
        {
            //Save data or otherwise decrement counter of operations
            if ( save )
            {
                auto handle_write = [this]( std::shared_ptr<boost::asio::io_context> ioc )
                { DecrementOutstandingOperations( ioc ); };
                auto saverIter = savers.find( savetype );
                if ( saverIter == savers.end() )
                {
                    m_logger->error( "No saver registered for savetype: {}", savetype );
                    DecrementOutstandingOperations( ioc );
                    finalcall( outcome::failure( std::make_error_code( std::errc::operation_not_supported ) ) );
                    return;
                }
                auto saver = saverIter->second;
                saver->SaveASync( ioc, handle_write, "", buffers, suffix );
            }
            else
            {
                // Handle completion
                DecrementOutstandingOperations( ioc );
            }
        }
        else
        {
            DecrementOutstandingOperations( ioc );
        }
        finalcall( buffers );
    };
    auto loader = loaderIter->second;
    // double check pointer is to a FileLoader class
    assert( dynamic_cast<FileLoader *>( loader ) );
    shared_ptr<void> data = loader->LoadASync( filePath, parse, save, ioc, handle_read );
    return data;
}

void FileManager::SaveASync( const std::string                       &url,
                             ResultType                               data,
                             std::shared_ptr<boost::asio::io_context> ioc,
                             FinalCallback                            finalcall,
                             std::shared_ptr<std::string>             save_location )
{
    std::string prefix;
    std::string filePath;
    std::string suffix;

    getURLComponents( url, prefix, filePath, suffix );
    m_logger->debug( "URL: {} -prefix: {} -filePath: {} -suffix: {}", url, prefix, filePath, suffix );

    auto saverIter = savers.find( prefix );
    if ( saverIter == savers.end() )
    {
        throw std::range_error( "No saver registered for prefix " + prefix );
    }

    auto saver = saverIter->second;
    assert( dynamic_cast<FileSaver *>( saver ) );

    IncrementOutstandingOperations();

    auto handle_write = [this, ioc, finalcall, data]( std::shared_ptr<boost::asio::io_context> )
    {
        DecrementOutstandingOperations( ioc );
        if ( finalcall )
        {
            finalcall( data );
        }
    };

    try
    {
        saver->SaveASync( ioc, handle_write, filePath, data, suffix, save_location );
    }
    catch ( ... )
    {
        DecrementOutstandingOperations( ioc );
        throw;
    }
}

shared_ptr<void> FileManager::LoadFile( const std::string &url )
{
    std::string prefix;
    std::string filePath;
    std::string suffix;

    getURLComponents( url, prefix, filePath, suffix );
    auto loaderIter = loaders.find( prefix );
    if ( loaderIter == loaders.end() )
    {
        throw std::range_error( "No loader registered for prefix " + prefix );
    }
    auto loader = loaderIter->second;
    // double check pointer is to a FileLoader class
    assert( dynamic_cast<FileLoader *>( loader ) );

    shared_ptr<void> data = loader->LoadFile( filePath );

    return data;
}

void FileManager::SaveFile( const std::string &url, std::shared_ptr<void> data )
{
    std::string prefix;
    std::string filePath;
    std::string suffix;

    getURLComponents( url, prefix, filePath, suffix );

    auto saverIter = savers.find( prefix );

    if ( saverIter == savers.end() )
    {
        throw std::range_error( "No saver registered for prefix " + prefix );
    }

    auto saver = saverIter->second;
    // double check pointer is to a FileSaver class
    assert( dynamic_cast<FileSaver *>( saver ) );

    saver->SaveFile( filePath, data );
}

/// @brief Function to decrement operation count
void FileManager::DecrementOutstandingOperations( std::shared_ptr<boost::asio::io_context> ioc )
{
    if ( outstandingOperations_ <= 0 )
    {
        // Clean up io_context
        m_logger->error( "Tried to decrement operations but we have none already. This should never happen" );
        ioc->stop();
        return;
    }
    // Decrement the counter
    outstandingOperations_--;

    // If all operations are complete, perform additional cleanup
    if ( outstandingOperations_ == 0 )
    {
        // Clean up io_context
        ioc->stop();
    }
}

/// @brief Function to increment operation count
void FileManager::IncrementOutstandingOperations()
{
    // Increment the counter
    outstandingOperations_++;
}

std::shared_ptr<int> FileManager::GetOutstandingOperationsPointer()
{
    // Return a shared pointer to the outstandingOperations counter
    return std::make_shared<int>( outstandingOperations_ );
}

void FileManager::setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap>      bitswap,
                              std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht )
{
    std::lock_guard<std::mutex> lock( bitswapMutex_ );
    cacheDir_ = bitswap ? bitswap->getCacheDir() : "";

    // Forward bitswap instance to IPFSLoader
    auto ipfsLoaderIter = loaders.find( "ipfs" );
    if ( ipfsLoaderIter != loaders.end() )
    {
        auto ipfsLoader = dynamic_cast<sgns::IPFSLoader *>( ipfsLoaderIter->second );
        if ( ipfsLoader )
        {
            ipfsLoader->setBitswap( bitswap, dht );
            m_logger->info( "Bitswap instance set for IPFS loader{}", dht ? " with DHT" : "" );
        }
        else
        {
            m_logger->warn( "IPFS loader found but cast failed" );
        }
    }
    else
    {
        m_logger->warn( "IPFS loader not registered, cannot set bitswap" );
    }

    // Forward bitswap instance to IPFSSaver
    auto ipfsSaverIter = savers.find( "ipfs" );
    if ( ipfsSaverIter != savers.end() )
    {
        auto ipfsSaver = dynamic_cast<sgns::IPFSSaver *>( ipfsSaverIter->second );
        if ( ipfsSaver )
        {
            ipfsSaver->setBitswap( bitswap, dht );
            m_logger->info( "Bitswap instance set for IPFS saver{}", dht ? " with DHT" : "" );
        }
        else
        {
            m_logger->warn( "IPFS saver found but cast failed" );
        }
    }
    else
    {
        m_logger->warn( "IPFS saver not registered, cannot set bitswap" );
    }
}

void FileManager::clearBitswap( const std::shared_ptr<sgns::ipfs_bitswap::Bitswap> &bitswap )
{
    if ( !bitswap )
    {
        return;
    }

    std::lock_guard<std::mutex> lock( bitswapMutex_ );
    bool cleared = false;

    if ( auto loaderIter = loaders.find( "ipfs" ); loaderIter != loaders.end() )
    {
        if ( auto loader = dynamic_cast<sgns::IPFSLoader *>( loaderIter->second ) )
        {
            cleared |= loader->clearBitswap( bitswap );
        }
    }

    if ( auto saverIter = savers.find( "ipfs" ); saverIter != savers.end() )
    {
        if ( auto saver = dynamic_cast<sgns::IPFSSaver *>( saverIter->second ) )
        {
            cleared |= saver->clearBitswap( bitswap );
        }
    }

    if ( cleared )
    {
        cacheDir_.clear();
    }
}

std::string FileManager::getCacheDir() const
{
    std::lock_guard<std::mutex> lock( bitswapMutex_ );
    return cacheDir_;
}
