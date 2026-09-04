/**
 * Source file for the LocalFileCommon (POSIX)
 */
#include "LocalFileCommon.hpp"

namespace sgns
{
    using namespace boost::asio;

    LocalFileDevice::LocalFileDevice( std::shared_ptr<boost::asio::io_context> ioc, std::string filename, int writemode ) :
        file_( *ioc ), filename_( filename ),
        flags_( writemode == 0 ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC ) // D-12: O_TRUNC for overwrite semantics
    {
    }

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
            ::close( fd_ ); // error-cleanup path — NOT the D-13 double-close; stays
            fd_ = -1;
            return ec_;
        }
        m_logger->debug( "File opened successfully (POSIX)" );
        return ec_; // Success: ec_.value() == 0
    }

} // End namespace sgns
