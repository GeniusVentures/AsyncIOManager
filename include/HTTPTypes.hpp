/**
 * Header file for the HTTPTypes — data contract for the status-aware
 * HTTPClient surface (D-01..D-10).
 */
#pragma once
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <libp2p/outcome/outcome.hpp>

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    namespace http
    {
        /// @brief Per-request options for the status-aware HTTPClient surface.
        /// @brief Timeouts are per-request parameters (D-10); the user agent is
        /// a per-request parameter (D-08); TLS trust is pinned to a caller
        /// supplied CA file (D-07).
        struct RequestOptions
        {
            /// @brief User-Agent header value (D-08). The price client sets a
            /// CoinGecko-friendly product/contact UA; the default mirrors the
            /// legacy HTTPDevice literal so non-price consumers are unchanged.
            std::string userAgent = "GeniusAI/1.0 (SGNS AsyncIO Manager)";

            /// @brief TCP connect deadline (D-10). Default mirrors HTTPDevice's 10s.
            std::chrono::milliseconds connectTimeout{ 10000 };

            /// @brief TLS handshake deadline (D-10). Default mirrors HTTPDevice's 10s.
            std::chrono::milliseconds handshakeTimeout{ 10000 };

            /// @brief Response read deadline (D-10). Default mirrors HTTPDevice's 30s.
            std::chrono::milliseconds readTimeout{ 30000 };

            /// @brief Path to a PEM CA bundle used for verify_peer (D-06/D-07).
            /// nullopt means plain HTTP — no TLS objects are created at all.
            std::optional<std::string> caCertFile;

            /// @brief Additional request headers, appended verbatim after the
            /// standard GET/Host/UA/Connection block.
            std::vector<std::pair<std::string, std::string>> extraHeaders;
        };

        /// @brief A completed HTTP exchange — status, reason, headers, body.
        /// @brief Any completed exchange is a transport SUCCESS (LPM-05): a 403
        /// arrives here with status=403; status==0 means no HTTP response at
        /// all (the exchange failed before a status line was received).
        struct Response
        {
            /// @brief HTTP status code; 0 = no HTTP response (transport failed first)
            unsigned status = 0;

            /// @brief Status-line reason phrase as received
            std::string reason;

            /// @brief Response headers, case preserved as received
            std::multimap<std::string, std::string> headers;

            /// @brief Response body bytes
            std::string body;
        };

        /// @brief Error taxonomy for the status-aware client surface.
        enum class ClientError
        {
            RESOLVE_FAILED        = 1,
            CONNECT_FAILED        = 2,
            TLS_HANDSHAKE_FAILED  = 3,
            TLS_CA_LOAD_FAILED    = 4,
            WRITE_FAILED          = 5,
            READ_INTERRUPTED      = 6,
            NO_HEADER             = 7,
            TIMEOUT               = 8,
        };

        /// @brief Maps a ClientError onto std::error_code (defined in HTTPClient.cpp).
        std::error_code make_error_code( ClientError e );
    } // namespace http
} // namespace sgns

namespace std
{
    template <>
    struct is_error_code_enum<sgns::http::ClientError> : true_type
    {
    };
} // namespace std
