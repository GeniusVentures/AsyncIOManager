/**
 * Header file for the HTTPClient — status-aware sibling of HTTPDevice (D-01).
 *
 * Unlike HTTPDevice (which slices the response at \r\n\r\n and discards the
 * status line), HTTPClient delivers the full http::Response on every
 * completed exchange (LPM-05) and performs positive TLS verification
 * (verify_peer + SNI + caller-pinned CA, LPM-08 / D-06/D-07).
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include "boost/asio.hpp"
#include "boost/asio/ssl.hpp"
#include <libp2p/outcome/outcome.hpp>
#include <asiomgr-logger.hpp>

#include "HTTPTypes.hpp"

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    using namespace boost::asio;

    /**
     * This class executes one status-aware HTTP(S) exchange and reports the
     * full response (status/reason/headers/body) to the completion handler.
     */
    class HTTPClient : public std::enable_shared_from_this<HTTPClient>
    {
    public:
        /// @brief Result of one exchange: a full Response on any completed
        /// HTTP exchange, or a http::ClientError when the exchange failed.
        using Result = outcome::result<http::Response>;

        /// @brief Completion callback. The io_context is echoed back so the
        /// caller can manage the run-loop; parse/save flags do not apply to
        /// this surface and are not carried.
        using Completion = std::function<void( std::shared_ptr<boost::asio::io_context> ioc, Result result )>;

        /**
         * Create an HTTP client for one request.
         * @param http_host - address of HTTP server
         * @param http_target - path+query on the HTTP server (D-04: the query
         * string is already part of the target; parseHTTPUrl passes it through)
         * @param http_port - port for the HTTP server
         */
        HTTPClient( std::string http_host, std::string http_target, std::string http_port );

        ~HTTPClient()
        {
            // Cleanup
        }

        /**
         * Start the exchange asynchronously on a caller-supplied io_context.
         * @param ioc - ASIO context the exchange runs on (D-09: the caller owns
         * the executor; HTTPClient is executor-agnostic and owns no threads)
         * @param options - per-request UA / timeouts / CA file (D-08/D-10/D-07)
         * @param onDone - completion handler invoked with the full Response
         * @note One HTTPClient object performs exactly one request — construct
         * a new object per exchange, same lifetime rule as HTTPDevice today.
         */
        void Execute( std::shared_ptr<boost::asio::io_context> ioc, const http::RequestOptions &options, Completion onDone );

        /**
         * Run one exchange to completion, blocking the calling thread.
         * @param ioc - ASIO context supplied by the caller (D-09); a work guard
         * keeps it alive until the exchange completes, then run() returns
         * @param options - per-request UA / timeouts / CA file
         * @return The full Response on any completed HTTP exchange, or a
         * http::ClientError when the exchange failed
         */
        Result ExecuteBlocking( std::shared_ptr<boost::asio::io_context> ioc, const http::RequestOptions &options );

    private:
        /**
         * Continue the exchange after the socket layer is established:
         * write the request and read the response.
         * @param ioc - ASIO context the exchange runs on
         * @param options - the per-request options the exchange started with
         * @param stream - the established plain/TLS stream
         * @param onDone - completion handler
         */
        void StartExchange( std::shared_ptr<boost::asio::io_context>                          ioc,
                            const http::RequestOptions                                       &options,
                            std::shared_ptr<void>                                            stream,
                            Completion                                                       onDone );

        sgns::asiomgr::Logger m_logger = sgns::asiomgr::createLogger( "HTTPClient" );
        // Common vars used for the exchange
        std::string http_host_;
        std::string http_target_;
        std::string http_port_;
    };
} // namespace sgns
