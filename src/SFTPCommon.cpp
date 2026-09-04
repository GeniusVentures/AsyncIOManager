/**
 * Source file for the SFTPCommon
 */
#include "SFTPCommon.hpp"

namespace sgns
{
    using namespace boost::asio;

    SFTPDevice::SFTPDevice( std::string sftp_host,
                            std::string sftp_path,
                            std::string sftp_user,
                            std::string sftp_pass,
                            std::string sftp_pubkeyfile,
                            std::string sftp_privkeyfile,
                            std::string sftp_privkeypass,
                            bool        save )
    {
        sftp_host_        = sftp_host;
        sftp_path_        = sftp_path;
        sftp_user_        = sftp_user;
        sftp_pass_        = sftp_pass;
        sftp_pubkeyfile_  = sftp_pubkeyfile;
        sftp_privkeyfile_ = sftp_privkeyfile;
        sftp_privkeypass_ = sftp_privkeypass;
        save_             = save;
    }

    void SFTPDevice::StartSFTPDownload( std::shared_ptr<boost::asio::io_context>      ioc,
                                        std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                        LIBSSH2_SESSION                              *sftp2session,
                                        CompletionCallback                            handle_read,
                                        StatusCallback                                status )
    {
        if ( downloading_ )
        {
            std::cerr << "Already downloading" << std::endl;
            return;
        }
        downloading_ = true;
        ip::tcp::resolver                            resolver( *ioc );
        boost::asio::ip::tcp::resolver::results_type resolvedaddr;
        try
        {
            resolvedaddr = resolver.resolve( sftp_host_, "22" );
        }
        catch ( const boost::system::system_error &e )
        {
            std::cerr << "Error resolving address: " << e.what() << std::endl;
            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Could not resolve address" ) ) );
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Exception: " << e.what() << std::endl;
            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Could not resolve address" ) ) );
        }
        catch ( ... )
        {
            std::cerr << "Unknown error occurred during address resolution." << std::endl;
            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Could not resolve address" ) ) );
        }

        //auto tcpSocket = std::make_shared<boost::asio::ip::tcp::socket>(*ioc);
        status( CustomResult( sgns::AsyncError::outcome::success( Success{ "Starting SFTP Connection" } ) ) );
        async_connect(
            *tcpSocket,
            resolvedaddr,
            [self = shared_from_this(), ioc, sftp2session, tcpSocket, handle_read, status](
                const boost::system::error_code &connect_error,
                const auto & )
            {
                if ( !connect_error )
                {
                    //Setup Socket
                    auto sock = tcpSocket->native_handle();
                    libssh2_session_set_blocking( sftp2session, 0 );
                    status(
                        CustomResult( sgns::AsyncError::outcome::success( Success{ "SFTP SSL Handshake Started" } ) ) );
                    self->StartSFTPHandshake( ioc, sftp2session, tcpSocket, sock, handle_read, status );
                }
                else
                {
                    std::cerr << "Error connecting to server: " << connect_error.message() << std::endl;
                    status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Connection Error" ) ) );
                    handle_read( ioc,
                                 std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                                 false );
                }
            } );
    }

    void SFTPDevice::StartSFTPHandshake( std::shared_ptr<boost::asio::io_context>                   ioc,
                                         LIBSSH2_SESSION                                           *sftp2session,
                                         std::shared_ptr<boost::asio::ip::tcp::socket>              tcpSocket,
                                         basic_socket<ip::tcp, any_io_executor>::native_handle_type sock,
                                         CompletionCallback                                         handle_read,
                                         StatusCallback                                             status )
    {
        int rc = libssh2_session_handshake( sftp2session, sock );

        if ( rc == 0 )
        {
            // Handshake successful, proceed with authentication
            status( CustomResult( sgns::AsyncError::outcome::success( Success{ "Starting SFTP Auth" } ) ) );
            StartSFTPAuth( ioc, sftp2session, tcpSocket, handle_read, status );
        }
        else if ( rc == LIBSSH2_ERROR_EAGAIN )
        {
            tcpSocket->async_wait(
                boost::asio::socket_base::wait_read,
                [self = shared_from_this(), ioc, sftp2session, tcpSocket, sock, handle_read, status](
                    const boost::system::error_code &ec )
                {
                    if ( !ec )
                    {
                        // Continue with handshake
                        self->StartSFTPHandshake( ioc, sftp2session, tcpSocket, sock, handle_read, status );
                    }
                    else
                    {
                        // Handle error
                        status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Handshake Error" ) ) );
                        handle_read(
                            ioc,
                            std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                            false );
                    }
                } );
        }
        else
        {
            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Handshake Error" ) ) );
            handle_read( ioc,
                         std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                         false );
        }
    }

