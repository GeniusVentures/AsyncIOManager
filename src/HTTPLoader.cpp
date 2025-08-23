
#include "FileManager.hpp"
#include "HTTPLoader.hpp"
#include "HTTPCommon.hpp"
OUTCOME_CPP_DEFINE_CATEGORY_3(sgns, HTTPLoader::Error, e)
{
    switch (e)
    {
    case sgns::HTTPLoader::Error::INVALID_URL:
        return "Invalid URL";
    }
    return "Unknown error";
}

namespace sgns
{
    HTTPLoader* HTTPLoader::_instance = nullptr;
    void HTTPLoader::InitializeSingleton() {
        if (_instance == nullptr) {
            _instance = new HTTPLoader();
        }
    }
    HTTPLoader::HTTPLoader()
    {
        //FileManager::GetInstance().RegisterLoader("http", this);
        FileManager::GetInstance().RegisterLoader("https", this);
    }

    std::shared_ptr<void> HTTPLoader::LoadFile(std::string filename)
    {
        const char* dummyValue = "Inside the HTTPLoader::LoadFile Function";
        // for this test, we don't need to delete the shared_ptr as the data is static, so pass null lambda delete function
        return { (void*)dummyValue, [](void*) {} };
        /* TODO: scorpioluck20 - Need to implement this. How we load file base on format file?*/
    }


    std::shared_ptr<void> HTTPLoader::LoadASync(std::string filename, bool parse, bool save, std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback handle_read)
    {
        std::shared_ptr<string> result = std::make_shared<string>("test");
        //Parse hostname and path
        std::string http_host;
        std::string http_path;
        std::string http_port;
        if (!parseHTTPUrl(filename, http_host, http_path, http_port))
        {
            boost::asio::post(*ioc, [handle_read, ioc]() {
                handle_read(ioc, outcome::failure(Error::INVALID_URL), false, false);
                });
            return result;
        }

        auto httpDevice = std::make_shared<HTTPDevice>(http_host, http_path, http_port, parse, save);
        httpDevice->StartHTTPDownload(ioc, handle_read);
        
        return result;
    }

} // End namespace sgns
