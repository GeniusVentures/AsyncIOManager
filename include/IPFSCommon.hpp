/**
 * Header file for the IPFSCommon
 */
#pragma once
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include "logger.hpp"
#include "bitswap.hpp"
#include "boost/asio/io_context.hpp"
#include "libp2p/injector/host_injector.hpp"
#include "libp2p/log/configurator.hpp"
#include "libp2p/protocol/identify/identify.hpp"
#include "libp2p/multi/content_identifier_codec.hpp"
#include "libp2p/protocol/ping/ping.hpp"
#include "ipfs_lite/dht/kademlia_dht.hpp"
#include "libp2p/injector/kademlia_injector.hpp"
//TEMP REmove
#include <fstream>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <libp2p/outcome/outcome.hpp>
#include <asiomgr-logger.hpp>

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{

    /**
	 * For creating a peer
	 */
    struct Peer
    {
        libp2p::peer::PeerInfo info;
    };

    /**
	 * This class creates an IPFS Device and has a function to download
	 * from an IPFS node(s).
	 */
    class IPFSDevice : public std::enable_shared_from_this<IPFSDevice>
    {
    public:
        enum class Error
        {
            CANNOT_DECODE = 1,
            NO_SOURCE     = 2,
        };
        using ResultType =
            outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>;
        /**
		 * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
		 * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
		 * @param buffers - Contains path/data loaded
		 * @param parse - Whether to parse file upon completion (for MNN)
		 * @param save - Whether to save the file to local disk upon completion
		 */
        using CompletionCallback = std::function<
            void( std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool parse, bool save )>;

        /**
		 * Create an IPFS Singlelton Device and return instance
		 * @param ioc - Asio io context to use
		 */
        static outcome::result<std::shared_ptr<IPFSDevice>> getInstance( std::shared_ptr<boost::asio::io_context> ioc );

        /**
		 * Create an IPFS Device instance using external bitswap (no singleton)
		 * @param ioc - Asio io context to use
		 * @param bitswap - External bitswap instance to use
         * @param dht - Optional external DHT instance for provider discovery/announcement
         */
        static outcome::result<std::shared_ptr<IPFSDevice>> createWithBitswap(
            std::shared_ptr<boost::asio::io_context>          ioc,
            std::shared_ptr<sgns::ipfs_bitswap::Bitswap>      bitswap,
            std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht = nullptr );
        /**
         * Get bitswap from device
         */
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> getBitswap() const;
        /**
        * Get host from device
        */
        std::shared_ptr<libp2p::Host> getHost() const;
        /**
         * Get DHT from device (null when none was provided or created)
         */
        std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> getDHT() const;

        ~IPFSDevice()
        {
            // Cleanup resources if needed
        }

        /**
		 * Find peers with the CID we are looking for using Kademlia DHT.
		 * @param ioc - Asio io context to use
		 * @param cid - IPFS Main CID to get from bitswap
		 * @param filename - Filename for file, mostly for use if this is a single file.
		 * @param addressoffset - Offset from list of addresses to use, usually want to call 0 on this as it will loop through from starting point if needed
		 * @param parse - Whether to parse file upon completion (for MNN currently)
		 * @param save - Whether to save the file to local disk upon completion
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        bool StartFindingPeers( std::shared_ptr<boost::asio::io_context> ioc,
                                const sgns::ipfs_bitswap::CID           &cid,
                                std::string                              filename,
                                int                                      addressoffset,
                                bool                                     parse,
                                bool                                     save,
                                CompletionCallback                       handle_read );
        void StartFindingPeersWithRetry( std::shared_ptr<boost::asio::io_context> ioc,
                                         const sgns::ipfs_bitswap::CID           &cid,
                                         std::string                              filename,
                                         int                                      addressoffset,
                                         bool                                     parse,
                                         bool                                     save,
                                         CompletionCallback                       handle_read );
        /**
		 * Add the Main CID for a file to bitswap wantlist to get information or file(if small enough)
		 * @param ioc - Asio io context to use
		 * @param cid - IPFS Main CID to get from bitswap
		 * @param filename - Filename for file, mostly for use if this is a single file.
		 * @param addressoffset - Offset from list of addresses to use, usually want to call 0 on this as it will loop through from starting point if needed
		 * @param parse - Whether to parse file upon completion (for MNN currently)
		 * @param save - Whether to save the file to local disk upon completion
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        bool RequestBlockMain( std::shared_ptr<boost::asio::io_context> ioc,
                               const sgns::ipfs_bitswap::CID           &cid,
                               std::string                              filename,
                               int                                      addressoffset,
                               bool                                     parse,
                               bool                                     save,
                               CompletionCallback                       handle_read );

        /**
		 * Add a peer address for a specific CID
		 * @param cid - content identifier
		 * @param address - libp2p multiaddress to add for this CID
		 */
        void addAddress( const sgns::ipfs_bitswap::CID &cid, const libp2p::multi::Multiaddress &address );

        /**
		 * Add multiple peer addresses for a specific CID
		 * @param cid - content identifier
		 * @param addresses - vector of multiaddresses
		 */
        void addAddresses( const sgns::ipfs_bitswap::CID                  &cid,
                           const std::vector<libp2p::multi::Multiaddress> &addresses );

    private:
        /**
		 * Convert UnixFSContent from bitswap to AsyncIOManager result format
		 * @param ioc - Asio io context
		 * @param unixfsContent - UnixFS content from bitswap RequestContent
		 * @param filename - Base filename for result
		 * @param parse - Whether to parse file upon completion
		 * @param save - Whether to save the file to local disk upon completion
		 * @param handle_read - Completion callback
		 */
        void convertUnixFSContentToResult( std::shared_ptr<boost::asio::io_context> ioc,
                                           const sgns::ipfs_bitswap::UnixFSContent &unixfsContent,
                                           const std::string                       &filename,
                                           bool                                     parse,
                                           bool                                     save,
                                           CompletionCallback                       handle_read );

        /**
		 * Create an IPFSDevice along with associated bitswap and host on an asio io_context
		 * @param ioc - Asio io context to use
		 */
        IPFSDevice( std::shared_ptr<boost::asio::io_context> ioc );

        /**
		 * Create an IPFSDevice using external bitswap instance
		 * @param ioc - Asio io context to use
		 * @param bitswap - External bitswap instance to use
         * @param dht - Optional external DHT instance to use
         */
        IPFSDevice( std::shared_ptr<boost::asio::io_context>          ioc,
                    std::shared_ptr<sgns::ipfs_bitswap::Bitswap>      bitswap,
                    std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht = nullptr );

        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "IPFSCommon" );

        static std::shared_ptr<IPFSDevice> instance_;
        static std::mutex                  mutex_;

        std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht_;
        std::shared_ptr<libp2p::Host>                        host_;
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap>         bitswap_;
        boost::asio::deadline_timer                          dhtretry_;

        //Default Bootstrap Servers
        std::vector<std::string> bootstrapAddresses_ = {
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmcZf59bWwK5XFi76CZX8cbJ4BhTzzA3gU1ZjYZcYW3dwt",
            //"/ip4/64.225.105.42/tcp/4001/p2p/QmPo1ygpngghu5it8u4Mr3ym6SEU2Wp2wA66Z91Y1S1g29",
            //"/ip4/3.92.45.153/tcp/4001/ipfs/12D3KooWP6R6XVCBK7t76o8VDwZdxpzAqVeDtHYQNmntP2y8NHvK",
            "/ip4/104.131.131.82/tcp/4001/ipfs/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",  // mars.i.ipfs.io
            "/ip4/104.236.179.241/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM", // pluto.i.ipfs.io
            "/ip4/128.199.219.111/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu", // saturn.i.ipfs.io
            "/ip4/104.236.76.40/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64",   // venus.i.ipfs.io
            "/ip4/178.62.158.247/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd",  // earth.i.ipfs.io
            "/ip6/2604:a880:1:20::203:d001/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM", // pluto.i.ipfs.io
            "/ip6/2400:6180:0:d0::151:6001/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu", // saturn.i.ipfs.io
            "/ip6/2604:a880:800:10::4a:5001/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64", // venus.i.ipfs.io
            "/ip6/2a03:b0c0:0:1010::23:1001/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd", // earth.i.ipfs.io
            //"/dnsaddr/fra1-1.hostnodes.pinata.cloud/ipfs/QmWaik1eJcGHq1ybTWe7sezRfqKNcDRNkeBaLnGwQJz1Cj",
            //"/dnsaddr/fra1-2.hostnodes.pinata.cloud/ipfs/QmNfpLrQQZr5Ns9FAJKpyzgnDL2GgC6xBug1yUZozKFgu4",
            //"/dnsaddr/fra1-3.hostnodes.pinata.cloud/ipfs/QmPo1ygpngghu5it8u4Mr3ym6SEU2Wp2wA66Z91Y1S1g29",
            //"/dnsaddr/nyc1-1.hostnodes.pinata.cloud/ipfs/QmRjLSisUCHVpFa5ELVvX3qVPfdxajxWJEHs9kN3EcxAW6",
            //"/dnsaddr/nyc1-2.hostnodes.pinata.cloud/ipfs/QmPySsdmbczdZYBpbi2oq2WMJ8ErbfxtkG8Mo192UHkfGP",
            //"/dnsaddr/nyc1-3.hostnodes.pinata.cloud/ipfs/QmSarArpxemsPESa6FNkmuu9iSE1QWqPX2R3Aw6f5jq4D5",
        };
    };

}
