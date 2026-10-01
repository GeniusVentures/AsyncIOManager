/**
 * Source file for the HTTPClient -- status-aware sibling of HTTPDevice (D-01).
 */
#include "HTTPClient.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <system_error>
#include <variant>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::http, ClientError, e )
{
    switch ( e )
    {
        case sgns::http::ClientError::RESOLVE_FAILED:
            return "HTTP Could not resolve address";
        case sgns::http::ClientError::CONNECT_FAILED:
            return "HTTP Connect Error";
        case sgns::http::ClientError::TLS_HANDSHAKE_FAILED:
            return "HTTP Handshake Error";
        case sgns::http::ClientError::TLS_CA_LOAD_FAILED:
            return "HTTP TLS CA certificate load failed";
        case sgns::http::ClientError::WRITE_FAILED:
            return "HTTP Data Read failed. Get Request Fail.";
        case sgns::http::ClientError::READ_INTERRUPTED:
            return "HTTP Read Error. Connection interrupted.";
        case sgns::http::ClientError::NO_HEADER:
            return "HTTP Data Read failed. No header.";
        case sgns::http::ClientError::TIMEOUT:
            return "HTTP operation timed out";
    }
    return "Unknown error";
}

namespace
{
    using PlainSocket = boost::asio::ip::tcp::socket;
    using SslSocket   = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    using Stream      = std::variant<PlainSocket, SslSocket>;

    /// @brief Cancel whichever stream alternative is active (deadline fire).
    void CancelStream( Stream &stream )
    {
        std::visit(
            []( auto &s )
            {
                boost::system::error_code ignored;
                s.lowest_layer().cancel( ignored );
            },
            stream );
    }

    struct Deadline
    {
        std::shared_ptr<boost::asio::steady_timer> timer;
        std::shared_ptr<std::atomic_bool>          expired;
    };

    /// @brief Arm a millisecond deadline over the variant stream (P-1 widened:
    /// the timer lambda captures the stream shared_ptr so the SSL stream stays
    /// alive through a timeout fire -- R1).
    Deadline ArmDeadline( const std::shared_ptr<boost::asio::io_context> &ioc,
                          const std::shared_ptr<Stream>                   &stream,
                          std::chrono::milliseconds                        timeout )
    {
        Deadline deadline{ std::make_shared<boost::asio::steady_timer>( *ioc ),
                           std::make_shared<std::atomic_bool>( false ) };
        deadline.timer->expires_after( timeout );
        deadline.timer->async_wait(
            [timer = deadline.timer, stream, expired = deadline.expired]( const boost::system::error_code &error )
            {
                if ( !error )
                {
                    expired->store( true );
                    CancelStream( *stream );
                }
            } );
        return deadline;
    }

    void CancelDeadline( const Deadline &deadline )
    {
        boost::system::error_code ignored;
        deadline.timer->cancel( ignored );
    }

    /// @brief Synchronous Beast parse of a completed raw response buffer,
    /// replacing the legacy \r\n\r\n slice (LPM-05's root fix).
    sgns::HTTPClient::Result ParseResponse( const std::string &raw )
    {
        boost::beast::http::response_parser<boost::beast::http::string_body> parser;
        parser.body_limit( ( std::numeric_limits<std::uint64_t>::max )() );
        boost::beast::error_code ec;
        // Feed the buffer incrementally: put() consumes only what the current
        // parser state accepts (headers first, then body) -- a single call on
        // a full response leaves the body unconsumed. Loop until it stops
        // eating, then put_eof() finalizes the parse.
        std::size_t offset = 0;
        while ( offset < raw.size() )
        {
            std::size_t consumed = parser.put( boost::asio::buffer( raw.data() + offset, raw.size() - offset ), ec );
            if ( ec )
            {
                return outcome::failure( sgns::http::ClientError::NO_HEADER );
            }
            if ( consumed == 0 )
            {
                break;
            }
            offset += consumed;
        }
        if ( !parser.is_done() )
        {
            parser.put_eof( ec );
            if ( ec )
            {
                return outcome::failure( sgns::http::ClientError::NO_HEADER );
            }
        }
        auto                                     &message = parser.get();
        sgns::http::Response                      response;
        response.status = static_cast<unsigned>( message.result_int() );
        response.reason = std::string( message.reason() );
        for ( const auto &field : message )
        {
            response.headers.emplace( std::string( field.name_string() ), std::string( field.value() ) );
        }
        response.body = std::move( message.body() );
        return outcome::success( std::move( response ) );
    }
} // namespace

