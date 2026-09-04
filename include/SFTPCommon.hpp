/**
 * Header file for the SFTPCommon
 */
#pragma once
#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>
#include <memory>
#include "FileManager.hpp"
#include "SFTPLoader.hpp"
#include "boost/asio.hpp"
#include "boost/asio/ssl.hpp"
#include "URLStringUtil.h"
#include "libssh2.h"
#include "libssh2_sftp.h"
#include <thread>
#include "FILEError.hpp"
using Success      = sgns::AsyncError::Success;
using CustomResult = sgns::AsyncError::CustomResult;

namespace sgns
{
    using namespace boost::asio;

    /**
	 * This class creates an SFTP Device and has a function to download
	 * from an SFTP server.
	 */
    class SFTPDevice : public std::enable_shared_from_this<SFTPDevice>
    {
    public:
        /**
		 * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
		 * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
		 * @param buffers - Contains path/data loaded
		 * @param save - Whether to save the file to local disk upon completion
		 */
        using CompletionCallback = std::function<void(
            std::shared_ptr<boost::asio::io_context>                                             ioc,
            std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers,
            bool                                                                                 save )>;
        /**
		 * Status callback returns an error code as an async load proceeds
		 * @param int - Status code
		 */
        using StatusCallback = std::function<void( const CustomResult & )>;

        /**
		 * Create an SFTP Device to load a file from SFTP. Will authenticate with priority towards private key, public key, and lastly user/pass
		 * @param sftp_host - address of SFTP
		 * @param sftp_path - File on SFTP to get
		 * @param sftp_user - Username for SFTP
		 * @param sftp_pass - Password for SFTP
		 * @param sftp_pubkeyfile - Public key file for logging in
		 * @param sftp_privkeyfile - Private key file for logging in
		 * @param sftp_Privkeypass - Private key pass for logging in
		 * @param callback - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 * @param save - Whether to save the file to local disk upon completion
		 */
        SFTPDevice( std::string sftp_host,
                    std::string sftp_path,
                    std::string sftp_user,
                    std::string sftp_pass,
                    std::string sftp_pubkeyfile,
                    std::string sftp_privkeyfile,
                    std::string sftp_privkeypass,
                    bool        save );

        ~SFTPDevice()
        {
            // Cleanup
        }

        /**
		 * Start downloading file on an SFTPDevice
		 * @param ioc - ASIO context for async loading
		 * @param tcpSocket - tcp socket for network 
		 * @param sftp2session - SFTP session 
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        void StartSFTPDownload( std::shared_ptr<boost::asio::io_context>      ioc,
                                std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                LIBSSH2_SESSION                              *sftp2session,
                                CompletionCallback                            handle_read,
                                StatusCallback                                status );

    private:
        /**
		 * Do a SFTP Handshake
		 * @param ioc - ASIO context for async loading
		 * @param sftp2session - SFTP session
		 * @param tcpSocket - tcp socket for network
		 * @param sock - native handle socket for operations
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        void StartSFTPHandshake( std::shared_ptr<boost::asio::io_context>                   ioc,
                                 LIBSSH2_SESSION                                           *sftp2session,
                                 std::shared_ptr<boost::asio::ip::tcp::socket>              tcpSocket,
                                 basic_socket<ip::tcp, any_io_executor>::native_handle_type sock,
                                 CompletionCallback                                         handle_read,
                                 StatusCallback                                             status );
        /**
		 * Authenticate SFTP
		 * @param ioc - ASIO context for async loading
		 * @param sftp2session - SFTP session
		 * @param tcpSocket - tcp socket for network
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        void StartSFTPAuth( std::shared_ptr<boost::asio::io_context>      ioc,
                            LIBSSH2_SESSION                              *sftp2session,
                            std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                            CompletionCallback                            handle_read,
                            StatusCallback                                status );
        /**
		 * Create SFTP 
		 * @param ioc - ASIO context for async loading
		 * @param sftp2session - SFTP session
		 * @param tcpSocket - tcp socket for network
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        void StartCreateSFTP( std::shared_ptr<boost::asio::io_context>      ioc,
                              LIBSSH2_SESSION                              *sftp2session,
                              std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                              CompletionCallback                            handle_read,
                              StatusCallback                                status );
        /**
		 * Create SFTP Handler to open file for read
		 * @param ioc - ASIO context for async loading
		 * @param sftp2session - SFTP session
		 * @param tcpSocket - tcp socket for network
		 * @param sftp - sftp 
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        void StartSFTPOpen( std::shared_ptr<boost::asio::io_context>      ioc,
                            LIBSSH2_SESSION                              *sftp2session,
                            std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                            LIBSSH2_SFTP                                 *sftp,
                            CompletionCallback                            handle_read,
                            StatusCallback                                status );
        /**
		 * Get file size to create appropriate buffer
		 * @param ioc - ASIO context for async loading
		 * @param sftp2session - SFTP session
		 * @param tcpSocket - tcp socket for network
		 * @param sftp - sftp
		 * @param sftpHandle - sftp handler that opened file for read
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        void StartSFTPGetSize( std::shared_ptr<boost::asio::io_context>      ioc,
                               LIBSSH2_SESSION                              *sftp2session,
                               std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                               LIBSSH2_SFTP                                 *sftp,
                               LIBSSH2_SFTP_HANDLE                          *sftpHandle,
                               CompletionCallback                            handle_read,
                               StatusCallback                                status );
        /**
		 * Download the file
		 * @param ioc - ASIO context for async loading
		 * @param sftp2session - SFTP session
		 * @param tcpSocket - tcp socket for network
		 * @param sftp - sftp
		 * @param sftpHandle - sftp handler that opened file for read
		 * @param buffer - buffer created based on file size
		 * @param handle_read - Filemanager callback on completion
		 * @param status - Status function that will be updated with status codes as operation progresses
		 */
        void StartSFTPGetBlocks( std::shared_ptr<boost::asio::io_context>      ioc,
                                 LIBSSH2_SESSION                              *sftp2session,
                                 std::shared_ptr<boost::asio::ip::tcp::socket> tcpSocket,
                                 LIBSSH2_SFTP                                 *sftp,
                                 LIBSSH2_SFTP_HANDLE                          *sftpHandle,
                                 std::shared_ptr<std::vector<char>>            buffer,
                                 size_t                                        totalBytesRead,
                                 CompletionCallback                            handle_read,
                                 StatusCallback                                status );
        /**
		 * Clean up SFTP2 items
		 * @param sftp2session - SFTP session
		 * @param sftp - sftp
		 * @param sftpHandle - sftp handler that opened file for read
		 */
        void StartSFTPCleanup( LIBSSH2_SESSION *sftp2session, LIBSSH2_SFTP_HANDLE *sftpHandle, LIBSSH2_SFTP *sftp );

        //Common vars used for getting file from SFTP
        std::size_t file_size_;
        std::string sftp_host_;
        std::string sftp_path_;
        std::string sftp_user_;
        std::string sftp_pass_;
        std::string sftp_pubkeyfile_;
        std::string sftp_privkeyfile_;
        std::string sftp_privkeypass_;
        bool        save_;
        bool        downloading_ = false;
    };
}
