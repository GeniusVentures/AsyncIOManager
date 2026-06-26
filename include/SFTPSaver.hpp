/**
 * Header file for the SFTPSaver, saving files to SFTP
 */

#pragma once

#include <memory>
#include <string>
#include "FileSaver.hpp"
#include "ASIOSingleton.hpp"

namespace sgns
{
    /**
     * This class is for saving files to SFTP servers using libssh2.
     * Supports authentication via password, public key, or private key.
     */
    class SFTPSaver : public FileSaver
    {
        SINGLETON_PTR( SFTPSaver );

    public:
        static void InitializeSingleton();

        /// @brief Save a file to SFTP synchronously, throws on error
        /// @param filename Full SFTP URL with credentials and path
        /// @param data File data to save
        virtual void SaveFile( std::string filename, std::shared_ptr<void> data ) override;

        /// @brief Asynchronously save data to SFTP
        /// @param ioc ASIO context for async operations
        /// @param handle_write Callback invoked when write completes
        /// @param filename Target filename (SFTP path component)
        /// @param data File data to save (path/name pairs + content)
        /// @param suffix File suffix/extension
        /// @param save_location Output parameter — saver writes the resulting SFTP URL here
        virtual void SaveASync( std::shared_ptr<boost::asio::io_context>                            ioc,
                                std::function<void( std::shared_ptr<boost::asio::io_context> ioc )> handle_write,
                                std::string                                                         filename,
                                ResultType                                                          data,
                                std::string                                                         suffix,
                                std::shared_ptr<std::string>                                        save_location = nullptr ) override;

    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "SFTPSaver" );
    };

} // End namespace sgns
