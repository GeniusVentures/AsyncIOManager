/**
 * Header file for the LocalFileCommon
 */
#pragma once
#include <iostream>
#include <memory>
#include "boost/asio.hpp"
#include <asiomgr-logger.hpp>
#include <libp2p/outcome/outcome.hpp>

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    using namespace boost::asio;
} // End namespace sgns

// Platform selection is injected by CMake (src/CMakeLists.txt):
//   WIN32 -> ASIOMGR_LOCALFILE_HEADER="LocalFileCommon.win.hpp"
//   UNIX  -> ASIOMGR_LOCALFILE_HEADER="LocalFileCommon.posix.hpp"
#if !defined( ASIOMGR_LOCALFILE_HEADER )
#error "ASIOMGR_LOCALFILE_HEADER must be defined by the build system (see src/CMakeLists.txt)"
#endif
#include ASIOMGR_LOCALFILE_HEADER
