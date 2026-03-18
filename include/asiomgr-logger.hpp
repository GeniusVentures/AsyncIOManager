#pragma once

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

#if defined( ANDROID )
#include <spdlog/sinks/android_sink.h>
#endif

namespace sgns::asiomgr
{
    using Logger = std::shared_ptr<spdlog::logger>;

    /**
   * Provide logger object
   * @param tag - tagging name for identifying logger
   * @return logger object
   */
    Logger createLogger( const std::string &tag, const std::string &basepath = "" );
} // namespace sgns::sgprocmanager
