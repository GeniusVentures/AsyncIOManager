//#define BOOST_ASIO_HAS_IO_URING 1 //Linux
//#define BOOST_ASIO_DISABLE_EPOLL 0 //Maybe linux?
#include <iostream>
#include <string>
#include <vector>
#include "ASIOSingleton.hpp"
#include "FileManager.hpp"
#include <asiomgr-logger.hpp>
//#include "MNNLoader.hpp"
//#include "MNNParser.hpp"
//#include "IPFSLoader.hpp"
//#include "HTTPLoader.hpp"
//#include "SFTPLoader.hpp"
//#include "WSLoader.hpp"
#include "URLStringUtil.h"
#include <libp2p/injector/host_injector.hpp>
#include "libp2p/injector/kademlia_injector.hpp"

/**
 * This program is example to loading MNN model file
 */
//#define IPFS_FILE_PATH_NAME "ipfs://example.mnn"
#define FILE_PATH_NAME "file://../test/1.mnn"
#define FILE_SAVE_NAME "file://test.mnn"
void loadhandler(boost::system::error_code ec, std::size_t n, std::vector<char>& buffer) {
    if (!ec) {
        std::cout << "Received data: ";
        std::cout << std::endl;
        std::cout << "Handler Out Size:" << n << std::endl;
    }
    else {
        std::cerr << "Error in async_read: " << ec.message() << std::endl;
    }
}


int main(int argc, char **argv)
{
    std::string file_name = "";
    std::vector<string> file_names(0);
    std::cout << "argcount" << argc << std::endl;
    if (argc < 2)
    {
        std::cout << argv[0] << " [MNN extension file]" << std::endl;
        std::cout << "E.g:" << std::endl;
        std::cout << "\t " << argv[0] << " file://../test/1.mnn" << std::endl;
        return 1;
    }
    else
    {
        file_name = std::string(argv[1]);
        for (int i = 1; i < argc; i++)
        {
            std::cout << "file: " << argv[i] << std::endl;
            file_names.push_back(argv[i]);
        }
    }
   // return 1;
    // this should all be moved to GTest
    // Break out the URL prefix, file path, and extension.
    std::string url_prefix;
    std::string file_path;
    std::string extension;
    std::string http_host;
    std::string http_path;

    getURLComponents(
            "https://www.example.com/test.jpg",
            url_prefix, file_path, extension);

    // test plugins
    if (file_name.empty())
    {
        file_name = FILE_PATH_NAME;
    }
    auto loggerHttpCommon = sgns::asiomgr::createLogger("HTTPCommon");
    auto loggerFileManager = sgns::asiomgr::createLogger("FileManager");
    auto loggerIpfsLoader = sgns::asiomgr::createLogger("IPFSLoader");
    auto loggerWSCommon = sgns::asiomgr::createLogger("WSCommon");
    auto loggerIPFSCommon = sgns::asiomgr::createLogger("IPFSCommon");
    auto loggerFILECommon = sgns::asiomgr::createLogger("FILECommon");
    auto loggerIPFSSaver = sgns::asiomgr::createLogger("IPFSSaver");
    auto loggerMNNLoader = sgns::asiomgr::createLogger("MNNLoader");
    loggerHttpCommon->set_level(spdlog::level::trace);
    loggerFileManager->set_level(spdlog::level::trace);
    loggerIpfsLoader->set_level(spdlog::level::trace);
    loggerWSCommon->set_level(spdlog::level::trace);
    loggerIPFSCommon->set_level(spdlog::level::trace);
    loggerFILECommon->set_level(spdlog::level::trace);
    loggerIPFSSaver->set_level(spdlog::level::trace);
    loggerMNNLoader->set_level(spdlog::level::trace);
    //auto ioc = std::make_shared<boost::asio::io_context>();
    
    //auto injector = libp2p::injector::makeHostInjector();
    libp2p::protocol::kademlia::Config kademlia_config;
    kademlia_config.randomWalk.enabled = true;
    kademlia_config.randomWalk.interval = std::chrono::seconds(300);
    kademlia_config.requestConcurency = 20;
    //auto injector = libp2p::injector::makeHostInjector();
    auto injector = libp2p::injector::makeHostInjector(
        // libp2p::injector::useKeyPair(kp), // Use predefined keypair
        libp2p::injector::makeKademliaInjector(
            libp2p::injector::useKademliaConfig(kademlia_config)));
    auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

    // Create a work guard to keep the io_context alive
    boost::asio::io_context::executor_type executor = ioc->get_executor();
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);

    FileManager::GetInstance().InitializeSingletons();
    for (int i = 0; i < file_names.size(); i++)
    {
        std::cout << "LoadASync: " << file_names[i] << std::endl;
        auto data = FileManager::GetInstance().LoadASync(file_names[i], false, false, ioc, 
                [](outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>> buffers)
                {
                    if (buffers)
                    {
                        std::cout << "Final Callback, it was a success" << std::endl;
                    }
                    else {
                        std::cout << "Final Callback, it was not a success: " << buffers.error().message() << std::endl;
                    }
                    
                },"file");
        //FileManager::GetInstance().IncrementOutstandingOperations();

        //std::cout << "LoadFile: " << file_names[i] << std::endl;
        //auto data = FileManager::GetInstance().LoadFile(file_names[i], false);

        //std::cout << "LoadFile with Parse: " << file_names[i] << std::endl;
        //data = FileManager::GetInstance().LoadFile(file_names[i], true);
        //std::cout << "ParseFile: " << std::endl;
        //FileManager::GetInstance().ParseData("mnn", data);
        //std::cout << "SaveFile: Save to " << FILE_SAVE_NAME;
        //FileManager::GetInstance().SaveFile(FILE_SAVE_NAME, data);
        //std::cout << std::endl;
        //boost::asio::post(ioc, postload);
    }
    //std::thread([ioc]() {
    //    ioc->run();
    //    }).detach();
    ioc->reset();
    ioc->run();

    return 0;
}

