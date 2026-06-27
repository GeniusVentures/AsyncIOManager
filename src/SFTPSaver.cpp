/**
 * Source file for the SFTPSaver, saving files to SFTP
 */

#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>
#include <memory>
#include <thread>
#include "FileManager.hpp"
#include "SFTPSaver.hpp"
#include "URLStringUtil.h"
#include "libssh2.h"
#include "libssh2_sftp.h"

namespace sgns
{
    using namespace boost::asio;

    /**
     * Internal device class that handles the async SFTP upload pipeline.
     * Mirrors the SFTPDevice download pattern but reverses direction for writes.
     */
    class SFTPUploadDevice : public std::enable_shared_from_this<SFTPUploadDevice>
    {
    public:
        using WriteCallback = std::function<void( std::shared_ptr<boost::asio::io_context> ioc )>;

        SFTPUploadDevice( std::string sftp_host,
                          std::string sftp_path,
                          std::string sftp_user,
                          std::string sftp_pass,
                          std::string sftp_pubkeyfile,
                          std::string sftp_privkeyfile,
                          std::string sftp_privkeypass,
                          std::vector<char> file_content,
                          WriteCallback   handle_write,
                          std::shared_ptr<std::string> save_location )
        {
            sftp_host_        = std::move( sftp_host );
            sftp_path_        = std::move( sftp_path );
            sftp_user_        = std::move( sftp_user );
            sftp_pass_        = std::move( sftp_pass );
            sftp_pubkeyfile_  = std::move( sftp_pubkeyfile );
            sftp_privkeyfile_ = std::move( sftp_privkeyfile );
            sftp_privkeypass_ = std::move( sftp_privkeypass );
            file_content_     = std::move( file_content );
            handle_write_     = std::move( handle_write );
            save_location_    = std::move( save_location );
        }

        ~SFTPUploadDevice()
        {
            // Cleanup handled in StartSFTPCleanup
        }

        void StartSFTPUpload( std::shared_ptr<boost::asio::io_context>      ioc,
                              std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                              LIBSSH2_SESSION                              *sftp2session )
        {
            if ( uploading_ )
            {
                std::cerr << "Already uploading" << std::endl;
                return;
            }
            uploading_ = true;

            ip::tcp::resolver                            resolver( *ioc );
            boost::asio::ip::tcp::resolver::results_type resolvedaddr;

            // Extract port from host if present (e.g., "127.0.0.1:2222" → host="127.0.0.1", port="2222")
            std::string resolveHost = sftp_host_;
            std::string resolvePort = "22";
            auto        colonPos    = sftp_host_.rfind( ':' );
            if ( colonPos != std::string::npos )
            {
                resolvePort = sftp_host_.substr( colonPos + 1 );
                resolveHost = sftp_host_.substr( 0, colonPos );
            }

            try
            {
                resolvedaddr = resolver.resolve( resolveHost, resolvePort );
            }
            catch ( const boost::system::system_error &e )
            {
                std::cerr << "Error resolving address: " << e.what() << std::endl;
                DoWriteCallback( ioc );
                return;
            }
            catch ( const std::exception &e )
            {
                std::cerr << "Exception: " << e.what() << std::endl;
                DoWriteCallback( ioc );
                return;
            }
            catch ( ... )
            {
                std::cerr << "Unknown error occurred during address resolution." << std::endl;
                DoWriteCallback( ioc );
                return;
            }

            // Log resolved addresses for diagnostics
            for ( const auto &ep : resolvedaddr )
            {
                std::cerr << "[SFTPSaver] Resolved " << resolveHost << ":" << resolvePort
                          << " -> " << ep.endpoint().address().to_string()
                          << ":" << ep.endpoint().port() << std::endl;
            }

            async_connect(
                *tcpSocket,
                resolvedaddr,
                [self = shared_from_this(), ioc, sftp2session, tcpSocket](
                    const boost::system::error_code &connect_error,
                    const auto & )
                {
                    if ( !connect_error )
                    {
                        auto sock = tcpSocket->native_handle();
                        libssh2_session_set_blocking( sftp2session, 0 );
                        self->StartSFTPHandshake( ioc, sftp2session, tcpSocket, sock );
                    }
                    else
                    {
                        std::cerr << "Error connecting to server: " << connect_error.message() << std::endl;
                        self->DoWriteCallback( ioc );
                    }
                } );
        }

