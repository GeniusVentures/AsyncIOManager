cmake_minimum_required(VERSION 3.20)
set(
    CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/cmake/toolchain/cxx17.cmake"
    CACHE
    FILEPATH
    "Default toolchain"
)
add_definitions(-D_WIN32_WINNT=0x0601)
add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)

project(AsyncIOManager LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf CONFIG REQUIRED)

if(NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc IMPORTED)
endif()

if(EXISTS "${Protobuf_PROTOC_EXECUTABLE}")
    set_target_properties(protobuf::protoc PROPERTIES IMPORTED_LOCATION ${Protobuf_PROTOC_EXECUTABLE})
endif()

include(cmake/functions.cmake)

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${GSL_INCLUDE_DIR})
find_package(MNN CONFIG REQUIRED)
include_directories(${MNN_INCLUDE_DIR})
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)
find_package(soralog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(tsl_hat_trie CONFIG REQUIRED)
find_package(Boost.DI CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS date_time filesystem random regex system thread log program_options)
include_directories(${Boost_INCLUDE_DIRS})
find_package(libp2p CONFIG REQUIRED)
find_package(ipfs-lite-cpp CONFIG REQUIRED)
include_directories(${ipfs-lite-cpp_INCLUDE_DIR} ${CBOR_INCLUDE_DIR})
find_package(ipfs-bitswap-cpp CONFIG REQUIRED)
include_directories(${ipfs-bitswap-cpp_INCLUDE_DIR})
find_package(libssh2 CONFIG REQUIRED)

option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
endif()

set(VERSION_STRING 0)
set(SUBVERSION_STRING 1)

set(PROJECT.Path ${CMAKE_SOURCE_DIR}/src)
set(PROJECT.Source_DIR
    ${PROJECT.Path}
)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
add_subdirectory(${PROJECT.Path})

# Install Headers
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h*")

# Install LIB
install(TARGETS AsyncIOManager
    EXPORT AsyncIOManagerTargets DESTINATION "${CMAKE_INSTALL_LIBDIR}")

# Install Targets
set(AsyncIOManager_CONFIG_DESTINATION_DIR "lib/cmake/AsyncIOManager")
install(EXPORT AsyncIOManagerTargets
    FILE AsyncIOManagerTargets.cmake
    DESTINATION ${AsyncIOManager_CONFIG_DESTINATION_DIR}
)

include(CMakePackageConfigHelpers)

# generate the config file that is includes the exports
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/AsyncIOManagerConfig.cmake"
    INSTALL_DESTINATION "lib/cmake/examples"
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

# generate the version file for the config file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/AsyncIOManagerConfigVersion.cmake"
    VERSION "${VERSION_STRING}.${SUBVERSION_STRING}"
    COMPATIBILITY AnyNewerVersion
)

# install the configuration file
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/AsyncIOManagerConfig.cmake
    DESTINATION ${AsyncIOManager_CONFIG_DESTINATION_DIR}
)
