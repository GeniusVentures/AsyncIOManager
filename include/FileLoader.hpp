// FileLoaders.hpp

#pragma once

#include <string>
#include "boost/asio.hpp"
#include <libp2p/outcome/outcome.hpp>

namespace outcome {
    using libp2p::outcome::result;
    using libp2p::outcome::success;
    using libp2p::outcome::failure;
}

class FileLoader {
public:
    using ResultType = outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>;
    /**
     * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
     * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
     * @param buffers - Contains path/data loaded
     * @param parse - Whether to parse file upon completion (for MNN)
     * @param save - Whether to save the file to local disk upon completion
     */
    using CompletionCallback = std::function<void(std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool parse, bool save)>;

    /// @brief virtual destructor to prevent memory leaks from derived classes
    virtual ~FileLoader() {}
    /// @brief Load a file into memory
    /// @param filename URL prefix based filename to load from, i.e. 'https://filename.html', 'ipfs://testme.mnn', etc.
    /// @return a shared void pointer to the in memory data that was loaded, auto deletes at termination
    virtual std::shared_ptr<void> LoadFile(std::string filename) = 0;
    /**
     * Asynchronously load a file based on type
     * @param url - URL to load, will determine loader we use
     * @param parse - Whether to parse file upon completion (for MNN)
     * @param save - Whether to save the file to local disk upon completion
     * @param ioc - ASIO context for async loading
     * @param callback - Filemanager callback on completion
     * @param status - Status function that will be updated with status codes as operation progresses
     * @return String indicating init
     */
    virtual std::shared_ptr<void> LoadASync(std::string filename, bool parse, bool save, std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback callback) = 0;
};
