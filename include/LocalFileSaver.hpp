/*
 * LocalFileSaver.hpp
 */

#pragma once

#include "FileSaver.hpp"
#include "ASIOSingleton.hpp"

namespace sgns
{
    /// @brief class to handle "file://" prefix in a filename to save to local disk
    class LocalFileSaver : public FileSaver
    {
        SINGLETON_PTR( LocalFileSaver );

    public:
        enum class Error
        {
            READ_ERROR     = 1,
            FILE_OPEN_FAIL = 2,
        };
        static void InitializeSingleton();
        /// @brief save a file to local disk, throws on error
        /// @param filename filename to save the file as
        virtual void SaveFile( std::string filename, std::shared_ptr<void> data ) override;
        virtual void SaveASync( std::shared_ptr<boost::asio::io_context>                            ioc,
                                std::function<void( std::shared_ptr<boost::asio::io_context> ioc )> handle_write,
                                std::string                                                         filename,
                                ResultType                                                          data,
                                std::string                                                         suffix,
                                std::shared_ptr<std::string>                                        save_location = nullptr ) override;
    };
} // End namespace sgns
