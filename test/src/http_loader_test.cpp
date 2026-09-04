/**
 * Tests for HTTPLoader — HTTPS file loading via FileManager.
 * Gated behind ASYNC_IO_MANAGER_NETWORK_TESTS=ON.
 *
 * Starts an embedded Boost.Beast HTTPS server on localhost with a
 * self-signed certificate for tests.
 */

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include "HTTPCommon.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/test_fixture.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <thread>
#include <atomic>
#include <optional>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace ssl   = boost::asio::ssl;
using tcp       = boost::asio::ip::tcp;

// ---------------------------------------------------------------------------
// Embedded HTTPS test server (self-signed cert)
// ---------------------------------------------------------------------------

class TestHttpsServer
{
public:
    TestHttpsServer()
    {
        ioc_      = std::make_shared<boost::asio::io_context>();
        acceptor_ = std::make_shared<tcp::acceptor>( *ioc_, tcp::endpoint{ tcp::v4(), 0 } );
        port_     = acceptor_->local_endpoint().port();

        // Set up SSL context with self-signed cert
        ssl_ctx_ = std::make_shared<ssl::context>( ssl::context::tls );
        ssl_ctx_->set_options( ssl::context::default_workarounds | ssl::context::no_sslv2 |
                               ssl::context::no_sslv3 );
        ssl_ctx_->use_certificate_chain_file( std::string( TEST_CERT_DIR ) + "/cert.pem" );
        ssl_ctx_->use_private_key_file( std::string( TEST_CERT_DIR ) + "/key.pem", ssl::context::pem );

        // Signal that we're ready to accept
        std::promise<void> ready;
        auto               readyFlag = ready.get_future();

        serverThread_ = std::thread( [this, p = std::move( ready )]() mutable
        {
            p.set_value();
            acceptLoop();
        } );

        readyFlag.wait();
    }

    ~TestHttpsServer()
    {
        boost::system::error_code ec;
        acceptor_->close( ec );
        ioc_->stop();
        if ( serverThread_.joinable() )
        {
            serverThread_.join();
        }
    }

    uint16_t port() const { return port_; }

    void startAccept()
    {
        // acceptLoop() already runs directly on the server thread.
        // No-op: the server is ready as soon as the constructor returns.
    }

private:
    void acceptLoop()
    {
        while ( true )
        {
            boost::system::error_code ec;
            tcp::socket               sock( *ioc_ );

            acceptor_->accept( sock, ec );
            if ( ec )
                break;  // Acceptor closed or other error — stop

            // Wrap accepted socket in SSL
            ssl::stream<tcp::socket> stream( std::move( sock ), *ssl_ctx_ );

            // SSL handshake (server side)
            stream.handshake( ssl::stream_base::server, ec );
            if ( ec )
                continue;  // Handshake failed — skip this connection

            handleRequest( stream );

            // Graceful SSL shutdown
            stream.shutdown( ec );
        }
    }

    void handleRequest( ssl::stream<tcp::socket> &stream )
    {
        try
        {
            beast::flat_buffer               buffer;
            http::request<http::string_body> req;
            http::read( stream, buffer, req );

            http::response<http::string_body> res;

            if ( req.target() == "/test/data.bin" )
            {
                res.result( http::status::ok );
                res.set( http::field::content_type, "application/octet-stream" );
                res.body() = "hello from test https server";
            }
            else
            {
                res.result( http::status::not_found );
                res.body() = "Not Found";
            }

            res.prepare_payload();
            http::write( stream, res );
        }
        catch ( ... )
        {
            // Client disconnected or other error — ignore
        }
    }

    std::shared_ptr<boost::asio::io_context> ioc_;
    std::shared_ptr<tcp::acceptor>           acceptor_;
    std::shared_ptr<ssl::context>            ssl_ctx_;
    uint16_t                                 port_ = 0;
    std::thread                              serverThread_;
};

// ---------------------------------------------------------------------------
// HTTPLoader Tests (via FileManager dispatch)
// ---------------------------------------------------------------------------

class HTTPLoaderTest : public FileManagerTestFixture
{
protected:
    void SetUp() override
    {
        FileManagerTestFixture::SetUp();
        // Accept self-signed cert for the embedded test server
        sgns::HTTPDevice::SetVerifyPeer( false );
        server_ = std::make_unique<TestHttpsServer>();
        server_->startAccept();
    }

    void TearDown() override
    {
        server_.reset();
        // Restore default verification
        sgns::HTTPDevice::SetVerifyPeer( true );
    }

    std::unique_ptr<TestHttpsServer> server_;
};

// ---------------------------------------------------------------------------
// Happy path: full HTTPS roundtrip with self-signed cert
// ---------------------------------------------------------------------------

TEST_F( HTTPLoaderTest, LoadASync_DownloadsOverHttps )
{
    IOContextRunner runner;

    bool                                   completed = false;
    std::optional<FileManager::ResultType> received;

    std::string url = "https://127.0.0.1:" + std::to_string( server_->port() ) + "/test/data.bin";

    FileManager::GetInstance().LoadASync(
        url,
        false, runner.ioc(),
        [&]( FileManager::ResultType result )
        {
            received  = std::move( result );
            completed = true;
        },
        "" );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 10 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for HTTPS result";

    ASSERT_TRUE( received.has_value() );
    ASSERT_TRUE( received->has_value() ) << "HTTPS download should succeed";

    auto &[paths, contents] = *( received->value() );
    ASSERT_EQ( paths.size(), 1u );
    EXPECT_EQ( paths[0], "data.bin" );   // HTTPLoader uses p.filename() — strips directory
    ASSERT_EQ( contents.size(), 1u );
    std::string body( contents[0].begin(), contents[0].end() );
    EXPECT_EQ( body, "hello from test https server" );
}

// ---------------------------------------------------------------------------
// Unhappy paths
// ---------------------------------------------------------------------------

TEST_F( HTTPLoaderTest, LoadASync_InvalidUrlReturnsError )
{
    IOContextRunner runner;

    bool                                   completed = false;
    std::optional<FileManager::ResultType> received;

    // Empty URL should fail URL parsing
    FileManager::GetInstance().LoadASync(
        "https://",  // no host/path
        false, runner.ioc(),
        [&]( FileManager::ResultType result )
        {
            received  = std::move( result );
            completed = true;
        },
        "" );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for error callback";

    ASSERT_TRUE( received.has_value() );
    EXPECT_FALSE( received->has_value() );
}

TEST_F( HTTPLoaderTest, LoadASync_ConnectionRefusedReturnsError )
{
    IOContextRunner runner;

    bool                                   completed = false;
    std::optional<FileManager::ResultType> received;

    // Port 1 is typically closed
    FileManager::GetInstance().LoadASync(
        "https://127.0.0.1:1/test/data.bin",
        false, runner.ioc(),
        [&]( FileManager::ResultType result )
        {
            received  = std::move( result );
            completed = true;
        },
        "" );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 10 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for error callback";

    ASSERT_TRUE( received.has_value() );
    EXPECT_FALSE( received->has_value() );
}
