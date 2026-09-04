/**
 * Source file for the LocalFileCommon
 */
#include "LocalFileCommon.hpp"

namespace sgns
{
    using namespace boost::asio;

#ifndef _WIN32
    LocalFileDevice::LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode ) :
        file_( *ioc ), filename_( filename ), flags_( writemode == 0 ? O_RDONLY : O_WRONLY | O_CREAT )
    {
    } // Map writemode to basic POSIX flags (adjust as needed)

    boost::system::error_code LocalFileDevice::Open()
    {
        // POSIX-specific: Use file descriptor with ::open
        fd_ = ::open( filename_.c_str(),
                      flags_,
                      0666 ); // 0666 for creation mode (rw-rw-rw); adjust permissions if needed
        if ( fd_ == -1 )
        {
            ec_ = boost::system::error_code( errno, boost::system::system_category() );
            m_logger->error( "Failed to open file (POSIX): " + ec_.message() );
            return ec_;
        }
        file_.assign( fd_, ec_ );
        if ( ec_ )
        {
            m_logger->error( "Failed to assign file descriptor: " + ec_.message() );
            ::close( fd_ );
            fd_ = -1;
            return ec_;
        }
        m_logger->debug( "File opened successfully (POSIX)" );
        return ec_; // Success: ec_.value() == 0
    }
#else
    LocalFileDevice::LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode ) :
        file_( *ioc ), filename_( filename ), flags_( writemode )
    {
    }

    boost::system::error_code LocalFileDevice::Open()
    {
        // Windows-specific: Use Boost.Asio stream_file with mode based on flags_
        boost::asio::stream_file::flags mode = ( flags_ == 0 ) ? boost::asio::stream_file::read_only
                                                               : boost::asio::stream_file::write_only;
        // Add create if writing
        if ( flags_ == 1 )
        {
            mode |= boost::asio::stream_file::create;
        }

        file_.open( filename_, mode, ec_ );
        if ( ec_ )
        {
            m_logger->error( "Failed to open file (Windows): " + ec_.message() );
            return ec_;
        }
        m_logger->debug( "File opened successfully (Windows)" );
        return ec_; // Success: ec_.value() == 0
    }
#endif

}
