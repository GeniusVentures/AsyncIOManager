/**
 * Header file for the LocalFileCommon (POSIX)
 */
#pragma once
#include <iostream>
#include <memory>
#include "boost/asio.hpp"
#include <asiomgr-logger.hpp>
#include <libp2p/outcome/outcome.hpp>
#include <fcntl.h>
#include "boost/asio/posix/stream_descriptor.hpp"

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    using namespace boost::asio;

/**
 * This class creates a FILE Device and has a function load a local file (POSIX).
 */
    class LocalFileDevice : public std::enable_shared_from_this<LocalFileDevice>
    {
    public:
        /**
         * Create a FILE Device to load a file from local. 
         * @param ioc - Boost asio io_context to use
         * @param filename - Path to location of file to load
         * @param writemode - 0 for read, 1 for write
         */
        LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode );

        ~LocalFileDevice()
        {
            // Cleanup
            // D-13: only file_.close() — stream_descriptor::close(ec) closes the
            // underlying descriptor even on error (Boost 1.85 basic_descriptor.hpp);
            // an extra close(fd_) here would be a latent double-close/EBADF.
            file_.close( ec_ );
        }

        boost::system::error_code Open();

        /**
         * Get the current file pointer for async operations
         */
        boost::asio::posix::stream_descriptor &getFile()
        {
            return file_;
        }

    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "LocalFileCommon" );
        //Common vars used for file loading
        boost::asio::posix::stream_descriptor file_;
        boost::system::error_code             ec_;
        std::string                           filename_;
        int                                   flags_;
        int                                   fd_ = -1;
    };

} // End namespace sgns