    private:
        void DoWriteCallback( std::shared_ptr<boost::asio::io_context> ioc )
        {
            if ( handle_write_ )
            {
                boost::asio::post( *ioc, [cb = handle_write_, ioc]() { cb( ioc ); } );
            }
        }

        void StartSFTPHandshake( std::shared_ptr<boost::asio::io_context>                   ioc,
                                 LIBSSH2_SESSION                                           *sftp2session,
                                 std::shared_ptr<boost::asio::ip::tcp::socket>              tcpSocket,
                                 basic_socket<ip::tcp, any_io_executor>::native_handle_type sock )
        {
            int rc = libssh2_session_handshake( sftp2session, sock );

            if ( rc == 0 )
            {
                StartSFTPAuth( ioc, sftp2session, tcpSocket );
            }
            else if ( rc == LIBSSH2_ERROR_EAGAIN )
            {
                tcpSocket->async_wait(
                    boost::asio::socket_base::wait_read,
                    [self = shared_from_this(), ioc, sftp2session, tcpSocket, sock](
                        const boost::system::error_code &ec )
                    {
                        if ( !ec )
                        {
                            self->StartSFTPHandshake( ioc, sftp2session, tcpSocket, sock );
                        }
                        else
                        {
                            self->DoWriteCallback( ioc );
                        }
                    } );
            }
            else
            {
                DoWriteCallback( ioc );
            }
        }

        void StartSFTPAuth( std::shared_ptr<boost::asio::io_context>      ioc,
                            LIBSSH2_SESSION                              *sftp2session,
                            std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket )
        {
            int auth_result;
            if ( !sftp_privkeyfile_.empty() )
            {
                auth_result = libssh2_userauth_publickey_fromfile( sftp2session,
                                                                   sftp_user_.c_str(),
                                                                   nullptr,
                                                                   sftp_privkeyfile_.c_str(),
                                                                   sftp_privkeypass_.c_str() );
            }
            else if ( !sftp_pubkeyfile_.empty() )
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

            if ( auth_result == 0 )
            {
                StartCreateSFTP( ioc, sftp2session, tcpSocket );
            }
            else if ( auth_result == LIBSSH2_ERROR_EAGAIN )
            {
                tcpSocket->async_wait(
                    socket_base::wait_read,
                    [self = shared_from_this(), ioc, sftp2session, tcpSocket](
                        const boost::system::error_code &ec )
                    {
                        if ( !ec )
                        {
                            self->StartSFTPAuth( ioc, sftp2session, tcpSocket );
                        }
                        else
                        {
                            self->DoWriteCallback( ioc );
                        }
                    } );
            }
            else
            {
                std::cerr << "[SFTPSaver] Auth failed: rc=" << auth_result
                          << " user=" << sftp_user_ << " key=" << sftp_privkeyfile_ << std::endl;
                DoWriteCallback( ioc );
            }
        }

        void StartCreateSFTP( std::shared_ptr<boost::asio::io_context>      ioc,
                              LIBSSH2_SESSION                              *sftp2session,
                              std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket )
        {
            auto sftp = libssh2_sftp_init( sftp2session );
            if ( sftp == nullptr )
            {
                int sftp_error_code = libssh2_session_last_errno( sftp2session );
                if ( sftp_error_code == LIBSSH2_ERROR_EAGAIN )
                {
                    tcpSocket->async_wait(
                        boost::asio::socket_base::wait_read,
                        [self = shared_from_this(), ioc, sftp2session, tcpSocket](
                            const boost::system::error_code &ec )
                        {
                            if ( !ec )
                            {
                                self->StartCreateSFTP( ioc, sftp2session, tcpSocket );
                            }
                            else
                            {
                                self->DoWriteCallback( ioc );
                            }
                        } );
                }
                else
                {
                    DoWriteCallback( ioc );
                }
            }
            else
            {
                StartSFTPOpen( ioc, sftp2session, tcpSocket, sftp );
            }
        }

