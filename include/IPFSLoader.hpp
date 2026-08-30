/**
 * Header file for the IPFSLoader
 */

#pragma once

#include <memory>
#include <string>
#include "IPFSCommon.hpp"
#include "FileLoader.hpp"
#include "ASIOSingleton.hpp"
#include "ipfs_lite/ipfs/impl/ipfs_block_service.hpp"
#include "ipfs_lite/ipfs/impl/in_memory_datastore.hpp"

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
<<<<<<< HEAD
=======

// Forward declaration for DHT
namespace libp2p
{
    class PeerInfo;
}
>>>>>>> 0d0d3ff

namespace sgns
{

    /**
     * This class is for loading files from IPFS
     */
    class IPFSLoader: public FileLoader
    {
        SINGLETON_PTR( IPFSLoader );

    public:
        enum class Error
        {
            CANNOT_LISTEN = 1,
            BAD_CID       = 2,
            INVALID_URL   = 3,
        };
        static void InitializeSingleton();

        /**
         * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
         * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
         * @param buffers - Contains path/data loaded
         * @param parse - Whether to parse file upon completion (for MNN)
         * @param save - Whether to save the file to local disk upon completion
         */
        using CompletionCallback = std::function<
            void( std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool parse, bool save )>;

        /**ok
         * Load Data on the MNN file
         * @param filename - MNN file part
         * @return Interpreter of MNN file
         *
         */
        std::shared_ptr<void> LoadFile( std::string filename ) override;
        /**
         * Asynchronously load a file
         * @param filename - Filename to load
         * @param parse - Whether to parse file upon completion (for MNN)
         * @param save - Whether to save the file to local disk upon completion
         * @param ioc - ASIO context for async loading
         * @param callback - Filemanager callback on completion
         * @param status - Status function that will be updated with status codes as operation progresses
         * @return String indicating init
         */
        std::shared_ptr<void> LoadASync( std::string                              filename,
                                         bool                                     parse,
                                         bool                                     save,
                                         std::shared_ptr<boost::asio::io_context> ioc,
                                         CompletionCallback                       callback ) override;

        /// @brief Set external bitswap instance to reuse existing libp2p host
        /// @param bitswap Shared pointer to existing bitswap instance
        /// @param dht Optional external DHT instance used for provider discovery
        void setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap>          bitswap,
                         std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht = nullptr );

        /// @brief Clear the external Bitswap only when it still belongs to the specified owner.
        bool clearBitswap( const std::shared_ptr<sgns::ipfs_bitswap::Bitswap> &bitswap );

        /// @brief Check if external bitswap is available
        /// @return True if external bitswap has been set
        bool hasExternalBitswap() const;

        /// @brief Check if external DHT is available
        /// @return True if external DHT has been set
        bool hasExternalDHT() const;

        /// @brief Register a seed provider for a CID on the IPFS loader's bitswap (DHT-less discovery)
        /// @param cid  CID the seed provider serves
        /// @param peerInfo - peers info for seed address
        void addSeedProvider( const CID &cid, const libp2p::peer::PeerInfo &peerInfo );

    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "IPFSLoader" );

        /// @brief External bitswap instance to reuse existing libp2p host
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> externalBitswap_;

        /// @brief External DHT instance for provider discovery (optional)
        std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> externalDht_;

    protected:
    };

} // End namespace sgns