    void SFTPDevice::StartSFTPAuth( std::shared_ptr<boost::asio::io_context>      ioc,
                                    LIBSSH2_SESSION                              *sftp2session,
                                    std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                    CompletionCallback                            handle_read,
                                    StatusCallback                                status )
    {
        int auth_result;
        if ( !sftp_privkeyfile_.empty() )
        {
            auth_result = libssh2_userauth_publickey_fromfile( sftp2session,
                                                               sftp_user_.c_str(),
                                                               nullptr,
                                                               sftp_privkeyfile_.c_str(),
                                                               nullptr );
        }
        else
        {
            if ( !sftp_pubkeyfile_.empty() )
            {
                auth_result = libssh2_userauth_publickey_fromfile( sftp2session,
                                                                   sftp_user_.c_str(),
                                                                   nullptr,
                                                                   sftp_pubkeyfile_.c_str(),
                                                                   sftp_privkeypass_.c_str() );
            }
            else
            {
                auth_result = libssh2_userauth_password( sftp2session, sftp_user_.c_str(), sftp_pass_.c_str() );
            }
        }

        if ( auth_result == 0 )
        {
            // Auth successful, create sftp instance
            status( CustomResult( sgns::AsyncError::outcome::success( Success{ "Creating SFTP Handler" } ) ) );
            StartCreateSFTP( ioc, sftp2session, tcpSocket, handle_read, status );
        }
        else if ( auth_result == LIBSSH2_ERROR_EAGAIN )
        {
            tcpSocket->async_wait(
                socket_base::wait_read,
                [self = shared_from_this(), ioc, sftp2session, tcpSocket, handle_read, status](
                    const boost::system::error_code &ec )
                {
                    if ( !ec )
                    {
                        // Continue with auth
                        self->StartSFTPAuth( ioc, sftp2session, tcpSocket, handle_read, status );
                    }
                    else
                    {
                        // Handle error
                        status(
                            CustomResult( sgns::AsyncError::outcome::failure( "SFTP Fail, authentication fail" ) ) );
                        handle_read(
                            ioc,
                            std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                            false );
                    }
                } );
        }
        else
        {
            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Fail, authentication fail" ) ) );
            handle_read( ioc,
                         std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                         false );
        }
    }

    void SFTPDevice::StartCreateSFTP( std::shared_ptr<boost::asio::io_context>      ioc,
                                      LIBSSH2_SESSION                              *sftp2session,
                                      std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                      CompletionCallback                            handle_read,
                                      StatusCallback                                status )
    {
        auto sftp = libssh2_sftp_init( sftp2session );
        if ( sftp == nullptr )
        {
            // Handle the error, if it is EAGAIN we need to continue
            int sftp_error_code = libssh2_session_last_errno( sftp2session );
            if ( sftp_error_code == LIBSSH2_ERROR_EAGAIN )
            {
                tcpSocket->async_wait(
                    boost::asio::socket_base::wait_read,
                    [self = shared_from_this(), ioc, sftp2session, tcpSocket, sftp, handle_read, status](
                        const boost::system::error_code &ec )
                    {
                        if ( !ec )
                        {
                            self->StartCreateSFTP( ioc, sftp2session, tcpSocket, handle_read, status );
                        }
                        else
                        {
                            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Create Error" ) ) );
                            handle_read(
                                ioc,
                                std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                                false );
                        }
                    } );
            }
            else
            {
                status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Create Error" ) ) );
                handle_read( ioc,
                             std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                             false );
            }
        }
        else
        {
            // SFTP instance initialization succeeded
            status( CustomResult( sgns::AsyncError::outcome::success( Success{ "Starting SFTP Open" } ) ) );
            StartSFTPOpen( ioc, sftp2session, tcpSocket, sftp, handle_read, status );
        }
    }

