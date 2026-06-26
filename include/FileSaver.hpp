// FileSaver.hpp

#pragma once

#include <string>
#include <memory>

using namespace std;

class FileSaver
{
public:
    using ResultType =
        outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>;

    virtual ~FileSaver() {}

    virtual void SaveFile( std::string filename, shared_ptr<void> data ) = 0;
    /// @brief Asynchronously save data
    /// @param ioc ASIO context for async operations
    /// @param handle_write Callback invoked when write completes
    /// @param filename Target filename/path component
    /// @param data File data to save (path/name pairs + content)
    /// @param suffix File suffix/extension
    /// @param save_location Output parameter — saver writes the resulting location (file path, IPFS CID, URL, etc.) here
    virtual void SaveASync( std::shared_ptr<boost::asio::io_context>                            ioc,
                            std::function<void( std::shared_ptr<boost::asio::io_context> ioc )> handle_write,
                            std::string                                                         filename,
                            ResultType                                                          data,
                            std::string                                                         suffix,
                            std::shared_ptr<std::string>                                        save_location = nullptr ) = 0;
};
