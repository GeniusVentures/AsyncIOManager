//IPFSCommon.hpp
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
#include "ipfs_lite/ipld/impl/ipld_node_decoder_pb.hpp"
#include "libp2p/protocol/kademlia/kademlia.hpp"
#include "libp2p/injector/kademlia_injector.hpp"
//TEMP REmove
#include <fstream>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#ifndef IPFSCOMMON_HPP
#define IPFSCOMMON_HPP
namespace sgns
{
	struct CIDInfo
	{
		/**
		 * Data, which contains the main CID and any linked CIDs stored as a vector, as well as any associated data
		 * an entire file grabbed over bitswap
		 */
		libp2p::multi::ContentIdentifier mainCID;
		std::vector<libp2p::multi::ContentIdentifier> mainCIDs;
		std::vector<std::string> directories;
		std::pair<std::vector<std::string>, std::vector<std::vector<char>>> finalcontents;
		size_t outstandingRequests_;
		struct LinkedCIDInfo
		{
			libp2p::multi::ContentIdentifier linkedCID;
			libp2p::multi::ContentIdentifier parentCID;
			std::string directory;
			std::vector<char> content;

			LinkedCIDInfo(const libp2p::multi::ContentIdentifier& cid, const libp2p::multi::ContentIdentifier& parentcid, std::string& dir)
				: linkedCID(cid), content(), parentCID(parentcid), directory(dir) {}
		};
		std::vector<LinkedCIDInfo> linkedCIDs;

		CIDInfo(const libp2p::multi::ContentIdentifier& cid)
			: mainCID(cid), mainCIDs(), linkedCIDs(), finalcontents(), outstandingRequests_() {}

		/**
		 * Group data for linked CIDs to make a complete file
		 */
		void groupLinkedCIDs() {
			// Group by CID 
			std::unordered_map<std::string, std::pair<std::string, std::vector<char>>> groupedData;
			std::string cidbase;
			// For each linked CID we will add data to group
			for (const auto& linkedCID : linkedCIDs) {
				//Use pretty string because CID can't be copied for unordered map
				auto cidstring = linkedCID.parentCID.toPrettyString(cidbase);
				auto it = groupedData.find(cidstring);
				if (it == groupedData.end()) {
					//Nothing there, so add it.
					groupedData.emplace(cidstring, std::make_pair(linkedCID.directory, linkedCID.content));
				}
				else {
					//Add it
					it->second.second.insert(it->second.second.end(), linkedCID.content.begin(), linkedCID.content.end());
				}
			}

			// Populate finalcontents
			for (const auto& entry : groupedData) {
				finalcontents.first.push_back(entry.second.first);
				finalcontents.second.push_back(entry.second.second);
			}
		}

		/**
		 * Write final data out to directories. Not async.
		 */
		void writeFinalContentsToDirectories() {
			auto basedir = boost::lexical_cast<std::string>((boost::uuids::random_generator())()) + "/";
			//Iterate through finalcontents
			for (size_t i = 0; i < finalcontents.first.size(); ++i) {
				const std::string& directoryWithFile = basedir + finalcontents.first[i];
				const std::vector<char>& content = finalcontents.second[i];

				//Extract the directory path from the full path including the filename
				std::filesystem::path filePath(directoryWithFile);
				std::filesystem::path directory = filePath.parent_path();

				// Create the directory if it doesn't exist
				std::filesystem::create_directories(directory);

				// Write the content to the file
				std::ofstream outputFile(directoryWithFile, std::ios::binary);
				if (outputFile.is_open()) {
					outputFile.write(content.data(), content.size());
					outputFile.close();
				}
				else {
					// Handle error if unable to open file
					std::cerr << "Error: Unable to open file " << directoryWithFile << " for writing." << std::endl;
				}
			}
		}
		/**
		 * Set the data for a linked CID
		 * @param linkedCID - Linked CID to set content to
		 * @param content - Content to insert
		 */
		bool setContentForLinkedCID(const libp2p::multi::ContentIdentifier& linkedCID, const std::vector<char>& content)
		{
			auto it = std::find_if(linkedCIDs.begin(), linkedCIDs.end(),
				[&linkedCID](const LinkedCIDInfo& info) {
					return info.linkedCID == linkedCID;
				});

			if (it != linkedCIDs.end())
			{
				// Update the content for the linked CID
				it->content = content;
				return true;
			}
			return false;
		}

