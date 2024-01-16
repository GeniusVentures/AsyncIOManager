/**
 * Header file for the WSLoader
 */

#ifndef INCLUDE_HTTPLOADER_HPP_
#define INCLUDE_HTTPLOADER_HPP_

#include <memory>
#include <string>
#include "FileLoader.hpp"
//#include "MNNCommon.hpp"
#include "Singleton.hpp"

namespace sgns
{

    /**
     * This class is for loading files from HTTP
     */
    class HTTPLoader : public FileLoader
    {
        SINGLETON_PTR(HTTPLoader);
    public:
        /**
         * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
         * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
         * @param buffer - Contains data loaded
         * @param parse - Whether to parse file upon completion (for MNN)
         * @param save - Whether to save the file to local disk upon completion
         */
        using CompletionCallback = std::function<void(std::shared_ptr<boost::asio::io_context> ioc, std::shared_ptr<std::vector<char>> buffer, bool parse, bool save)>;
        /**
         * Status callback returns an error code as an async load proceeds
         * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
         * @param buffer - Contains data loaded
         * @param parse - Whether to parse file upon completion (for MNN)
         * @param save - Whether to save the file to local disk upon completion
         */
        using StatusCallback = std::function<void(const int&)>;
        /**ok
         * Load Data on the MNN file
         * @param filename - MNN file part
         * @return Interpreter of MNN file
         */
        std::shared_ptr<void> LoadFile(std::string filename) override;
        /**
         * Asynchronously load a file
         * @param filename - Filename to load
         * @param parse - Whether to parse file upon completion (for MNN)
         * @param save - Whether to save the file to local disk upon completion
         * @param ioc - ASIO context for async loading
         * @param callback - Filemanager callback on completion
         * @param status - Status function that will be updated with status codes as operation progresses
         * @return String indicating init
         */
        std::shared_ptr<void> LoadASync(std::string filename, bool parse, bool save, std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback callback, StatusCallback status) override;
    protected:

    };

} // End namespace sgns

#endif /* INCLUDE_HTTPLOADER_HPP_ */
