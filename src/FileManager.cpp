#include "FileManager.hpp"
#include "URLStringUtil.h"
//#include "MNNLoader.hpp"
//#include "MNNParser.hpp"
//#include "MNNSaver.hpp"
//#include "IPFSLoader.hpp"
//#include "IPFSSaver.hpp"
#include "HTTPLoader.hpp"
//#include "SFTPLoader.hpp"
//#include "WSLoader.hpp"

void FileManager::RegisterLoader(const std::string &prefix,
        FileLoader *handlerLoader)
{   
    loaders[prefix] = handlerLoader;
}

void FileManager::RegisterParser(const std::string &suffix,
        FileParser *handlerParser)
{
    parsers[suffix] = handlerParser;
}

void FileManager::RegisterSaver(const std::string &prefix,
        FileSaver *handlerSaver)
{
    savers[prefix] = handlerSaver;
}

void AsyncHandler(boost::system::error_code ec, std::size_t n, std::vector<char>& buffer) {


}

void FileManager::InitializeSingletons() {
    //sgns::MNNLoader::InitializeSingleton();
    //sgns::MNNParser::InitializeSingleton();
    //sgns::SFTPLoader::InitializeSingleton();
    sgns::HTTPLoader::InitializeSingleton();
    //sgns::WSLoader::InitializeSingleton();
    //sgns::IPFSLoader::InitializeSingleton();
    //sgns::IPFSSaver::InitializeSingleton();
    //sgns::MNNSaver::InitializeSingleton();
}
shared_ptr<void> FileManager::LoadASync(const std::string& url, bool parse, bool save, std::shared_ptr<boost::asio::io_context> ioc, FinalCallback finalcall, std::string savetype)
{
    std::string prefix;
    std::string filePath;
    std::string suffix;
	
    getURLComponents(url, prefix, filePath, suffix);
#if 0
    std::cout << "DEBUG: URL: " << url << " -prefix: " << prefix << " -filePath: " << filePath << " -suffix: " << suffix << std::endl;
#endif
    auto loaderIter = loaders.find(prefix);
    if (loaderIter == loaders.end())
    {
        throw std::range_error("No loader registered for prefix " + prefix);
    }
    //Increment Operations
    IncrementOutstandingOperations();
    //Create a handler
    auto handle_read = [this, savetype, suffix, finalcall](std::shared_ptr<boost::asio::io_context> ioc, ResultType buffers, bool parse, bool save) {
        std::cout << "Callback!" << std::endl;
        //Parse Data
        if (parse)
        {
            auto parserIter = parsers.find("mnn");
            auto parser = dynamic_cast<FileParser*>(parserIter->second);
            //shared_ptr<void> data = parser->ParseASync(buffer);
        }
        //Save data or otherwise decrement counter of operations
        if (save)
        {
            auto handle_write = [this](std::shared_ptr<boost::asio::io_context> ioc) {
                DecrementOutstandingOperations(ioc);
            };
            auto saverIter = savers.find(savetype);
            auto saver = saverIter->second;
            saver->SaveASync(ioc,handle_write,"",buffers, suffix);
        }
        else {
            // Handle completion
            DecrementOutstandingOperations(ioc);
        }
        finalcall(buffers);

    };
    auto loader = loaderIter->second;
    // double check pointer is to a FileLoader class
    assert(dynamic_cast<FileLoader*>(loader));
    shared_ptr<void> data = loader->LoadASync(filePath,parse,save,ioc,handle_read);
    return data;
}

shared_ptr<void> FileManager::LoadFile(const std::string &url, bool parse)
{
    std::string prefix;
    std::string filePath;
    std::string suffix;

    getURLComponents(url, prefix, filePath, suffix);
#if 0
    std::cout << "DEBUG: URL: " << url << " -prefix: " << prefix << " -filePath: " << filePath << " -suffix: " << suffix << std::endl;
#endif
    auto loaderIter = loaders.find(prefix);
    if (loaderIter == loaders.end())
    {
        throw std::range_error("No loader registered for prefix " + prefix);
    }
    auto loader = loaderIter->second;
    // double check pointer is to a FileLoader class
    assert(dynamic_cast<FileLoader*>(loader));

    shared_ptr<void> data = loader->LoadFile(filePath);
    if (parse)
    {
        data = ParseData(suffix, data);
    }

    return data;
}

shared_ptr<void> FileManager::ParseData(const std::string &suffix,
        shared_ptr<void> data)
{
    auto parserIter = parsers.find(suffix);
    if (parserIter == parsers.end())
    {
        throw std::range_error("No parser registered for suffix " + suffix);
    }

    auto parser = dynamic_cast<FileParser*>(parserIter->second);
    data = parser->ParseData(data);
    return data;
}

void FileManager::SaveFile(const std::string &url, std::shared_ptr<void> data)
{
    std::string prefix;
    std::string filePath;
    std::string suffix;

    getURLComponents(url, prefix, filePath, suffix);

    auto saverIter = savers.find(prefix);

    if (saverIter == savers.end())
    {
        throw std::range_error("No saver registered for prefix " + prefix);
    }

    auto saver = saverIter->second;
    // double check pointer is to a FileSaver class
    assert(dynamic_cast<FileSaver*>(saver));

    saver->SaveFile(filePath, data);
}

/// @brief Function to decrement operation count
void FileManager::DecrementOutstandingOperations(std::shared_ptr<boost::asio::io_context> ioc)
{
    // Decrement the counter
    outstandingOperations_--;

    // If all operations are complete, perform additional cleanup
    if (outstandingOperations_ == 0) {
        // Clean up io_context
        ioc->stop();
    }
}

/// @brief Function to increment operation count
void FileManager::IncrementOutstandingOperations() 
{
    // Increment the counter
    outstandingOperations_++;
}

std::shared_ptr<int> FileManager::GetOutstandingOperationsPointer() 
{
    // Return a shared pointer to the outstandingOperations counter
    return std::make_shared<int>(outstandingOperations_);
}