		/**
		 * Check whether we have gotten data for all linked CIDs
		 * use of outstandingRequests_ is preferred.
		 */
		bool allLinkedCIDsHaveContent() const
		{
			return std::all_of(linkedCIDs.begin(), linkedCIDs.end(),
				[](const LinkedCIDInfo& linkedCIDInfo) {
					return !linkedCIDInfo.content.empty();
				});
		}

		/**
		 * Create a shared buffer vector with data from all linked CIDs combined to create a single file.
		 */
		std::shared_ptr<std::vector<char>> combineContents() const
		{
			auto combinedContent = std::make_shared<std::vector<char>>();

			// Iterate through each linkedCID and appends
			for (const auto& linkedCIDInfo : linkedCIDs)
			{
				combinedContent->insert(combinedContent->end(),
					linkedCIDInfo.content.begin(),
					linkedCIDInfo.content.end());
			}

			return combinedContent;
		}
	};

	/**
	 * For creating a peer
	 */
	struct Peer {
		libp2p::peer::PeerInfo info;
	};


	/**
	 * This class creates an DHT for finding peers with CIDs we want
	 * from IPFS node(s).
	 */
	class IpfsDHT
	{
	public:
		IpfsDHT(
			std::shared_ptr<libp2p::protocol::kademlia::Kademlia> kademlia,
			std::vector<std::string> bootstrapAddresses);

		void Start();

		bool FindProviders(
			const libp2p::multi::ContentIdentifier& cid,
			std::function<void(libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> onProvidersFound)> onProvidersFound);

		void FindPeer(
			const libp2p::peer::PeerId& peerId,
			std::function<void(libp2p::outcome::result<libp2p::peer::PeerInfo>)> onPeerFound);
	private:
		std::vector<libp2p::peer::PeerInfo> GetBootstrapNodes() const;

		std::shared_ptr<libp2p::protocol::kademlia::Kademlia> kademlia_;
		std::vector<std::string> bootstrapAddresses_;
	};

	/**
	 * This class creates an IPFS Device and has a function to download
	 * from an IPFS node(s).
	 */
	class IPFSDevice {
	public:
		/**
		 * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
		 * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
		 * @param buffers - Contains path/data loaded
		 * @param parse - Whether to parse file upon completion (for MNN)
		 * @param save - Whether to save the file to local disk upon completion
		 */
		using CompletionCallback = std::function<void(std::shared_ptr<boost::asio::io_context> ioc, std::pair<std::vector<std::string>, std::vector<std::vector<char>>> buffers, bool parse, bool save)>;
		/**
		 * Status callback returns an error code as an async load proceeds
		 * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
		 * @param buffer - Contains data loaded
		 * @param parse - Whether to parse file upon completion (for MNN)
		 * @param save - Whether to save the file to local disk upon completion
		 */
		using StatusCallback = std::function<void(const int&)>;

		/**
		 * Create an IPFS Singlelton Device and return instance
		 * @param ioc - Asio io context to use
		 */
		static outcome::result<std::shared_ptr<IPFSDevice>> getInstance(std::shared_ptr<boost::asio::io_context> ioc);
		/**
		 * Get bitswap from device
		 */
		std::shared_ptr<sgns::ipfs_bitswap::Bitswap> getBitswap() const;
		/**
		 * Get host from device
		 */
		std::shared_ptr<libp2p::Host> getHost() const;

