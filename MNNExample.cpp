//#define BOOST_ASIO_HAS_IO_URING 1 //Linux
//#define BOOST_ASIO_DISABLE_EPOLL 0 //Maybe linux?
#include <iostream>
#include <string>
#include "MNNLoader.hpp"
#include "FileManager.hpp"
#include "URLStringUtil.h"
#include <vector>

/**
 * This program is example to loading MNN model file
 */
#define IPFS_FILE_PATH_NAME "ipfs://example.mnn"
#define FILE_PATH_NAME "file://../test/1.mnn"
#define FILE_SAVE_NAME "file://test.mnn"

void loadhandler( boost::system::error_code ec, std::size_t n, std::vector<char> &buffer )
{
    if ( !ec )
    {
        std::cout << "Received data: ";
        std::cout << std::endl;
        std::cout << "Handler Out Size:" << n << std::endl;
    }
    else
    {
        std::cerr << "Error in async_read: " << ec.message() << std::endl;
    }
}

int main( int argc, char **argv )
{
    std::string         file_name = "";
    std::vector<string> file_names( 0 );
    std::cout << "argcount" << argc << std::endl;
    if ( argc < 2 )
    {
        std::cout << argv[0] << " [MNN extension file]" << std::endl;
        std::cout << "E.g:" << std::endl;
        std::cout << "\t " << argv[0] << " file://../test/1.mnn" << std::endl;
        return 1;
    }
    else
    {
        file_name = std::string( argv[1] );
        for ( int i = 1; i < argc; i++ )
        {
            std::cout << "file: " << argv[i] << std::endl;
            file_names.push_back( argv[i] );
        }
    }
    // return 1;
    // this should all be moved to GTest
    // Break out the URL prefix, file path, and extension.
    std::string url_prefix;
    std::string file_path;
    std::string extension;

    // getURLComponents(
    //        "https://www.example.com/what is this and who cares about the extension anyway?dum='.jpg'",
    //        url_prefix, file_path, extension);
    // Print the results.
    // cout << "urlPrefix: " << url_prefix << endl;
    // cout << "filePath: " << file_path << endl;
    //cout << "extension: " << extension << endl;

    // getURLComponents("file://./test.mnn", url_prefix, file_path, extension);
    // Print the results.
    // cout << "urlPrefix: " << url_prefix << endl;
    //cout << "filePath: " << file_path << endl;
    //cout << "extension: " << extension << endl;

    // getURLComponents("ipfs://really/long/path/to/a/file/testme.jpg",
    //       url_prefix, file_path, extension);
    // Print the results.
    //cout << "urlPrefix: " << url_prefix << endl;
    //cout << "filePath: " << file_path << endl;
    //cout << "extension: " << extension << endl;

    // test plugins
    if ( file_name.empty() )
    {
        file_name = FILE_PATH_NAME;
    }

    for ( int i = 0; i < file_names.size(); i++ )
    {
        //boost::asio::io_context ioc;
        //auto work = make_work_guard(ioc);
        //boost::asio::stream_file file(ioc, file_names[i], boost::asio::stream_file::flags::read_only);
        //std::vector<char> buffer(file.size());
        //boost::asio::async_read(file,
        //    boost::asio::buffer(buffer),
        //    boost::asio::transfer_exactly(buffer.size()),
        //    [&](const boost::system::error_code& error, std::size_t bytes_transferred) {
        //        loadhandler(error, bytes_transferred, buffer);
        //    });
        //work.reset();
        //ioc.run();

        //std::cout << "LoadFile: " << file_names[i] << std::endl;
        //auto data = FileManager::GetInstance().LoadASync(file_names[i],true);
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

    return 0;
}