        void StartSFTPOpen( std::shared_ptr<boost::asio::io_context>      ioc,
                            LIBSSH2_SESSION                              *sftp2session,
                            std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                            LIBSSH2_SFTP                                 *sftp )
        {
            std::string fullPath = "." + sftp_path_;
            // Open file for writing: create if not exists, truncate if exists
            auto sftpHandle = libssh2_sftp_open( sftp,
                                                 fullPath.c_str(),
                                                 LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                                 LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
                                                     LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH );

            if ( sftpHandle == nullptr )
            {
                int sftp_error_code = libssh2_session_last_errno( sftp2session );
                if ( sftp_error_code == LIBSSH2_ERROR_EAGAIN )
                {
                    tcpSocket->async_wait(
                        boost::asio::socket_base::wait_read,
                        [self = shared_from_this(), ioc, sftp2session, tcpSocket, sftp](
                            const boost::system::error_code &ec )
                        {
                            if ( !ec )
                            {
                                self->StartSFTPOpen( ioc, sftp2session, tcpSocket, sftp );
                            }
                            else
                            {
                                self->DoWriteCallback( ioc );
                            }
                        } );
                }
                else
                {
                    DoWriteCallback( ioc );
                }
            }
            else
            {
                // Report the save location
                if ( save_location_ )
                {
                    *save_location_ = "sftp://" + sftp_user_ + "@" + sftp_host_ + sftp_path_;
                }

                StartSFTPWriteBlocks( ioc, sftp2session, tcpSocket, sftp, sftpHandle, 0 );
            }
        }

        void StartSFTPWriteBlocks( std::shared_ptr<boost::asio::io_context>      ioc,
                                   LIBSSH2_SESSION                              *sftp2session,
                                   std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                   LIBSSH2_SFTP                                 *sftp,
                                   LIBSSH2_SFTP_HANDLE                          *sftpHandle,
                                   size_t                                        totalBytesWritten )
        {
            size_t remaining = file_content_.size() - totalBytesWritten;
            if ( remaining == 0 )
            {
                // All data written, cleanup and signal completion
                StartSFTPCleanup( sftp2session, sftpHandle, sftp );
                DoWriteCallback( ioc );
                return;
            }

            ssize_t rc = libssh2_sftp_write( sftpHandle,
                                             file_content_.data() + totalBytesWritten,
                                             remaining );

            if ( rc > 0 )
            {
                totalBytesWritten += rc;
                if ( totalBytesWritten >= file_content_.size() )
                {
                    // Done writing
                    StartSFTPCleanup( sftp2session, sftpHandle, sftp );
                    DoWriteCallback( ioc );
                }
                else
                {
                    // Continue writing
                    tcpSocket->async_wait(
                        boost::asio::socket_base::wait_write,
                        [self = shared_from_this(),
                         ioc,
                         sftp2session,
                         tcpSocket,
                         sftp,
                         sftpHandle,
                         totalBytesWritten]( const boost::system::error_code &ec )
                        {
                            if ( !ec )
                            {
                                self->StartSFTPWriteBlocks( ioc,
                                                            sftp2session,
                                                            tcpSocket,
                                                            sftp,
                                                            sftpHandle,
                                                            totalBytesWritten );
                            }
                            else
                            {
                                self->StartSFTPCleanup( sftp2session, sftpHandle, sftp );
                                self->DoWriteCallback( ioc );
                            }
                        } );
                }
            }
            else if ( rc == LIBSSH2_ERROR_EAGAIN )
            {
                tcpSocket->async_wait(
                    boost::asio::socket_base::wait_write,
                    [self = shared_from_this(),
                     ioc,
                     sftp2session,
                     tcpSocket,
                     sftp,
                     sftpHandle,
                     totalBytesWritten]( const boost::system::error_code &ec )
                    {
                        if ( !ec )
                        {
                            self->StartSFTPWriteBlocks( ioc,
                                                        sftp2session,
                                                        tcpSocket,
                                                        sftp,
                                                        sftpHandle,
                                                        totalBytesWritten );
                        }
                        else
                        {
                            self->StartSFTPCleanup( sftp2session, sftpHandle, sftp );
                            self->DoWriteCallback( ioc );
                        }
                    } );
            }
            else
            {
                // Write error
                StartSFTPCleanup( sftp2session, sftpHandle, sftp );
                DoWriteCallback( ioc );
            }
        }

        void StartSFTPCleanup( LIBSSH2_SESSION     *sftp2session,
                               LIBSSH2_SFTP_HANDLE *sftpHandle,
                               LIBSSH2_SFTP        *sftp )
        {
            if ( sftpHandle )
            {
                libssh2_sftp_close_handle( sftpHandle );
            }
            if ( sftp )
            {
                libssh2_sftp_shutdown( sftp );
            }
            if ( sftp2session )
            {
                libssh2_session_disconnect( sftp2session, "Normal Shutdown" );
                libssh2_session_free( sftp2session );
            }
            libssh2_exit();
        }

