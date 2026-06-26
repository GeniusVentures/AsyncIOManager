cmake_minimum_required(VERSION 3.5.1)

# --------------------------------------------------------
# if on Windows system, build the windows executables
if (NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    message(FATAL_ERROR "${CMAKE_HOST_SYSTEM_NAME} host system name does not match Windows - Please select the correct folder for configuring project")
endif()

project(AsyncIOManager C CXX)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
add_definitions(-D_WIN32_WINNT=0x0601)


if (CMAKE_BUILD_TYPE EQUAL Debug)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MTd")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MTd")
    set(MSVC_RUNTIME_LIBRARY_OPTION "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif (CMAKE_BUILD_TYPE EQUAL Debug)

if (CMAKE_BUILD_TYPE EQUAL Release)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MT")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
    set(MSVC_RUNTIME_LIBRARY_OPTION "MultiThreaded$<$<CONFIG:Realease>:Release>")
endif (CMAKE_BUILD_TYPE EQUAL Release)

set(CompilerFlags
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
)
foreach(CompilerFlag ${CompilerFlags})
    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
endforeach()

# --------------------------------------------------------
# build common libraries by platforms
include(../CommonCompilerOptions.CMake)

# TODO: Check grpc install on Windows
# set(Protobuf_DIR  "${_THIRDPARTY_BUILD_DIR}/grpc/cmake")
# set(Protobuf_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/include/google/protobuf")

# --------------------------------------------------------
# Set toolchain file
set(CMAKE_TOOLCHAIN_FILE "${PROJECT_ROOT}/cmake/toolchain/cxx17.cmake")

# --------------------------------------------------------
# Set Windows crypto and socket library 
set(USER_ENV_LIBRARY userenv)
set(WIN_SOCK_LIBRARY wsock32 ws2_32)
set(WIN_CRYPT_LIBRARY crypt32)

# --------------------------------------------------------
# Allow multiple definitions
set(FORCE_MULTILE ON)
set(MULTIPLE_OPTION /FORCE:MULTIPLE)

# --------------------------------------------------------
# Set extra compiler flags
set(EXTRA_CXX_FLAGS)

# --------------------------------------------------------
# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "") 

include(ExternalProject)
include_directories(
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/include
)


# --------------------------------------------------------
# Include common build paramaeters
include(../CommonBuildParameters.cmake)

# install example binary to bin
SET(TESTAPP MNNExample )

install(TARGETS ${TESTAPP} DESTINATION "${BUILD_FILELOADER_DIR}/bin")
