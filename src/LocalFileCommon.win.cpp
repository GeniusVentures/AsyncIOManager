/**
 * Source file for the LocalFileCommon (Windows)
 */
#include "LocalFileCommon.hpp"

namespace sgns
{
    using namespace boost::asio;

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

} // End namespace sgns
