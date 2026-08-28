// IPFSSAVER.hpp

#pragma once

#include "FileSaver.hpp"
#include "ASIOSingleton.hpp"
#include "bitswap.hpp"

// Forward declaration for DHT
namespace sgns::ipfs_lite::ipfs::dht
{
    class IpfsDHT;
}

namespace sgns
{
    /// @brief class to handle "ipfs://" prefix in a filename to save to ipfs
    class IPFSSaver : public FileSaver
    {
        SINGLETON_PTR( IPFSSaver );

    public:
        /// @brief init singleton pointer for usage
        static void InitializeSingleton();
        /// @brief save a file to ipfs, throws on error
        /// @param filename filename to save the file as
        virtual void SaveFile( std::string filename, std::shared_ptr<void> data ) override;
        virtual void SaveASync( std::shared_ptr<boost::asio::io_context>                            ioc,
                                std::function<void( std::shared_ptr<boost::asio::io_context> ioc )> handle_write,
                                std::string                                                         filename,
                                ResultType                                                          data,
                                std::string                                                         suffix,
                                std::shared_ptr<std::string>                                        save_location = nullptr ) override;

        /// @brief Set external bitswap instance for publishing content
        /// @param bitswap Shared pointer to bitswap instance
        /// @param dht Optional external DHT instance used to announce published CIDs
        void setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap>          bitswap,
                         std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht = nullptr );

        /// @brief Clear the external Bitswap only when it still belongs to the specified owner.
        bool clearBitswap( const std::shared_ptr<sgns::ipfs_bitswap::Bitswap> &bitswap );

        /// @brief Check if external bitswap is available
        /// @return true if external bitswap is set
        bool hasExternalBitswap() const;

        /// @brief Check if external DHT is available
        /// @return true if external DHT is set
        bool hasExternalDHT() const;

    private:
        /// @brief Announce a published CID in the DHT (when an external DHT is set)
        void AnnounceCID( const std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> &dht,
                          const sgns::ipfs_bitswap::CID                              &cid );

        std::shared_ptr<sgns::ipfs_bitswap::Bitswap>          externalBitswap_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> externalDht_;
        sgns::asiomgr::Logger                                 m_logger = sgns::asiomgr::createLogger( "IPFSSaver" );
    };
}
