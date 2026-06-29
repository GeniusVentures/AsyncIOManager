include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../testutil
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
message(WARNING "INCL DIR: ${CMAKE_CURRENT_SOURCE_DIR}/../../include}")
message(WARNING "Boost LIBRARIES: ${Boost_LIBRARIES}")
# ---------------------------------------------------------------------------
# Phase 2: File-based tests (always built when BUILD_TESTING=ON)
# ---------------------------------------------------------------------------

addtest(mnn_loader_test
    mnn_loader_test.cpp
)
target_link_libraries(mnn_loader_test
    spdlog::spdlog 
    protobuf::libprotobuf
    AsyncIOManager
    asiomgr_testutil
)

addtest(mnn_saver_test
    mnn_saver_test.cpp
)
target_link_libraries(mnn_saver_test
    spdlog::spdlog 
    protobuf::libprotobuf
    AsyncIOManager
    asiomgr_testutil
)

addtest(filemanager_test
    filemanager_test.cpp
)
target_link_libraries(filemanager_test
    spdlog::spdlog 
    protobuf::libprotobuf
    AsyncIOManager
    asiomgr_testutil
)

# ---------------------------------------------------------------------------
# Phase 3: Network-dependent tests (gated behind ASYNC_IO_MANAGER_NETWORK_TESTS)
# ---------------------------------------------------------------------------

option(ASYNC_IO_MANAGER_NETWORK_TESTS "Build network-dependent tests (HTTP, SFTP, IPFS)" OFF)

if(ASYNC_IO_MANAGER_NETWORK_TESTS)
    # HTTPLoader tests — embeds a Boost.Beast server in the test binary
    addtest(http_loader_test
        http_loader_test.cpp
    )
    target_link_libraries(http_loader_test
        AsyncIOManager
        asiomgr_testutil
        ${Boost_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
    )

    # SFTPSaver tests — requires local sshd
    addtest(sftp_saver_test
        sftp_saver_test.cpp
    )
    target_link_libraries(sftp_saver_test
        AsyncIOManager
        asiomgr_testutil
        ${Boost_LIBRARIES}
        Libssh2::libssh2
        OpenSSL::SSL
        OpenSSL::Crypto
    )

    # # IPFS tests — local BitswapNode fixtures
    addtest(ipfs_loader_test
        ipfs_loader_test.cpp
    )
    target_link_libraries(ipfs_loader_test
        AsyncIOManager
        asiomgr_testutil
        ${Boost_LIBRARIES}
        ipfs-bitswap-cpp
        ipfs-bitswap-proto
        ipfs-unixfs-proto
        ipfs-lite-cpp::ipld_node
        Boost::Boost.DI
        p2p::p2p_default_network
        p2p::p2p_basic_host
        p2p::p2p_multiaddress
        p2p::p2p_kademlia
        p2p::p2p_identify
        p2p::p2p_identify_proto
        p2p::p2p_ping
        p2p::p2p_default_host
        spdlog::spdlog
        soralog::soralog
        yaml-cpp::yaml-cpp
        Boost::boost
        ${WIN_CRYPT_LIBRARY}
    )

    addtest(ipfs_saver_test
        ipfs_saver_test.cpp
    )
    target_link_libraries(ipfs_saver_test
        AsyncIOManager
        asiomgr_testutil
        ${Boost_LIBRARIES}
        ipfs-bitswap-cpp
        ipfs-bitswap-proto
        ipfs-unixfs-proto
        ipfs-lite-cpp::ipld_node
        Boost::Boost.DI
        p2p::p2p_default_network
        p2p::p2p_basic_host
        p2p::p2p_multiaddress
        p2p::p2p_kademlia
        p2p::p2p_identify
        p2p::p2p_identify_proto
        p2p::p2p_ping
        p2p::p2p_default_host
        spdlog::spdlog
        soralog::soralog
        yaml-cpp::yaml-cpp
        Boost::boost
        ${WIN_CRYPT_LIBRARY}
    )
endif()
