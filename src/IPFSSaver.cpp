// IPFSLoader.cpp

#include <iostream>
#include "FileManager.hpp"
#include "IPFSSaver.hpp"
#include "libp2p/multi/content_identifier_codec.hpp"
#include "common/hexutil.hpp"

namespace sgns
{
    IPFSSaver* IPFSSaver::_instance = nullptr;
    void IPFSSaver::InitializeSingleton() {
        if (_instance == nullptr) {
            _instance = new IPFSSaver();
        }
    }

    IPFSSaver::IPFSSaver() {
        FileManager::GetInstance().RegisterSaver("ipfs", this);
    }

    void IPFSSaver::SaveFile(std::string filename, std::shared_ptr<void> data) {
        m_logger->info("Inside the IPFSSaver::SaveFile Function");
    }
    inline std::vector<uint8_t> operator""_unhex(const char* c, size_t s) {
        return sgns::common::unhex(std::string_view(c, s)).value();
    }
    void IPFSSaver::SaveASync(std::shared_ptr<boost::asio::io_context> ioc, std::function<void(std::shared_ptr<boost::asio::io_context> ioc)> handle_write, std::string filename, ResultType data, std::string suffix) {
        m_logger->info("Inside the IPFSSaver::SaveASync Function");
        if (data.value()->first.data() == nullptr)
        {
            throw range_error("Can not save with null data");
        }
        sgns::ipfs_lite::rocksdb::Options options;
        //sgns::ipfs_lite::rocksdb::ReadOptions readoptions;
        //sgns::ipfs_lite::rocksdb::WriteOptions writeoptions;
        //readoptions.async_io = true;
        //options.write_thread_max_yield_usec = true;
        options.create_if_missing = true;
        auto r = sgns::ipfs_lite::rocksdb::create("ipfsdb", options);
        //r.value()->setReadOptions(readoptions);
        
        auto datastore = sgns::ipfs_lite::ipfs::RocksdbDatastore(r.value());
        for (size_t i = 0; i < data.value()->first.size(); ++i) {
            auto cid = sgns::common::getCidOf(std::vector<uint8_t>(data.value()->second[i].begin(), data.value()->second[i].end()));
            common::Buffer buffer(std::vector<uint8_t>(data.value()->second[i].begin(), data.value()->second[i].end()));
            datastore.set(cid.value(), buffer);

        }
        handle_write(ioc);
    }
}

