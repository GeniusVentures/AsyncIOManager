/**
 * Source file for the HTTPCommon
 */
#include "HTTPCommon.hpp"

#include <atomic>
#include <chrono>

namespace
{
    using SslSocket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

    struct Deadline
    {
        std::shared_ptr<boost::asio::steady_timer> timer;
        std::shared_ptr<std::atomic_bool>          expired;
    };

    Deadline ArmDeadline( const std::shared_ptr<boost::asio::io_context> &ioc,
                          const std::shared_ptr<SslSocket>               &socket,
                          std::chrono::seconds                            timeout )
    {
        Deadline deadline{ std::make_shared<boost::asio::steady_timer>( *ioc ),
                           std::make_shared<std::atomic_bool>( false ) };
        deadline.timer->expires_after( timeout );
        deadline.timer->async_wait(
            [timer = deadline.timer, socket, expired = deadline.expired]( const boost::system::error_code &error )
            {
                if ( !error )
                {
                    expired->store( true );
                    boost::system::error_code ignored;
                    socket->lowest_layer().cancel( ignored );
                }
            } );
        return deadline;
    }

    void CancelDeadline( const Deadline &deadline )
    {
        boost::system::error_code ignored;
        deadline.timer->cancel( ignored );
    }
}

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, HTTPDevice::Error, e )
{
    switch ( e )
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
        case sgns::HTTPDevice::Error::TIMEOUT:
            return "HTTP operation timed out";
    }
    return "Unknown error";
}

namespace sgns
{
    using namespace boost::asio;

    bool HTTPDevice::s_verify_peer = true;

    HTTPDevice::HTTPDevice( std::string http_host, std::string http_path, std::string http_port, bool parse, bool save )
    {
        http_host_ = http_host;
        http_path_ = http_path;
        http_port_ = http_port;
        parse_     = parse;
        save_      = save;
    }

