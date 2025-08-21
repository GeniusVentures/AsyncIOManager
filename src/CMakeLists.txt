#add_subdirectory(application)
file(GLOB FILELOADER_HEADER "../include/*.h*")
file(GLOB FILELOADER_SRCS "*.cpp")

add_library(AsyncIOManager STATIC
    #${FILELOADER_SRCS}
	FILECommon.cpp
	FileManager.cpp
	HTTPCommon.cpp
	HTTPLoader.cpp
	#IPFSCommon.cpp
	#IPFSLoader.cpp
	#IPFSSaver.cpp
	#MNNLoader.cpp
	#MNNSaver.cpp
	URLStringUtil.cpp
	#WSCommon.cpp
	#WSLoader.cpp
    )

add_proto_library(ipfs-unixfs proto/unixfs.proto)

target_link_libraries(AsyncIOManager PRIVATE
	spdlog::spdlog
	${Boost_LIBRARIES}
	OpenSSL::SSL 
	OpenSSL::Crypto
	#libssh2::libssh2
	#${MNN_LIBS}
    p2p::asio_scheduler
	p2p::p2p_logger
	p2p::p2p_default_network
	p2p::p2p_cares
	p2p::p2p_dnsaddr_resolver
	p2p::p2p_default_host
	p2p::p2p_gossip
	p2p::p2p_ping
	p2p::p2p_identify
	p2p::p2p_identify_proto
	p2p::p2p_basic_host
    p2p::p2p_multiaddress
	p2p::p2p_kademlia
	ipfs-lite-cpp::cbor
	ipfs-lite-cpp::graphsync
	ipfs-lite-cpp::ipld_node
	ipfs-lite-cpp::ipfs_blockservice
	ipfs-lite-cpp::ipfs_datastore_in_memory
	ipfs-lite-cpp::ipfs_datastore_rocksdb
	ipfs-lite-cpp::ipfs_kad_dht
	ipfs-bitswap-proto
	ipfs-bitswap-cpp 
	ipfs-unixfs
	)
include_directories(../include )

