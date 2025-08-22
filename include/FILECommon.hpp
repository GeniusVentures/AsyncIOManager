/**
 * Header file for the FILECommon
 */
#ifndef FILECOMMON_HPP
#define FILECOMMON_HPP
#ifndef _WIN32
#include <fcntl.h>
#include "boost/asio/posix/stream_descriptor.hpp"
#endif
#include <iostream>
#include <memory>
#include "boost/asio.hpp"
#include <asiomgr-logger.hpp>

namespace sgns
{
    using namespace boost::asio;
/**
 * This class creates a FILE Device and has a function load a local file.
 * The class differs based on Windows, or POSIX based OS.
 */
#ifndef _WIN32
    class FILEDevice : public std::enable_shared_from_this<FILEDevice> {
    public:
        /**
         * Create a FILE Device to load a file from local. 
         * @param ioc - Boost asio io_context to use
         * @param filename - Path to location of file to load
         * @param writemode - 0 for read, 1 for write
         */
        FILEDevice(std::shared_ptr<boost::asio::io_context> ioc,
            std::string filename, int writemode);
        ~FILEDevice() {
            // Cleanup
            file_.close(ec_);
            close(fd_);
        }
        /**
         * Get the current file pointer for async operations
         */
        boost::asio::posix::stream_descriptor& getFile() {
            return file_;
        }
    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger("FILECommon");
        //Common vars used for file loading
        boost::asio::posix::stream_descriptor file_;
        boost::system::error_code ec_;
        int fd_ = -1;
    };
#else
    class FILEDevice : public std::enable_shared_from_this<FILEDevice> {
    public:
        /**
         * Create a FILE Device to load a file from local.
         * @param ioc - Boost asio io_context to use
         * @param filename - Path to location of file to load
         * @param writemode - 0 for read, 1 for write
         */
        FILEDevice(std::shared_ptr<boost::asio::io_context> ioc,
            std::string filename, int writemode);
        ~FILEDevice() {
            // Cleanup
            file_.close();
        }
        /**
         * Get the current file pointer for async operations
         */
        boost::asio::stream_file& getFile() {
            return file_;
        }
    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger("FILECommon");
        //Common vars used for file loading
        boost::asio::stream_file file_;
        boost::system::error_code ec_;
    };
#endif

}

#endif