        // Connection parameters
        std::string sftp_host_;
        std::string sftp_path_;
        std::string sftp_user_;
        std::string sftp_pass_;
        std::string sftp_pubkeyfile_;
        std::string sftp_privkeyfile_;
        std::string sftp_privkeypass_;

        // File content to upload
        std::vector<char> file_content_;

        // Callbacks
        WriteCallback                handle_write_;
        std::shared_ptr<std::string> save_location_;

        bool uploading_ = false;
    };

    // ---------------------------------------------------------------------------
    // SFTPSaver implementation
    // ---------------------------------------------------------------------------

    SFTPSaver *SFTPSaver::_instance = nullptr;

    void SFTPSaver::InitializeSingleton()
    {
        if ( _instance == nullptr )
        {
            _instance = new SFTPSaver();
        }
    }

    SFTPSaver::SFTPSaver()
    {
        FileManager::GetInstance().RegisterSaver( "sftp", this );
    }

    void SFTPSaver::SaveFile( std::string filename, std::shared_ptr<void> data )
    {
        m_logger->info( "Inside the SFTPSaver::SaveFile Function" );

        if ( data == nullptr )
        {
            throw std::range_error( "Cannot save with null data" );
        }

        // Parse SFTP URL components
        std::string sftp_host;
        std::string sftp_path;
        std::string sftp_user;
        std::string sftp_pass;
        std::string sftp_pubkeyfile;
        std::string sftp_privkeyfile;
        std::string sftp_privkeypass;
        parseSFTPUrl( filename,
                      sftp_host,
                      sftp_path,
                      sftp_user,
                      sftp_pass,
                      sftp_pubkeyfile,
                      sftp_privkeyfile,
                      sftp_privkeypass );

        // Initialize libssh2
        int rc = libssh2_init( 0 );
        if ( rc != 0 )
        {
            throw std::runtime_error( "libssh2_init failed" );
        }

        // Create socket and connect
        boost::asio::io_context                 ioc;
        boost::asio::ip::tcp::resolver          resolver( ioc );

        std::string resolveHost = sftp_host;
        std::string resolvePort = "22";
        auto        colonPos    = sftp_host.rfind( ':' );
        if ( colonPos != std::string::npos )
        {
            resolvePort = sftp_host.substr( colonPos + 1 );
            resolveHost = sftp_host.substr( 0, colonPos );
        }

        boost::asio::ip::tcp::resolver::results_type endpoints = resolver.resolve( resolveHost, resolvePort );
        boost::asio::ip::tcp::socket            socket( ioc );
        boost::asio::connect( socket, endpoints );

        // Create session and handshake
        LIBSSH2_SESSION *session = libssh2_session_init();
        libssh2_session_set_blocking( session, 1 );
        rc = libssh2_session_handshake( session, socket.native_handle() );
        if ( rc )
        {
            libssh2_session_free( session );
            libssh2_exit();
            throw std::runtime_error( "SFTP handshake failed" );
        }

        // Authenticate
        int auth_result;
        if ( !sftp_privkeyfile.empty() )
        {
            auth_result = libssh2_userauth_publickey_fromfile( session,
                                                               sftp_user.c_str(),
                                                               nullptr,
                                                               sftp_privkeyfile.c_str(),
                                                               sftp_privkeypass.c_str() );
        }
        else if ( !sftp_pubkeyfile.empty() )
        {
            auth_result = libssh2_userauth_publickey_fromfile( session,
                                                               sftp_user.c_str(),
                                                               nullptr,
                                                               sftp_pubkeyfile.c_str(),
                                                               sftp_privkeypass.c_str() );
        }
        else
        {
            auth_result = libssh2_userauth_password( session, sftp_user.c_str(), sftp_pass.c_str() );
        }

        if ( auth_result )
        {
            libssh2_session_disconnect( session, "Auth failed" );
            libssh2_session_free( session );
            libssh2_exit();
            throw std::runtime_error( "SFTP authentication failed" );
        }

        // Init SFTP subsystem
        LIBSSH2_SFTP *sftp = libssh2_sftp_init( session );
        if ( !sftp )
        {
            libssh2_session_disconnect( session, "SFTP init failed" );
            libssh2_session_free( session );
            libssh2_exit();
            throw std::runtime_error( "SFTP init failed" );
        }

        // Open file for writing
        std::string         fullPath = "." + sftp_path;
        LIBSSH2_SFTP_HANDLE *sftpHandle =
            libssh2_sftp_open( sftp,
                               fullPath.c_str(),
                               LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                               LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP |
                                   LIBSSH2_SFTP_S_IROTH );

        if ( !sftpHandle )
        {
            libssh2_sftp_shutdown( sftp );
            libssh2_session_disconnect( session, "File open failed" );
            libssh2_session_free( session );
            libssh2_exit();
            throw std::runtime_error( "SFTP file open for write failed: " + sftp_path );
        }

        // Write data
        std::shared_ptr<std::string> fileContent = std::static_pointer_cast<std::string>( data );
        const char                  *writeData   = fileContent->c_str();
        size_t                       writeSize   = fileContent->size();
        size_t                       totalWritten = 0;

        while ( totalWritten < writeSize )
        {
            ssize_t written = libssh2_sftp_write( sftpHandle, writeData + totalWritten, writeSize - totalWritten );
            if ( written < 0 )
            {
                libssh2_sftp_close_handle( sftpHandle );
                libssh2_sftp_shutdown( sftp );
                libssh2_session_disconnect( session, "Write failed" );
                libssh2_session_free( session );
                libssh2_exit();
                throw std::runtime_error( "SFTP write failed" );
            }
            totalWritten += written;
        }

        // Cleanup
        libssh2_sftp_close_handle( sftpHandle );
        libssh2_sftp_shutdown( sftp );
        libssh2_session_disconnect( session, "Normal Shutdown" );
        libssh2_session_free( session );
        libssh2_exit();

        m_logger->info( "File saved successfully to sftp://{}@{}:{}", sftp_user, sftp_host, sftp_path );
    }