    void SFTPDevice::StartSFTPOpen( std::shared_ptr<boost::asio::io_context>      ioc,
                                    LIBSSH2_SESSION                              *sftp2session,
                                    std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                    LIBSSH2_SFTP                                 *sftp,
                                    CompletionCallback                            handle_read,
                                    StatusCallback                                status )
    {
        std::string fullPath   = "." + sftp_path_;
        auto        sftpHandle = libssh2_sftp_open( sftp, fullPath.c_str(), LIBSSH2_FXF_READ, 0 );
        if ( sftpHandle == nullptr )
        {
            // Handle the error, if it is EAGAIN we need to continue
            int sftp_error_code = libssh2_session_last_errno( sftp2session );
            if ( sftp_error_code == LIBSSH2_ERROR_EAGAIN )
            {
                tcpSocket->async_wait(
                    boost::asio::socket_base::wait_read,
                    [self = shared_from_this(), ioc, sftp2session, tcpSocket, sftp, handle_read, status](
                        const boost::system::error_code &ec )
                    {
                        if ( !ec )
                        {
                            self->StartSFTPOpen( ioc, sftp2session, tcpSocket, sftp, handle_read, status );
                        }
                        else
                        {
                            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Open Error" ) ) );
                            handle_read(
                                ioc,
                                std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                                false );
                        }
                    } );
            }
            else
            {
                status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Open Error" ) ) );
                handle_read( ioc,
                             std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                             false );
            }
        }
        else
        {
            //SFTP Opened, get file size
            status( CustomResult( sgns::AsyncError::outcome::success( Success{ "Getting SFTP File Size" } ) ) );
            StartSFTPGetSize( ioc, sftp2session, tcpSocket, sftp, sftpHandle, handle_read, status );
        }
    }

    void SFTPDevice::StartSFTPGetSize( std::shared_ptr<boost::asio::io_context>      ioc,
                                       LIBSSH2_SESSION                              *sftp2session,
                                       std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                       LIBSSH2_SFTP                                 *sftp,
                                       LIBSSH2_SFTP_HANDLE                          *sftpHandle,
                                       CompletionCallback                            handle_read,
                                       StatusCallback                                status )
    {
        std::string fullPath = "." + sftp_path_;
        // Get the size of the file
        LIBSSH2_SFTP_ATTRIBUTES sftpAttrs;
        int                     rc = libssh2_sftp_stat( sftp, fullPath.c_str(), &sftpAttrs );
        if ( rc == 0 )
        {
            //Got size, start reading to buffer
            file_size_  = sftpAttrs.filesize;
            auto buffer = std::make_shared<std::vector<char>>( file_size_ );
            status( CustomResult( sgns::AsyncError::outcome::success( Success{ "Reading SFTP File" } ) ) );
            StartSFTPGetBlocks( ioc, sftp2session, tcpSocket, sftp, sftpHandle, buffer, 0, handle_read, status );
        }
        else if ( rc == LIBSSH2_ERROR_EAGAIN )
        {
            //Continue getting size
            tcpSocket->async_wait(
                socket_base::wait_read,
                [self = shared_from_this(), ioc, sftp2session, tcpSocket, sftp, sftpHandle, handle_read, status](
                    const boost::system::error_code &ec )
                {
                    if ( !ec )
                    {
                        self->StartSFTPGetSize( ioc, sftp2session, tcpSocket, sftp, sftpHandle, handle_read, status );
                    }
                    else
                    {
                        status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP File Size does not match" ) ) );
                        handle_read(
                            ioc,
                            std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                            false );
                    }
                } );
        }
        else
        {
            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP File Size does not match" ) ) );
            handle_read( ioc,
                         std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                         false );
        }
    }

