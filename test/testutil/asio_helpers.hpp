#pragma once

#include <boost/asio/io_context.hpp>
#include <thread>
#include <chrono>
#include <memory>

/// @brief Helper to run an io_context in a background thread for async tests.
class IOContextRunner
{
public:
    explicit IOContextRunner()
    {
        ioc_         = std::make_shared<boost::asio::io_context>();
        work_guard_  = std::make_unique<boost::asio::io_context::work>( *ioc_ );
        io_thread_   = std::thread( [this]() { ioc_->run(); } );
    }

    ~IOContextRunner()
    {
        stop();
    }

    IOContextRunner( const IOContextRunner & )            = delete;
    IOContextRunner &operator=( const IOContextRunner & ) = delete;

    std::shared_ptr<boost::asio::io_context> ioc() const { return ioc_; }

    /// @brief Stop the io_context and join the thread.
    void stop()
    {
        if ( !stopped_ )
        {
            work_guard_.reset();
            if ( ioc_ && !ioc_->stopped() )
            {
                ioc_->stop();
            }
            if ( io_thread_.joinable() )
            {
                io_thread_.join();
            }
            stopped_ = true;
        }
    }

    /// @brief Reset for a new run (restarts the io_context thread).
    void restart()
    {
        stop();
        ioc_->restart();
        work_guard_ = std::make_unique<boost::asio::io_context::work>( *ioc_ );
        io_thread_  = std::thread( [this]() { ioc_->run(); } );
        stopped_    = false;
    }

private:
    std::shared_ptr<boost::asio::io_context>             ioc_;
    std::unique_ptr<boost::asio::io_context::work>       work_guard_;
    std::thread                                           io_thread_;
    bool                                                  stopped_ = false;
};

/// @brief Poll until a condition is true, with timeout.
/// @return true if condition became true, false on timeout.
template <typename Predicate>
bool pollUntil( Predicate pred, std::chrono::milliseconds timeout = std::chrono::seconds( 30 ),
                std::chrono::milliseconds interval = std::chrono::milliseconds( 50 ) )
{
    auto start = std::chrono::steady_clock::now();
    while ( !pred() )
    {
        if ( std::chrono::steady_clock::now() - start > timeout )
        {
            return false;
        }
        std::this_thread::sleep_for( interval );
    }
    return true;
}