namespace sgns
{
    using namespace boost::asio;

    HTTPClient::HTTPClient( std::string http_host, std::string http_target, std::string http_port )
    {
        http_host_   = http_host;
        http_target_ = http_target;
        http_port_   = http_port;
    }

    void HTTPClient::Execute( std::shared_ptr<boost::asio::io_context> ioc, const http::RequestOptions &options, Completion onDone )
    {
        const bool useTLS = options.caCertFile.has_value();

        // Get DNS result for hostname
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
            boost::asio::post( *ioc, [onDone, ioc]() { onDone( ioc, outcome::failure( http::ClientError::RESOLVE_FAILED ) ); } );
            return;
        }
        catch ( const std::exception &e )
        {
            m_logger->error( "Error resolving address: {}", e.what() );
            boost::asio::post( *ioc, [onDone, ioc]() { onDone( ioc, outcome::failure( http::ClientError::RESOLVE_FAILED ) ); } );
            return;
        }
        catch ( ... )
        {
            m_logger->error( "Error resolving address: Unknown" );
            boost::asio::post( *ioc, [onDone, ioc]() { onDone( ioc, outcome::failure( http::ClientError::RESOLVE_FAILED ) ); } );
            return;
        }

        // TLS context with positive verification: verify_peer + pinned CA +
        // hostname verification (D-06/D-07). Plain HTTP skips all of it.
        std::shared_ptr<boost::asio::ssl::context> ssl_context;
        if ( useTLS )
        {
            ssl_context = std::make_shared<boost::asio::ssl::context>( boost::asio::ssl::context::tls );

            // Disclude certain older insecure options
            ssl_context->set_options( boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
                                      boost::asio::ssl::context::no_sslv3 );

            ssl_context->set_verify_mode( boost::asio::ssl::verify_peer );

            boost::system::error_code ca_ec;
            ssl_context->load_verify_file( *options.caCertFile, ca_ec );
            if ( ca_ec )
            {
                m_logger->error( "Failed to load CA file ({}): {}", *options.caCertFile, ca_ec.message() );
                boost::asio::post(
                    *ioc, [onDone, ioc]() { onDone( ioc, outcome::failure( http::ClientError::TLS_CA_LOAD_FAILED ) ); } );
                return;
            }

            ssl_context->set_verify_callback( boost::asio::ssl::host_name_verification( http_host_ ) );
        }

        auto stream = std::make_shared<Stream>( std::in_place_type<PlainSocket>, *ioc );
        if ( useTLS )
        {
            *stream = Stream( std::in_place_type<SslSocket>, *ioc, *ssl_context );
            auto   &ssl_stream = std::get<SslSocket>( *stream );
            if ( !SSL_set_tlsext_host_name( ssl_stream.native_handle(), http_host_.c_str() ) )
            {
                unsigned long err = ERR_get_error();
                m_logger->error( "Failed to set SNI: {}", ERR_reason_error_string( err ) );
                boost::asio::post(
                    *ioc, [onDone, ioc]() { onDone( ioc, outcome::failure( http::ClientError::TLS_HANDSHAKE_FAILED ) ); } );
                return;
            }
        }

