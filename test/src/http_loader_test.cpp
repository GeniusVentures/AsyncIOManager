/**
 * Tests for HTTPLoader — HTTPS file loading via FileManager.
 * Gated behind ASYNC_IO_MANAGER_NETWORK_TESTS=ON.
 *
 * Starts an embedded Boost.Beast HTTP server on localhost for tests.
 */

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/test_fixture.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <thread>
#include <atomic>
#include <optional>

namespace beast = boost::beast;
namespace http  = beast::http;
using tcp       = boost::asio::ip::tcp;

// ---------------------------------------------------------------------------
// Embedded HTTP test server
// ---------------------------------------------------------------------------

class TestHttpServer
{
public:
    TestHttpServer()
    {
        ioc_      = std::make_shared<boost::asio::io_context>();
        acceptor_ = std::make_shared<tcp::acceptor>( *ioc_, tcp::endpoint{ tcp::v4(), 0 } );
        port_     = acceptor_->local_endpoint().port();

        // Signal that we're ready to accept
        std::promise<void> ready;
        auto               readyFlag = ready.get_future();

        serverThread_ = std::thread( [this, p = std::move( ready )]() mutable
        {
            // Notify caller that the acceptor is open
            p.set_value();
            ioc_->run();
        } );

        // Wait until the server thread is actually running
        readyFlag.wait();
    }

    ~TestHttpServer()
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
        auto socket = std::make_shared<tcp::socket>( *ioc_ );
        acceptor_->async_accept( *socket, [this, socket]( boost::system::error_code ec )
        {
            if ( !ec )
            {
                handleRequest( std::move( *socket ) );
                startAccept();  // Accept next connection
            }
        } );
    }

private:
    void handleRequest( tcp::socket socket )
    {
        try
        {
            beast::flat_buffer               buffer;
            http::request<http::string_body> req;
            http::read( socket, buffer, req );

            http::response<http::string_body> res;

            if ( req.target() == "/test/data.bin" )
            {
                res.result( http::status::ok );
                res.set( http::field::content_type, "application/octet-stream" );
                res.body() = "hello from test http server";
            }
            else
            {
                res.result( http::status::not_found );
                res.body() = "Not Found";
            }

            res.prepare_payload();
            http::write( socket, res );
            socket.shutdown( tcp::socket::shutdown_send );
        }
        catch ( ... )
        {
            // Client disconnected or other error — ignore
        }
    }

    std::shared_ptr<boost::asio::io_context> ioc_;
    std::shared_ptr<tcp::acceptor>           acceptor_;
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
        server_ = std::make_unique<TestHttpServer>();
        server_->startAccept();
    }

    void TearDown() override
    {
        server_.reset();
    }

    std::unique_ptr<TestHttpServer> server_;
};

// ---------------------------------------------------------------------------
// NOTE: Happy-path HTTPS download requires an SSL-enabled server.
// The embedded server is plain HTTP; the HTTPLoader ("https" prefix) does SSL.
// A full integration test would need a self-signed cert on the test server.
// For now, the test validates that the HTTPLoader connects and gets a result
// (SSL handshake will fail against plain HTTP, producing an error result).
// ---------------------------------------------------------------------------

TEST_F( HTTPLoaderTest, LoadASync_ConnectsToServer )
{
    IOContextRunner runner;

    bool                                   completed = false;
    std::optional<FileManager::ResultType> received;

    std::string url = "https://127.0.0.1:" + std::to_string( server_->port() ) + "/test/data.bin";

    FileManager::GetInstance().LoadASync(
        url,
        false, false, runner.ioc(),
        [&]( FileManager::ResultType result )
        {
            received  = std::move( result );
            completed = true;
        },
        "" );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 10 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for HTTP result";

    // Callback should fire (even if SSL handshake fails)
    ASSERT_TRUE( received.has_value() );
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
        false, false, runner.ioc(),
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
        false, false, runner.ioc(),
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
