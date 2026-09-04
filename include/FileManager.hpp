#pragma once

#include <iostream>
#include <string>
#include <map>
#include <filesystem>
#include <cassert>
#include <future>
#include <memory>
#include <mutex>
#include "ASIOSingleton.hpp"
#include "FileLoader.hpp"
#include "FileSaver.hpp"
#include "boost/asio.hpp"
#include "boost/bind.hpp"
#include <libp2p/outcome/outcome.hpp>
#include <asiomgr-logger.hpp>

// Forward declaration for bitswap
namespace sgns::ipfs_bitswap
{
    class Bitswap;
}

// Forward declaration for DHT
namespace sgns::ipfs_lite::ipfs::dht
{
    class IpfsDHT;
}

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

/// \brief FileManager class handles all the registration of the file loaders and savers and proxies the basic
///         functionality to the registered handlers
class FileManager
{
    SINGLETON_REF( FileManager );

private:
    sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "FileManager" );
    /// @brief a map from std::string to loader handlers
    map<std::string, FileLoader *> loaders;
    /// @brief a map from std::string to saver handlers
    map<std::string, FileSaver *> savers;

    int outstandingOperations_ = 0;

    mutable std::mutex bitswapMutex_;
    std::string        cacheDir_; ///< Disk cache directory from bitswap (for local persistence).

public:
    static void InitializeSingletons();
    using ResultType =
        outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>;
    /**
         * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
         * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
         * @param buffers - Contains path/data loaded
         * @param save - Whether to save the file to local disk upon completion
         */
    using CompletionCallback =
        std::function<void( std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool save )>;

    /**
         * Final callback returns data to application
         * @param buffers - Contains path/data loaded
         */
    using FinalCallback = std::function<void( ResultType buffers )>;

    /// @brief Decrement operations counter so io_context thread can be shut down when all are complete.
    /// @param The io_context that we have been reading on
    void DecrementOutstandingOperations( std::shared_ptr<boost::asio::io_context> ioc );
    /// @brief Increment operations counter so io_context thread can be shut down when all are complete.
    void            IncrementOutstandingOperations();
    shared_ptr<int> GetOutstandingOperationsPointer();
    /// @brief Register a synchronous loader class to handle a specific prefix
    /// @param prefix = "https", "file", etc from https://xxxxx
    /// @param handlerLoader Handler class object that can load the data
    void RegisterLoader( const std::string &prefix, FileLoader *handlerLoader );
    /// @brief Register a synchronous saver class to handle a specific prefix
    /// @param prefix = "https", "file", etc from https://xxxxx
    /// @param handlerSaver Handler class object that can save the data
    void RegisterSaver( const std::string &prefix, FileSaver *handlerSaver );

    /**
         * Asynchronously load a file based on type
         * @param url - URL to load, will determine loader we use
         * @param save - Whether to save the file to local disk upon completion
         * @param ioc - ASIO context for async loading
         * @param callback - Filemanager callback on completion
         * @param status - Status function that will be updated with status codes as operation progresses
         * @return String indicating init
         */
    shared_ptr<void> LoadASync( const std::string                       &url,
                                bool                                     save,
                                std::shared_ptr<boost::asio::io_context> ioc,
                                FinalCallback                            finalcall,
                                std::string                              savetype );

        /**
            * Asynchronously save data based on type
            * @param url - URL to save to, will determine saver we use
            * @param data - Buffer payload to save
            * @param ioc - ASIO context for async saving
            * @param finalcall - Filemanager callback on completion
            * @param save_location - Output parameter for the resulting save location (file path, IPFS CID, etc.)
            */
        void SaveASync( const std::string                       &url,
                    ResultType                               data,
                    std::shared_ptr<boost::asio::io_context> ioc,
                    FinalCallback                            finalcall,
                    std::shared_ptr<std::string>             save_location = nullptr );

    /// @brief Load a file given a filePath
    /// @param url the full path and filename to load
    /// @return shared pointer to void * of the data loaded
    shared_ptr<void> LoadFile( const std::string &url );

    /// @brief Save Data to a file via some system, throws exception on error
    /// @param url URL prefix filename and extension
    /// @param data shared pointer to void * of the data to save
    void SaveFile( const std::string &url, std::shared_ptr<void> data );

    /// @brief Set bitswap instance for IPFS operations
    /// @param bitswap Shared pointer to existing bitswap instance to reuse
    /// @param dht Optional DHT instance used to announce/discover CIDs (defaults to none)
    void setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap>      bitswap,
                     std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht = nullptr );

    /// @brief Clear the external Bitswap only if it is still the supplied node's instance.
    void clearBitswap( const std::shared_ptr<sgns::ipfs_bitswap::Bitswap> &bitswap );

    /// @brief Get the disk cache directory used by bitswap (empty if not configured)
    /// @return Cache directory path, or empty string
    std::string getCacheDir() const;
};