        auto connect_deadline = ArmDeadline( ioc, stream, options.connectTimeout );
        boost::asio::async_connect(
            std::visit( []( auto &s ) -> boost::asio::ip::tcp::socket::lowest_layer_type & { return s.lowest_layer(); },
                        *stream ),
            *endpoints,
            [self = shared_from_this(), ioc, options, ssl_context, stream, onDone, connect_deadline](
                const boost::system::error_code &connect_error, const boost::asio::ip::tcp::endpoint & )
            {
                CancelDeadline( connect_deadline );
                if ( !connect_error )
                {
                    if ( !options.caCertFile.has_value() )
                    {
                        // Plain HTTP: no handshake -- go straight to the exchange
                        self->StartExchange( ioc, options, stream, onDone );
                        return;
                    }
                    auto handshake_deadline = ArmDeadline( ioc, stream, options.handshakeTimeout );
                    std::get<SslSocket>( *stream ).async_handshake(
                        boost::asio::ssl::stream_base::client,
                        [self, ioc, options, ssl_context, stream, onDone, handshake_deadline](
                            const boost::system::error_code &handshake_error )
                        {
                            CancelDeadline( handshake_deadline );
                            if ( !handshake_error )
                            {
                                self->StartExchange( ioc, options, stream, onDone );
                            }
                            else
                            {
                                self->m_logger->error( "Handshake error: {}", handshake_error.message() );
                                onDone( ioc,
                                        outcome::failure( handshake_deadline.expired->load()
                                                              ? http::ClientError::TIMEOUT
                                                              : http::ClientError::TLS_HANDSHAKE_FAILED ) );
                            }
                        } );
                }
                else
                {
                    self->m_logger->error( "Connection error: {}", connect_error.message() );
                    onDone( ioc,
                            outcome::failure( connect_deadline.expired->load() ? http::ClientError::TIMEOUT
                                                                               : http::ClientError::CONNECT_FAILED ) );
                }
            } );
    }

    void HTTPClient::StartExchange( std::shared_ptr<boost::asio::io_context> ioc,
                                    const http::RequestOptions              &options,
                                    std::shared_ptr<void>                    stream,
                                    Completion                               onDone )
    {
        auto typed_stream = std::static_pointer_cast<Stream>( stream );

        // Build the GET request -- UA comes from options (D-08), never a literal
        std::string get_request = "GET " + http_target_ + " HTTP/1.1\r\nHost: " + http_host_ + "\r\nUser-Agent: " +
                                  options.userAgent + "\r\nConnection: close\r\n";
        for ( const auto &[name, value] : options.extraHeaders )
        {
            get_request += name + ": " + value + "\r\n";
        }
        get_request += "\r\n";

        auto read_deadline = ArmDeadline( ioc, typed_stream, options.readTimeout );
        // The request string must outlive StartExchange (dangling-view fix)
        auto request_ptr = std::make_shared<std::string>( std::move( get_request ) );
        std::visit(
            [request_ptr, ioc, typed_stream, read_deadline, self = shared_from_this(), onDone]( auto &s )
            {
                boost::asio::async_write(
                    s,
                    boost::asio::buffer( *request_ptr ),
                    [self, ioc, typed_stream, onDone, read_deadline, request_ptr]( const boost::system::error_code &write_error,
                                                                      std::size_t )
                    {
                        if ( !write_error )
                        {
                            auto buffer = std::make_shared<boost::asio::streambuf>();
                            std::visit(
                                [ioc, typed_stream, buffer, onDone, read_deadline, self]( auto &sock )
                                {
                                    boost::asio::async_read(
                                        sock,
                                        *buffer,
                                        boost::asio::transfer_all(),
                                        [self, ioc, typed_stream, buffer, onDone, read_deadline](
                                            const boost::system::error_code &read_error, std::size_t )
                                        {
                                            CancelDeadline( read_deadline );
                                            // EOF is success -- Connection: close (R10)
                                            if ( read_error && read_error != boost::asio::error::eof )
                                            {
                                                self->m_logger->error( "Error, connection interrupted" );
                                                onDone( ioc,
                                                        outcome::failure( read_deadline.expired->load()
                                                                              ? http::ClientError::TIMEOUT
                                                                              : http::ClientError::READ_INTERRUPTED ) );
                                                return;
                                            }
                                            std::string raw( boost::asio::buffers_begin( buffer->data() ),
                                                             boost::asio::buffers_end( buffer->data() ) );
                                            onDone( ioc, ParseResponse( raw ) );
                                        } );
                                },
                                *typed_stream );
                        }
                        else
                        {
                            CancelDeadline( read_deadline );
                            self->m_logger->error( "Error in async_write: {}", write_error.message() );
                            onDone( ioc,
                                    outcome::failure( read_deadline.expired->load() ? http::ClientError::TIMEOUT
                                                                                    : http::ClientError::WRITE_FAILED ) );
                        }
                    } );
            },
            *typed_stream );
    }

    HTTPClient::Result HTTPClient::ExecuteBlocking( std::shared_ptr<boost::asio::io_context> ioc, const http::RequestOptions &options )
    {
        std::promise<Result>                                                                            promise;
        std::future<HTTPClient::Result>                                                                 future = promise.get_future();
        auto workGuard = std::make_shared<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            ioc->get_executor() );

        Execute( ioc,
                 options,
                 [&promise, workGuard]( std::shared_ptr<boost::asio::io_context>, Result result )
                 {
                     promise.set_value( std::move( result ) );
                     // Release the guard on the io thread BEFORE run() can
                     // observe it -- held-forever guards deadlock run() (P-8).
                     workGuard->reset();
                 } );

        ioc->run();
        return future.get();
    }
} // namespace sgns