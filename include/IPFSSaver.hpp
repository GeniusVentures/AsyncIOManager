// IPFSSAVER.hpp

#ifndef IPFSSAVER_HPP
#define IPFSSAVER_HPP

#include "FileSaver.hpp"
#include "ASIOSingleton.hpp"
#include "ipfs_lite/rocksdb/rocksdb.hpp"
#include "ipfs_lite/rocksdb/rocksdb_error.hpp"
#include "ipfs_lite/ipfs/impl/datastore_rocksdb.hpp"
#include "ipfs_lite/ipfs/impl/in_memory_datastore.hpp"

namespace sgns
{
    /// @brief class to handle "ipfs://" prefix in a filename to save to ipfs
    class IPFSSaver : public FileSaver {
        SINGLETON_PTR(IPFSSaver);
    public:
        /// @brief init singleton pointer for usage
        static void InitializeSingleton();
        /// @brief save a file to ipfs, throws on error
        /// @param filename filename to save the file as
        virtual void SaveFile(std::string filename, std::shared_ptr<void> data) override;
        virtual void SaveASync(std::shared_ptr<boost::asio::io_context> ioc, std::function<void(std::shared_ptr<boost::asio::io_context> ioc)> handle_write,
            std::string filename,
            ResultType data, std::string suffix) override;
    private:
        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger("IPFSSaver");

    };
}
#endif
