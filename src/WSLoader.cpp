#include <sstream>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>
#include <memory>
#include "FileManager.hpp"
#include "WSLoader.hpp"
#include "WSCommon.hpp"



namespace sgns
{
    WSLoader* WSLoader::_instance = nullptr;
    void WSLoader::InitializeSingleton() {
        if (_instance == nullptr) {
            _instance = new WSLoader();
        }
    }
    WSLoader::WSLoader()
    {
        FileManager::GetInstance().RegisterLoader("wss", this);
        //FileManager::GetInstance().RegisterLoader("ws", this);
    }

    std::shared_ptr<void> WSLoader::LoadFile(std::string filename)
    {
        const char* dummyValue = "Inside the WSLoader::LoadFile Function";
        // for this test, we don't need to delete the shared_ptr as the data is static, so pass null lambda delete function
        return { (void*)dummyValue, [](void*) {} };
        /* TODO: scorpioluck20 - Need to implement this. How we load file base on format file?*/
    }

    std::shared_ptr<void> WSLoader::LoadASync(std::string filename, bool parse, bool save, std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback handle_read)
    {
        //Parse hostname and path
        std::string ws_host;
        std::string ws_path;
        std::string ws_port;
        parseHTTPUrl(filename, ws_host, ws_path, ws_port);
        std::cout << "host " << ws_host << std::endl;
        std::cout << "path " << ws_path << std::endl;
        std::cout << "port " << ws_port << std::endl;

        auto httpDevice = std::make_shared<WSDevice>(ws_host, ws_path, ws_port, parse, save);
        httpDevice->StartWSDownload(ioc, handle_read);

        std::shared_ptr<string> result = std::make_shared < string>("test");
        return result;
    }

} // End namespace sgns
