#include <sstream>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>
#include "FileManager.hpp"
#include "MNNLoader.hpp"
#include "FILECommon.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns, MNNLoader::Error, e)
{
    switch (e)
    {
    case sgns::MNNLoader::Error::READ_ERROR:
        return "HTTP Could not resolve address";
    }
    return "Unknown error";
}
namespace sgns
{
    MNNLoader* MNNLoader::_instance = nullptr;
    void MNNLoader::InitializeSingleton() {
        if (_instance == nullptr) {
            _instance = new MNNLoader();
        }
    }
    MNNLoader::MNNLoader()
    {
        FileManager::GetInstance().RegisterLoader("file", this);
    }

    std::shared_ptr<void> MNNLoader::LoadFile(std::string filename)
    {
        if (!std::filesystem::exists(filename))
        {
            throw std::range_error("File was not exist in system");
        }
        std::ifstream inputFile(filename, std::ios_base::binary);
        if (!inputFile.is_open())
        {
            throw std::range_error("Can not open file");
        }
        // Read all file to string
        std::string dataContent((std::istreambuf_iterator<char>(inputFile)),
                std::istreambuf_iterator<char>());
        inputFile.close();
        std::shared_ptr<string> result = std::make_shared<string>(
                dataContent);
        return result;
    }

    std::shared_ptr<void> MNNLoader::LoadASync(std::string filename,bool parse,bool save,std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback handle_read)
    {
        std::shared_ptr<string> result = std::make_shared < string>("init");
        // Create a file device which will have a stream_descriptor or stream_file based on whether we are on posix OS or not.
        auto fileDevice = std::make_shared<FILEDevice>(ioc, filename, 0);
        auto buffer = std::make_shared<boost::asio::streambuf>();
        ////Async Read.
       boost::asio::async_read(fileDevice->getFile(), *buffer,
            boost::asio::transfer_all(),
            [this, fileDevice, ioc, handle_read, parse, save, buffer, filename](const boost::system::error_code& error, std::size_t bytes_transferred) {
                if (error.value() == 2)
                {
                    m_logger->info("LOCAL File Finished");
                    auto finaldata = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
                    std::filesystem::path p(filename);
                    finaldata->first.push_back(p.filename().string());
                    size_t dataSize = buffer->size();
                    finaldata->second.emplace_back(
                        boost::asio::buffers_begin(buffer->data()),
                        boost::asio::buffers_begin(buffer->data()) + dataSize
                    );
                    handle_read(ioc, finaldata, parse, save);
                }
                else {
                    m_logger->error("File read error: {}", error.message());
                    handle_read(ioc, outcome::failure(Error::READ_ERROR), false, false);
                }
            });
       
        return result;
    }

} // End namespace sgns
