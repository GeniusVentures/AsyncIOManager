/**
 * Source file for the HTTPCommon
 */
#include "HTTPCommon.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns, HTTPDevice::Error, e)
{
    switch (e)
    {
    case sgns::HTTPDevice::Error::COULD_NOT_RESOLVE:
        return "HTTP Could not resolve address";
    case sgns::HTTPDevice::Error::HANDSHAKE_ERROR:
        return "HTTP Handshake Error";
    case sgns::HTTPDevice::Error::CONNECT_ERROR:
        return "HTTP Connect Error";
    case sgns::HTTPDevice::Error::CON_INTERRUPT:
        return "HTTP Read Error. Connection interrupted.";   
    case sgns::HTTPDevice::Error::NO_HEADER:
        return "HTTP Data Read failed. No header.";
    case sgns::HTTPDevice::Error::REQ_FAILED:
        return "HTTP Data Read failed. Get Request Fail.";
    }
    return "Unknown error";
}

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

    void HTTPDevice::StartHTTPDownload(std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback handle_read)
    {
        //Get DNS result for hostname

        boost::asio::ip::tcp::resolver resolver(*ioc);
        boost::asio::ip::tcp::endpoint endpoint;
        try {
            m_logger->info("Resolving Address");
            boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(http_host_, "https");
            endpoint = *results.begin();
        }
        catch (const boost::system::system_error& e) {
            m_logger->error("Error resolving address: {}", e.what());
            boost::asio::post(*ioc, [handle_read, ioc]() {
                    handle_read(ioc, outcome::failure(Error::COULD_NOT_RESOLVE), false, false);
                });
            return;
        }
        catch (const std::exception& e) {
            m_logger->error("Error resolving address: {}", e.what());
            boost::asio::post(*ioc, [handle_read, ioc]() {
                handle_read(ioc, outcome::failure(Error::COULD_NOT_RESOLVE), false, false);
                });
            return;
        }
        catch (...) {
            m_logger->error("Error resolving address: Unknown");
            boost::asio::post(*ioc, [handle_read, ioc]() {
                handle_read(ioc, outcome::failure(Error::COULD_NOT_RESOLVE), false, false);
                });
            return;
        }


        //Create SSL Context
        auto ssl_context = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls);

        //Disclude certain older insecure options
        ssl_context->set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3);

        //Create Socket with SSL Context
        auto socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(*ioc, *ssl_context);
        if (!SSL_set_tlsext_host_name(socket->native_handle(), http_host_.c_str())) {
            unsigned long err = ERR_get_error();
            throw std::runtime_error("Failed to set SNI: " + std::string(ERR_reason_error_string(err)));
        }
        
        //Connect socket
        socket->lowest_layer().async_connect(endpoint, [self = shared_from_this(), ioc, socket, handle_read](const boost::system::error_code& connect_error)
            {
                if (!connect_error)
                {
                    socket->async_handshake(boost::asio::ssl::stream_base::client, [self , ioc, socket, handle_read](const boost::system::error_code& handshake_error) {
                        if (!handshake_error) {
                            // Start the asynchronous download for a specific path
                            self->StartHTTPGet(ioc, socket, handle_read);
                        }
                        else {
                            self->m_logger->error("Handshake error: {}", handshake_error.message());
                            handle_read(ioc, outcome::failure(Error::HANDSHAKE_ERROR), false, false);
                        }
                        });
                }
                else {
                    self->m_logger->error("Connection error: {}", connect_error.message());
                    handle_read(ioc, outcome::failure(Error::CONNECT_ERROR), false, false);
                }
            });
    }

    void HTTPDevice::StartHTTPGet(std::shared_ptr<boost::asio::io_context> ioc,
        std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket,
        CompletionCallback handle_read)
    {
        //Create HTTP Get request and write to server
        std::string get_request = "GET " + http_path_ + " HTTP/1.1\r\nHost: " + http_host_ + "\r\nConnection: close\r\n\r\n";
        boost::asio::async_write(*socket, boost::asio::buffer(get_request), [self = shared_from_this(), ioc, handle_read, socket](const boost::system::error_code& write_error, std::size_t) {
            if (!write_error) {
                //Create a buffer for returned data and read from server
                auto headerbuff = std::make_shared<boost::asio::streambuf>();
                boost::asio::async_read(*socket, *headerbuff, boost::asio::transfer_all(), [self, ioc, handle_read, headerbuff, socket](const boost::system::error_code& read_error, std::size_t bytes_transferred) {
                    // Check if read completed normally with EOF (boost::asio::error::eof)
                    if (read_error && read_error != boost::asio::error::eof) {
                        // Connection was interrupted before completion
                        self->m_logger->error("Error, connection interrupted");
                        handle_read(ioc, outcome::failure(Error::CON_INTERRUPT), false, false);
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
                        //Send this to handler to be processed.
                        auto finaldata = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
                        std::filesystem::path p(self->http_path_);
                        finaldata->first.push_back(p.filename().string());
                        finaldata->second.emplace_back(
                            buffer->begin() + headerEnd + 4,  
                            buffer->end()                     
                        );
                        handle_read(ioc, finaldata, self->parse_, self->save_);
                    }
                    else {
                        self->m_logger->error("Error, no header in http");
                        handle_read(ioc, outcome::failure(Error::NO_HEADER), false, false);
                    }
                    });
            }
            else {
                self->m_logger->error("Error in async_write: {}", write_error.message());
                handle_read(ioc, outcome::failure(Error::REQ_FAILED), false, false);
            }
            });
    }
}