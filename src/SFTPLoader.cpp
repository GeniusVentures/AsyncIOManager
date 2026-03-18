#include <sstream>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>
#include <memory>
#include <iostream>
#include <thread>
#include "FileManager.hpp"
#include "SFTPLoader.hpp"
#include "SFTPCommon.hpp"

namespace sgns
{
    SFTPLoader *SFTPLoader::_instance = nullptr;

    void SFTPLoader::InitializeSingleton()
    {
        if ( _instance == nullptr )
        {
            _instance = new SFTPLoader();
        }
    }

    SFTPLoader::SFTPLoader()
    {
        FileManager::GetInstance().RegisterLoader( "sftp", this );
    }

    std::shared_ptr<void> SFTPLoader::LoadFile( std::string filename )
    {
        const char *dummyValue = "Inside the SFTPTLoader::LoadFile Function";
        // for this test, we don't need to delete the shared_ptr as the data is static, so pass null lambda delete function
        return { (void *)dummyValue, []( void * ) {} };
        /* TODO: scorpioluck20 - Need to implement this. How we load file base on format file?*/
    }

    std::shared_ptr<void> SFTPLoader::LoadASync( std::string                              filename,
                                                 bool                                     parse,
                                                 bool                                     save,
                                                 std::shared_ptr<boost::asio::io_context> ioc,
                                                 CompletionCallback                       handle_read,
                                                 StatusCallback                           status )
    {
        //Parse hostname and path
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
        //std::cout << "host " << sftp_host << std::endl;
        //std::cout << "path " << sftp_path << std::endl;
        //std::cout << "user " << sftp_user << std::endl;
        //std::cout << "pass " << sftp_pass << std::endl;
        //std::cout << "pubkeyfile " << sftp_pubkeyfile << std::endl;
        //std::cout << "privkeyfile " << sftp_privkeyfile << std::endl;
        //std::cout << "privkeypass " << sftp_privkeypass << std::endl;
        LIBSSH2_SESSION *session    = libssh2_session_init();
        auto             tcpSocket  = std::make_shared<boost::asio::ip::tcp::socket>( *ioc );
        auto             sftpDevice = std::make_shared<SFTPDevice>( sftp_host,
                                                        sftp_path,
                                                        sftp_user,
                                                        sftp_pass,
                                                        sftp_pubkeyfile,
                                                        sftp_privkeyfile,
                                                        sftp_privkeypass,
                                                        parse,
                                                        save );
        sftpDevice->StartSFTPDownload( ioc, tcpSocket, session, handle_read, status );

        std::shared_ptr<string> result = std::make_shared<string>( "test" );
        return result;
    }

} // End namespace sgns
