/**
 * Source file for the FILECommon
 */
#include "FILECommon.hpp"


namespace sgns
{
    using namespace boost::asio;
#ifndef _WIN32
    FILEDevice::FILEDevice(std::shared_ptr<boost::asio::io_context> ioc,
        std::string filename, int writemode) : file_(*ioc)
    {
        if (writemode == 0)
        {
            fd_ = open(filename.c_str(), O_RDONLY);
        }
        else {
            fd_ = open(filename.c_str(), O_WRONLY);
        }
        if (fd_ == -1) {
            m_logger->error("Failed to open file");
            return;
        }
        file_.assign(fd_,ec_);
        if (ec_) {
            m_logger->error("Failed to assign file descriptor: {}", ec_.message());
        }
    }
#else
    FILEDevice::FILEDevice(std::shared_ptr<boost::asio::io_context> ioc,
        std::string filename, int writemode) : file_(*ioc)
    {
        try {
            if (writemode == 0)
            {
                file_.open(filename, boost::asio::stream_file::flags::read_only);
            }
            else {
                file_.open(filename, boost::asio::stream_file::flags::write_only);
            }
        }
        catch(const boost::system::system_error& er){
            m_logger->error("Error: {}", er.what());
        }
    }
#endif
}