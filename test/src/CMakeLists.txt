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

addtest(localfile_loader_test
    localfile_loader_test.cpp
)
target_link_libraries(localfile_loader_test
    spdlog::spdlog 
    protobuf::libprotobuf
    AsyncIOManager
    asiomgr_testutil
)

addtest(localfile_saver_test
    localfile_saver_test.cpp
)
target_link_libraries(localfile_saver_test
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

addtest(no_ifdef_test
    no_ifdef_test.cpp
)
# Absolute source-dir paths injected at compile time (TEST_CERT_DIR pattern);
# CMake keeps forward slashes on Windows — safe for std::ifstream from test_bin/Debug/.
target_compile_definitions(no_ifdef_test PRIVATE
    LOCALFILE_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    LOCALFILE_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../src"
)

# ---------------------------------------------------------------------------
# Phase 3: Network-dependent tests (gated behind ASYNC_IO_MANAGER_NETWORK_TESTS)
# ---------------------------------------------------------------------------

option(ASYNC_IO_MANAGER_NETWORK_TESTS "Build network-dependent tests (HTTP, SFTP, IPFS)" OFF)

if(ASYNC_IO_MANAGER_NETWORK_TESTS)
    # -----------------------------------------------------------------------
    # Self-signed certificate for HTTPLoader HTTPS tests
    # -----------------------------------------------------------------------
    find_program(OPENSSL_EXECUTABLE openssl REQUIRED)
    set(TEST_CERT_DIR ${CMAKE_CURRENT_BINARY_DIR}/test_certs)
    file(MAKE_DIRECTORY ${TEST_CERT_DIR})

    # Minimal openssl.cnf — the installed OpenSSL has a hardcoded config
    # path that may not exist (e.g. Z:/extlib/...). Provide our own.
    set(TEST_OPENSSL_CNF ${TEST_CERT_DIR}/openssl.cnf)
    file(WRITE ${TEST_OPENSSL_CNF}
        "[req]\n"
        "distinguished_name = req_distinguished_name\n"
        "x509_extensions = v3_req\n"
        "prompt = no\n"
        "[req_distinguished_name]\n"
        "CN = 127.0.0.1\n"
        "[v3_req]\n"
        "subjectAltName = @alt_names\n"
        "[alt_names]\n"
        "IP.1 = 127.0.0.1\n"
        "DNS.1 = localhost\n"
    )

    add_custom_command(
        OUTPUT ${TEST_CERT_DIR}/cert.pem ${TEST_CERT_DIR}/key.pem
        COMMAND ${OPENSSL_EXECUTABLE} req -x509 -newkey rsa:2048
            -keyout ${TEST_CERT_DIR}/key.pem
            -out ${TEST_CERT_DIR}/cert.pem
            -days 365 -nodes
            -config ${TEST_OPENSSL_CNF}
        COMMENT "Generating self-signed test certificate for HTTPLoader tests"
    )

    # HTTPLoader tests — embeds a Boost.Beast server in the test binary
    addtest(http_loader_test
        http_loader_test.cpp
        ${TEST_CERT_DIR}/cert.pem
        ${TEST_CERT_DIR}/key.pem
    )
    target_compile_definitions(http_loader_test PRIVATE
        TEST_CERT_DIR="${TEST_CERT_DIR}"
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

    addtest(ipfs_device_test
        ipfs_device_test.cpp
    )
    target_link_libraries(ipfs_device_test
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
    # bitswap_node.hpp's Boost.DI TEMPLATE_TO macro trips newer clang's
    # -Wmissing-template-arg-list-after-template-kw (error by default);
    # same conformance drift as the example app. Match the project's
    # existing -Wno-* handling for toolchain noise.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(ipfs_device_test PRIVATE
            -Wno-missing-template-arg-list-after-template-kw
        )
    endif()
endif()