		~IPFSDevice() {
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
		bool StartFindingPeers(
			std::shared_ptr<boost::asio::io_context> ioc,
			const sgns::ipfs_bitswap::CID& cid,
			std::string filename,
			int addressoffset,
			bool parse,
			bool save,
			CompletionCallback handle_read,
			StatusCallback status
		);

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
		bool RequestBlockMain(
			std::shared_ptr<boost::asio::io_context> ioc,
			const sgns::ipfs_bitswap::CID& cid,
			std::string filename,
			int addressoffset,
			bool parse,
			bool save,
			CompletionCallback handle_read,
			StatusCallback status);

		/**
		 * Add an address to pool of addresses to try to get file using IPFS bitswap
		 * @param address - libp2p multiaddress to add to pool /ip4/127.0.0.1/tcp/4001/p2p/CID format
		 */
		void addAddress(
			libp2p::multi::Multiaddress address
		);
		void addAddresses(const std::vector<libp2p::peer::PeerInfo>& addresses);

		/**
		 * Add the data of a linked CID to list containing all linked CIDs and associated data
		 * @param mainCID - Main CID of file we are getting
		 * @param linkedCID - Linked CID to main CID to get a portion of file
		 * @param content - The content we retrieved for the linked CID from bitswap
		 */
		bool setContentForLinkedCID(const sgns::ipfs_bitswap::CID& mainCID,
			const sgns::ipfs_bitswap::CID& linkedCID,
			const std::vector<char>& content);

		bool IPFSDevice::CheckIfAllSet(const sgns::ipfs_bitswap::CID& mainCID);
		/**
		 * Get a buffer of the combineed data of all linked CIDs to create an entire file.
		 * @param mainCID - Main CID of file we are getting
		 */
		std::shared_ptr<std::vector<char>> combineLinkedCIDs(const sgns::ipfs_bitswap::CID& mainCID);

		/**
		 * Add a CIDInfo object to list of CIDs we are trying to get.
		 * @param cidInfo - A CIDInfo object struct as defined above
		 */
		size_t addCID(CIDInfo& cidInfo);
	private:
		/**
		 * Create an IPFSDevice along with associated bitswap and host on an asio io_context
		 * @param ioc - Asio io context to use
		 */
		IPFSDevice(std::shared_ptr<boost::asio::io_context> ioc);

		/**
		 * Add the sub CID for a file to bitswap wantlist to get part of file
		 * @param ioc - Asio io context to use
		 * @param cid - IPFS Main CID to get from bitswap
		 * @param parentcid - CID of the CID that had this linked CID
		 * @param scid - Linked CID to get file of
		 * @param directory - Path of file
		 * @param addressoffset - Offset from list of addresses to use, usually want to call 0 on this as it will loop through from starting point if needed
		 * @param parse - Whether to parse file upon completion (for MNN currently)
		 * @param save - Whether to save the file to local disk upon completion
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
		bool RequestBlockSub(
			std::shared_ptr<boost::asio::io_context> ioc,
			const sgns::ipfs_bitswap::CID& cid,
			const sgns::ipfs_bitswap::CID& parentcid,
			const sgns::ipfs_bitswap::CID& scid,
			std::string directory,
			int addressoffset,
			bool parse,
			bool save,
			CompletionCallback handle_read,
			StatusCallback status);


		//Common vars used for getting file from IPFS
		static std::shared_ptr<IPFSDevice> instance_;
		static std::mutex mutex_;

		std::shared_ptr<sgns::IpfsDHT> dht_;
		std::shared_ptr<libp2p::Host> host_;
		std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap_;
		//std::shared_ptr<std::vector<libp2p::multi::Multiaddress>> peerAddresses_;
		std::shared_ptr<std::vector<libp2p::peer::PeerInfo>> peerAddresses_;

		// Maintain a list of requested CIDs along with their linked CIDs and the content of the linked CIDs
		std::vector<CIDInfo> requestedCIDs_;

		/**
		 * Find index of CID we have requested for use in bitswap
		 * @param cid - CID to find in list
		 */
		size_t findRequestedCIDIndex(const libp2p::multi::ContentIdentifier& cid) const {
			auto it = std::find_if(requestedCIDs_.begin(), requestedCIDs_.end(),
				[&cid](const CIDInfo& cidInfo) {
					return cidInfo.mainCID == cid;
				});

			if (it != requestedCIDs_.end()) {
				// Found the CID, return its index
				return std::distance(requestedCIDs_.begin(), it);
			}
			else {
				// CID not found
				return -1; 
			}
		}

		//Default Bootstrap Servers
		std::vector<std::string> bootstrapAddresses_ = {
			//"/dnsaddr/bootstrap.libp2p.io/ipfs/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN",
			//"/dnsaddr/bootstrap.libp2p.io/ipfs/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa",
			//"/dnsaddr/bootstrap.libp2p.io/ipfs/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb",
			//"/dnsaddr/bootstrap.libp2p.io/ipfs/QmcZf59bWwK5XFi76CZX8cbJ4BhTzzA3gU1ZjYZcYW3dwt",
			"/ip4/104.131.131.82/tcp/4001/ipfs/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",            // mars.i.ipfs.io
			"/ip4/104.236.179.241/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM",           // pluto.i.ipfs.io
			"/ip4/128.199.219.111/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu",           // saturn.i.ipfs.io
			"/ip4/104.236.76.40/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64",             // venus.i.ipfs.io
			"/ip4/178.62.158.247/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd",            // earth.i.ipfs.io
			"/ip6/2604:a880:1:20::203:d001/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM",  // pluto.i.ipfs.io
			"/ip6/2400:6180:0:d0::151:6001/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu",  // saturn.i.ipfs.io
			"/ip6/2604:a880:800:10::4a:5001/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64", // venus.i.ipfs.io
			"/ip6/2a03:b0c0:0:1010::23:1001/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd", // earth.i.ipfs.io
		};
	};


}

#endif