    void HTTPDevice::StartHTTPDownload( std::shared_ptr<boost::asio::io_context> ioc, CompletionCallback handle_read )
    {
        //Get DNS result for hostname

        boost::asio::ip::tcp::resolver                                resolver( *ioc );
        std::shared_ptr<boost::asio::ip::tcp::resolver::results_type> endpoints;
        try
        {
            m_logger->info( "Resolving Address" );
            endpoints = std::make_shared<boost::asio::ip::tcp::resolver::results_type>(
                resolver.resolve( http_host_, http_port_ ) );
        }
        catch ( const boost::system::system_error &e )
        {
            m_logger->error( "Error resolving address: {}", e.what() );
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::COULD_NOT_RESOLVE ), false, false ); } );
            return;
        }
        catch ( const std::exception &e )
        {
            m_logger->error( "Error resolving address: {}", e.what() );
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::COULD_NOT_RESOLVE ), false, false ); } );
            return;
        }
        catch ( ... )
        {
            m_logger->error( "Error resolving address: Unknown" );
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::COULD_NOT_RESOLVE ), false, false ); } );
            return;
        }

        //Create SSL Context
        auto ssl_context = std::make_shared<boost::asio::ssl::context>( boost::asio::ssl::context::tls );

        //Disclude certain older insecure options
        ssl_context->set_options( boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
                                  boost::asio::ssl::context::no_sslv3 );

        // ponytail: global toggle so tests with self-signed certs can disable verification
        if ( !s_verify_peer )
        {
            ssl_context->set_verify_mode( boost::asio::ssl::verify_none );
        }

        //Create Socket with SSL Context
        auto socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>( *ioc, *ssl_context );
        if ( !SSL_set_tlsext_host_name( socket->native_handle(), http_host_.c_str() ) )
        {
            unsigned long err = ERR_get_error();
            throw std::runtime_error( "Failed to set SNI: " + std::string( ERR_reason_error_string( err ) ) );
        }

        //Connect socket
        auto connect_deadline = ArmDeadline( ioc, socket, std::chrono::seconds( 10 ) );
        boost::asio::async_connect(
            socket->lowest_layer(),
            *endpoints,
            [self = shared_from_this(), ioc, ssl_context, socket, endpoints, handle_read, connect_deadline](
                const boost::system::error_code &connect_error,
                const boost::asio::ip::tcp::endpoint & )
            {
                CancelDeadline( connect_deadline );
                if ( !connect_error )
                {
                    auto handshake_deadline = ArmDeadline( ioc, socket, std::chrono::seconds( 10 ) );
                    socket->async_handshake(
                        boost::asio::ssl::stream_base::client,
                        [self, ioc, ssl_context, socket, handle_read, handshake_deadline](
                            const boost::system::error_code &handshake_error )
                        {
                            CancelDeadline( handshake_deadline );
                            if ( !handshake_error )
                            {
                                // Start the asynchronous download for a specific path
                                self->StartHTTPGet( ioc, ssl_context, socket, handle_read );
                            }
                            else
                            {
                                self->m_logger->error( "Handshake error: {}", handshake_error.message() );
                                handle_read(
                                    ioc,
                                    outcome::failure( handshake_deadline.expired->load() ? Error::TIMEOUT
                                                                                         : Error::HANDSHAKE_ERROR ),
                                    false,
                                    false );
                            }
                        } );
                }
                else
                {
                    self->m_logger->error( "Connection error: {}", connect_error.message() );
                    handle_read(
                        ioc,
                        outcome::failure( connect_deadline.expired->load() ? Error::TIMEOUT : Error::CONNECT_ERROR ),
                        false,
                        false );
                }
            } );
    }

    void HTTPDevice::StartHTTPGet( std::shared_ptr<boost::asio::io_context>                                ioc,
                                   std::shared_ptr<boost::asio::ssl::context>                              ssl_context,
                                   std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket,
                                   CompletionCallback                                                      handle_read )
    {
        //Create HTTP Get request and write to server
        std::string get_request   = "GET " + http_path_ + " HTTP/1.1\r\nHost: " + http_host_ +
                                    "\r\nUser-Agent: GeniusAI/1.0 (SGNS AsyncIO Manager)\r\nConnection: close\r\n\r\n";
        auto        read_deadline = ArmDeadline( ioc, socket, std::chrono::seconds( 30 ) );
        boost::asio::async_write(
            *socket,
            boost::asio::buffer( get_request ),
            [self = shared_from_this(), ioc, ssl_context, handle_read, socket, read_deadline](
                const boost::system::error_code &write_error,
                std::size_t )
            {
                if ( !write_error )
                {
                    //Create a buffer for returned data and read from server
                    auto headerbuff = std::make_shared<boost::asio::streambuf>();
                    boost::asio::async_read(
                        *socket,
                        *headerbuff,
                        boost::asio::transfer_all(),
                        [self, ioc, ssl_context, handle_read, headerbuff, socket, read_deadline](
                            const boost::system::error_code &read_error,
                            std::size_t                      bytes_transferred )
                        {
                            CancelDeadline( read_deadline );
                            // Check if read completed normally with EOF (boost::asio::error::eof)
                            if ( read_error && read_error != boost::asio::error::eof )
                            {
                                // Connection was interrupted before completion
                                self->m_logger->error( "Error, connection interrupted" );
                                handle_read( ioc,
                                             outcome::failure( read_deadline.expired->load() ? Error::TIMEOUT
                                                                                             : Error::CON_INTERRUPT ),
                                             false,
                                             false );
                                return;
                            }
                            //Make a vector buffer from data
                            auto buffer = std::make_shared<std::vector<char>>(
                                boost::asio::buffers_begin( headerbuff->data() ),
                                boost::asio::buffers_end( headerbuff->data() ) );

                            //Copy to a string
                            std::string bufferStr( buffer->begin(), buffer->end() );

                            //Find end of header
                            size_t headerEnd = bufferStr.find( "\r\n\r\n" );

                            //Check if we found an end
                            if ( headerEnd != std::string::npos )
                            {
                                //Send this to handler to be processed.
                                auto finaldata = std::make_shared<
                                    std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
                                std::filesystem::path p( self->http_path_ );
                                finaldata->first.push_back( p.filename().string() );
                                finaldata->second.emplace_back( buffer->begin() + headerEnd + 4, buffer->end() );
                                handle_read( ioc, finaldata, self->parse_, self->save_ );
                            }
                            else
                            {
                                self->m_logger->error( "Error, no header in http" );
                                handle_read( ioc, outcome::failure( Error::NO_HEADER ), false, false );
                            }
                        } );
                }
                else
                {
                    CancelDeadline( read_deadline );
                    self->m_logger->error( "Error in async_write: {}", write_error.message() );
                    handle_read( ioc,
                                 outcome::failure( read_deadline.expired->load() ? Error::TIMEOUT : Error::REQ_FAILED ),
                                 false,
                                 false );
                }
            } );
    }
}
