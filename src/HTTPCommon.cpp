/**
 * Source file for the HTTPCommon
 */
#include "HTTPCommon.hpp"

namespace sgns
{
    using namespace boost::asio;
    HTTPDevice::HTTPDevice(
        std::string http_host,
        std::string http_path,
        std::string http_port,
        bool parse, bool save) 
    {
        http_host_ = http_host;
        http_path_ = http_path;
        http_port_ = http_port;
        parse_ = parse;
        save_ = save;
    }

    void HTTPDevice::StartHTTPDownload(std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback handle_read, StatusCallback status)
    {
        //Get DNS result for hostname

        boost::asio::ip::tcp::resolver resolver(*ioc);
        boost::asio::ip::tcp::endpoint endpoint;
        try {
            std::cout << "resolving address" << std::endl;
            boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(http_host_, "https");
            endpoint = *results.begin();
        }
        catch (const boost::system::system_error& e) {
            std::cerr << "Error resolving address: " << e.what() << std::endl;
            status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Could not resolve address")));
        }
        catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
            status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Could not resolve address")));
        }
        catch (...) {
            std::cerr << "Unknown error occurred during address resolution." << std::endl;
            status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Could not resolve address")));
        }
        //boost::asio::ip::tcp::endpoint endpoint = *results.begin();

        //Create SSL Context
        auto ssl_context = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls);

        //Disclude certain older insecure options
        ssl_context->set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3);

        

        //Consider setting verify callback to check whether domain name matches cert
        // ssl_context->set_verify_callback(...);
        //Create Socket with SSL Context
        auto socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(*ioc, *ssl_context);
        if (!SSL_set_tlsext_host_name(socket->native_handle(), http_host_.c_str())) {
            unsigned long err = ERR_get_error();
            throw std::runtime_error("Failed to set SNI: " + std::string(ERR_reason_error_string(err)));
            status(CustomResult(sgns::AsyncError::outcome::failure("Could not set SNI")));
        }
        
        //Connect socket
        status(CustomResult(sgns::AsyncError::outcome::success(Success{ "Starting HTTP Connection" })));
        socket->lowest_layer().async_connect(endpoint, [self = shared_from_this(), ioc, socket, handle_read, status](const boost::system::error_code& connect_error)
            {
                if (!connect_error)
                {
                    status(CustomResult(sgns::AsyncError::outcome::success(Success{ "SSL Handshake Started" })));
                    socket->async_handshake(boost::asio::ssl::stream_base::client, [self , ioc, socket, handle_read, status](const boost::system::error_code& handshake_error) {
                        if (!handshake_error) {
                            // Start the asynchronous download for a specific path
                            self->StartHTTPGet(ioc, socket, handle_read, status);
                        }
                        else {
                            std::cerr << "Handshake error: " << handshake_error.message() << std::endl;
                            status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Handshake Error")));
                            handle_read(ioc, std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(), false, false);
                        }
                        });
                }
                else {
                    std::cerr << "Connection error: " << connect_error.message() << std::endl;
                    status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Connection Error")));
                    handle_read(ioc, std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(), false, false);
                }
            });
    }

    void HTTPDevice::StartHTTPGet(std::shared_ptr<boost::asio::io_context> ioc,
        std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket,
        CompletionCallback handle_read,
        StatusCallback status)
    {
        //Create HTTP Get request and write to server
        status(CustomResult(sgns::AsyncError::outcome::success(Success{ "Starting HTTP Get Request" })));
        std::string get_request = "GET " + http_path_ + " HTTP/1.1\r\nHost: " + http_host_ + "\r\nConnection: close\r\n\r\n";
        boost::asio::async_write(*socket, boost::asio::buffer(get_request), [self = shared_from_this(), ioc, handle_read, status, socket](const boost::system::error_code& write_error, std::size_t) {
            if (!write_error) {
                //Create a buffer for returned data and read from server
                auto headerbuff = std::make_shared<boost::asio::streambuf>();
                status(CustomResult(sgns::AsyncError::outcome::success(Success{ "Starting HTTP File Read" })));
                boost::asio::async_read(*socket, *headerbuff, boost::asio::transfer_all(), [self, ioc, handle_read, status, headerbuff, socket](const boost::system::error_code& read_error, std::size_t bytes_transferred) {
                    // Check if read completed normally with EOF (boost::asio::error::eof)
                    if (read_error && read_error != boost::asio::error::eof) {
                        // Connection was interrupted before completion
                        status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Data Read failed. Connection interrupted.")));
                        handle_read(ioc, std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(), false, false);
                        return;
                    }
                    //Make a vector buffer from data
                    auto buffer = std::make_shared<std::vector<char>>(boost::asio::buffers_begin(headerbuff->data()), boost::asio::buffers_end(headerbuff->data()));

                    //Copy to a string
                    std::string bufferStr(buffer->begin(), buffer->end());

                    //Find end of header 
                    size_t headerEnd = bufferStr.find("\r\n\r\n");

                    //Check if we found an end
                    if (headerEnd != std::string::npos) {
                        //Create vector of binary data by cutting off the header.
                        //auto binaryData = std::make_shared<std::vector<char>>(buffer->begin() + headerEnd + 4, buffer->end());

                        //Send this to handler to be processed.
                        //std::cout << "HTTPS Finish" << std::endl;
                        status(CustomResult(sgns::AsyncError::outcome::success(Success{ "HTTP Get finished" })));
                        auto finaldata = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
                        std::filesystem::path p(self->http_path_);
                        finaldata->first.push_back(p.filename().string());
                        //finaldata->second.push_back(*binaryData);
                        finaldata->second.emplace_back(
                            buffer->begin() + headerEnd + 4,  
                            buffer->end()                     
                        );
                        handle_read(ioc, finaldata, self->parse_, self->save_);
                    }
                    else {
                        status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Data Read failed. No header.")));
                        handle_read(ioc, std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(), false, false);
                    }
                    });
            }
            else {
                std::cerr << "Error in async_write: " << write_error.message() << std::endl;
                status(CustomResult(sgns::AsyncError::outcome::failure("HTTP Data Read failed. Get Request Fail.")));
                handle_read(ioc, std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(), false, false);
            }
            });
    }
}