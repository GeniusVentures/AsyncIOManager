/**
 * Header file for the IPFSLoader
 */

#ifndef INCLUDE_IPFSLOADER_HPP_
#define INCLUDE_IPFSLOADER_HPP_

#include <memory>
#include <string>
#include "IPFSCommon.hpp"
#include "FileLoader.hpp"
#include "ASIOSingleton.hpp"
#include "ipfs_lite/ipfs/impl/ipfs_block_service.hpp"
#include "ipfs_lite/ipfs/impl/in_memory_datastore.hpp"


namespace sgns
{

    /**
     * This class is for loading files from IPFS
     */
    class IPFSLoader : public FileLoader
    {
        SINGLETON_PTR(IPFSLoader);
    public:
        enum class Error
        {
            CANNOT_LISTEN = 1,
            BAD_CID = 2,
        };
        static void InitializeSingleton();

        /**
         * Completion callback template. We expect an io_context so the thread can be shut down if no outstanding async loads exist, and a buffer with the read information
         * @param ioc - asio io context so we can stop this if no outstanding async tasks remain
         * @param buffers - Contains path/data loaded
         * @param parse - Whether to parse file upon completion (for MNN)
         * @param save - Whether to save the file to local disk upon completion
         */
        using CompletionCallback = std::function<void(std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool parse, bool save)>;

        /**ok
         * Load Data on the MNN file
         * @param filename - MNN file part
         * @return Interpreter of MNN file
         *
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
        std::shared_ptr<void> LoadASync(std::string filename, bool parse, bool save, std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback callback) override;
    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger("IPFSLoader");
    protected:

    };

} // End namespace sgns

#endif /* INCLUDE_IPFSLOADER_HPP_ */