    void SFTPSaver::SaveASync( std::shared_ptr<boost::asio::io_context>                            ioc,
                               std::function<void( std::shared_ptr<boost::asio::io_context> ioc )> handle_write,
                               std::string                                                         filename,
                               ResultType                                                          data,
                               std::string                                                         suffix,
                               std::shared_ptr<std::string>                                        save_location )
    {
        m_logger->info( "SFTPSaver::SaveASync starting for: {}", filename );

        if ( !data.has_value() || !data.value() || data.value()->first.empty() )
        {
            m_logger->error( "Cannot save with null or empty data" );
            boost::asio::post( *ioc, [handle_write, ioc]() { handle_write( ioc ); } );
            return;
        }

        auto &filePaths    = data.value()->first;
        auto &fileContents = data.value()->second;

        if ( filePaths.size() != fileContents.size() )
        {
            m_logger->error( "Mismatch between file paths ({}) and contents ({})",
                             filePaths.size(),
                             fileContents.size() );
            boost::asio::post( *ioc, [handle_write, ioc]() { handle_write( ioc ); } );
            return;
        }

        // Parse SFTP URL from filename
        std::string sftp_host;
        std::string sftp_path;
        std::string sftp_user;
        std::string sftp_pass;
        std::string sftp_pubkeyfile;
        std::string sftp_privkeyfile;
        std::string sftp_privkeypass;
        parseSFTPUrl( filename,
                      sftp_host,
                      sftp_path,
                      sftp_user,
                      sftp_pass,
                      sftp_pubkeyfile,
                      sftp_privkeyfile,
                      sftp_privkeypass );

        // Use a shared counter for tracking multiple file uploads
        auto remainingWrites = std::make_shared<size_t>( filePaths.size() );

        for ( size_t i = 0; i < filePaths.size(); ++i )
        {
            // Build the full path for this file by appending the file path component
            std::string fullFilePath = sftp_path;
            if ( !fullFilePath.empty() && fullFilePath.back() != '/' )
            {
                fullFilePath += "/";
            }
            fullFilePath += filePaths[i];

            // Convert vector<char> to a shared vector for the upload device
            auto content = std::make_shared<std::vector<char>>( fileContents[i] );

            auto tcpSocket = std::make_shared<boost::asio::ip::tcp::socket>( *ioc );
            auto session   = libssh2_session_init();

            auto uploadDevice = std::make_shared<SFTPUploadDevice>(
                sftp_host,
                fullFilePath,
                sftp_user,
                sftp_pass,
                sftp_pubkeyfile,
                sftp_privkeyfile,
                sftp_privkeypass,
                *content,
                [ioc, handle_write, remainingWrites]( std::shared_ptr<boost::asio::io_context> cb_ioc )
                {
                    ( *remainingWrites )--;
                    if ( *remainingWrites <= 0 )
                    {
                        handle_write( cb_ioc );
                    }
                },
                save_location );

            uploadDevice->StartSFTPUpload( ioc, tcpSocket, session );
        }
    }

} // End namespace sgns