    void SFTPDevice::StartSFTPGetBlocks( std::shared_ptr<boost::asio::io_context>      ioc,
                                         LIBSSH2_SESSION                              *sftp2session,
                                         std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                         LIBSSH2_SFTP                                 *sftp,
                                         LIBSSH2_SFTP_HANDLE                          *sftpHandle,
                                         std::shared_ptr<std::vector<char>>            buffer,
                                         size_t                                        totalBytesRead,
                                         CompletionCallback                            handle_read,
                                         StatusCallback                                status )
    {
        //libssh2_session_set_blocking(sftp2session_, 0);
        int rc;
        rc = libssh2_sftp_read( sftpHandle, buffer->data() + totalBytesRead, buffer->size() - totalBytesRead );
        //std::cout << "Reading: " << rc << std::endl;

        if ( rc > 0 )
        {
            totalBytesRead += rc;
            // Process data if available
            if ( totalBytesRead >= buffer->size() )
            {
                //We've read all the data, send to save
                std::cout << "SFTP Finish" << std::endl;
                status( CustomResult( sgns::AsyncError::outcome::success( Success{ "SFTP Read Finished" } ) ) );
                auto finaldata =
                    std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
                std::filesystem::path p( sftp_path_ );
                finaldata->first.push_back( p.filename().string() );
                finaldata->second.push_back( *buffer );
                handle_read( ioc, finaldata, save_ );
            }
            else
            {
                //Async wait for the next part
                tcpSocket->async_wait(
                    boost::asio::socket_base::wait_read,
                    [self = shared_from_this(),
                     ioc,
                     sftp2session,
                     tcpSocket,
                     sftp,
                     sftpHandle,
                     buffer,
                     totalBytesRead,
                     handle_read,
                     status]( const boost::system::error_code &ec )
                    {
                        if ( !ec )
                        {
                            // Socket is readable, continue the SFTP read operation
                            self->StartSFTPGetBlocks( ioc,
                                                      sftp2session,
                                                      tcpSocket,
                                                      sftp,
                                                      sftpHandle,
                                                      buffer,
                                                      totalBytesRead,
                                                      handle_read,
                                                      status );
                        }
                        else
                        {
                            // Handle error
                            status( CustomResult(
                                sgns::AsyncError::outcome::failure( "SFTP Read Failure. Next Part not obtained" ) ) );
                            self->StartSFTPCleanup( sftp2session, sftpHandle, sftp );
                            handle_read(
                                ioc,
                                std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                                false );
                        }
                    } );
            }
        }
        else if ( rc == LIBSSH2_ERROR_EAGAIN )
        {
            // Operation would block, set up asynchronous wait
            tcpSocket->async_wait(
                boost::asio::socket_base::wait_read,
                [self = shared_from_this(),
                 ioc,
                 sftp2session,
                 tcpSocket,
                 sftp,
                 sftpHandle,
                 buffer,
                 totalBytesRead,
                 handle_read,
                 status]( const boost::system::error_code &ec )
                {
                    if ( !ec )
                    {
                        // Socket is readable, continue the SFTP read operation
                        self->StartSFTPGetBlocks( ioc,
                                                  sftp2session,
                                                  tcpSocket,
                                                  sftp,
                                                  sftpHandle,
                                                  buffer,
                                                  totalBytesRead,
                                                  handle_read,
                                                  status );
                    }
                    else
                    {
                        // Handle error
                        status( CustomResult(
                            sgns::AsyncError::outcome::failure( "SFTP Read Failed. Socket not readable" ) ) );
                        self->StartSFTPCleanup( sftp2session, sftpHandle, sftp );
                        handle_read(
                            ioc,
                            std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                            false );
                    }
                } );
        }
        else
        {
            // Handle other errors
            status( CustomResult( sgns::AsyncError::outcome::failure( "SFTP Read Failed." ) ) );
            StartSFTPCleanup( sftp2session, sftpHandle, sftp );
            handle_read( ioc,
                         std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(),
                         false );
        }
    }

    void SFTPDevice::StartSFTPCleanup( LIBSSH2_SESSION     *sftp2session,
                                       LIBSSH2_SFTP_HANDLE *sftpHandle,
                                       LIBSSH2_SFTP        *sftp )
    {
        libssh2_sftp_close_handle( sftpHandle );
        libssh2_sftp_shutdown( sftp );
        libssh2_session_disconnect( sftp2session, "Normal Shutdown" );
        libssh2_session_free( sftp2session );
        libssh2_exit();
    }
}
