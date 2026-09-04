# Platform selection for the local-file device layer (PLAT-03):
# CMake is the ONLY selection mechanism — zero platform branching in sources.
if(WIN32)
    set(ASIOMGR_LOCALFILE_SOURCE LocalFileCommon.win.cpp)
    set(ASIOMGR_LOCALFILE_HEADER_NAME "LocalFileCommon.win.hpp")
elseif(UNIX)
    set(ASIOMGR_LOCALFILE_SOURCE LocalFileCommon.posix.cpp)
    set(ASIOMGR_LOCALFILE_HEADER_NAME "LocalFileCommon.posix.hpp")
endif()

add_library(AsyncIOManager STATIC
    ${ASIOMGR_LOCALFILE_SOURCE}
    FileManager.cpp
    HTTPCommon.cpp
    HTTPLoader.cpp
    IPFSCommon.cpp
    IPFSLoader.cpp
    IPFSSaver.cpp
    LocalFileLoader.cpp
    LocalFileSaver.cpp
    SFTPSaver.cpp
    URLStringUtil.cpp
    WSCommon.cpp
    WSLoader.cpp
    asiomgr-logger.cpp
)

target_link_libraries(AsyncIOManager
    PUBLIC
    ${Boost_LIBRARIES}
    spdlog::spdlog
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
    libssh2::libssh2
    PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ipfs-bitswap-proto
    ipfs-unixfs-proto
    ipfs-bitswap-cpp
)

# Header token WITH literal quotes — the umbrella's `#include ASIOMGR_LOCALFILE_HEADER`
# expands to #include "LocalFileCommon.win.hpp" on this platform. PRIVATE (D-10):
# the only TUs including the umbrella are library sources.
target_compile_definitions(AsyncIOManager PRIVATE
    ASIOMGR_LOCALFILE_HEADER="${ASIOMGR_LOCALFILE_HEADER_NAME}"
)
include_directories(../include)

if(BUILD_TESTING)
    add_subdirectory(../test ${CMAKE_BINARY_DIR}/test)
endif()
