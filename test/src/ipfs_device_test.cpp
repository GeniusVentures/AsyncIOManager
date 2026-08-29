/**
 * @file ipfs_device_test.cpp
 * @brief Regression coverage for the use-after-free in IPFSDevice::StartFindingPeersWithRetry.
 *
 * The 10s dhtretry_ timer handler previously captured raw `this`. When the last
 * external shared_ptr<IPFSDevice> was dropped while the timer was armed (e.g. the
 * process-global FileManager singleton is setBitswap'd to the next node), the
 * handler ran on freed memory. These tests prove that the armed handler keeps the
 * device alive (weak_ptr does not expire) and that the full retry chain completes
 * on a valid object after the external reference is dropped.
 *
 * Gated behind ASYNC_IO_MANAGER_NETWORK_TESTS=ON (BitswapNode fixture).
 */

#include <gtest/gtest.h>
#include "IPFSCommon.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/bitswap_node.hpp"
#include <libp2p/multi/content_identifier_codec.hpp>

#include <memory>
#include <string>

using namespace sgns;

namespace
{
/// Valid CIDv0 that always parses but is never published by the local node
/// (no provider is ever registered for it).
constexpr char kNeverPublishedCid[] = "QmYwAPJzv5CZsnA625s3Xf2nemtYgPpHdWEz79ojWnPbdG";
} // namespace

// ---------------------------------------------------------------------------
// Fixture: one client-only BitswapNode for the suite. These tests never touch
// disk, so there is no FileManagerTestFixture base.
// ---------------------------------------------------------------------------

class IPFSDeviceRetryTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        try
        {
            s_clientNode = std::make_unique<BitswapNode>();
        }
        catch ( const std::exception &e )
        {
            GTEST_SKIP() << "Cannot create BitswapNode: " << e.what();
        }
    }

    static void TearDownTestSuite()
    {
        s_clientNode.reset();
    }

protected:
    static std::unique_ptr<BitswapNode> s_clientNode;
};

std::unique_ptr<BitswapNode> IPFSDeviceRetryTest::s_clientNode;

// ---------------------------------------------------------------------------
// RetryTimerKeepsDeviceAlive — the use-after-free regression.
// ---------------------------------------------------------------------------

TEST_F( IPFSDeviceRetryTest, RetryTimerKeepsDeviceAlive )
{
    IOContextRunner runner;
    auto            device = IPFSDevice::createWithBitswap( runner.ioc(), s_clientNode->getBitswap(), nullptr ).value();
    const auto      cid    = libp2p::multi::ContentIdentifierCodec::fromString( kNeverPublishedCid ).value();

    // shared_ptr so the completion callback has no dangling by-ref capture
    auto                      fired = std::make_shared<bool>( false );
    IPFSDevice::CompletionCallback callback =
        [fired]( std::shared_ptr<boost::asio::io_context>, IPFSDevice::ResultType, bool, bool )
        {
            *fired = true;
        };

    // Arm the 10s retry timer, then drop the last EXTERNAL reference (the UAF window)
    device->StartFindingPeersWithRetry( runner.ioc(), cid, "uaf_probe.bin", 0, false, false, callback );
    std::weak_ptr<IPFSDevice> watch = device;
    device.reset();

    // Pre-fix this FAILS (object destroyed instantly); post-fix the armed
    // handler's self capture keeps it alive until the timer fires.
    EXPECT_NE( watch.lock(), nullptr ) << "device was freed while retry timer was armed (use-after-free)";

    // Deterministic cleanup: wait out the timer so the test binary never tears
    // down the suite BitswapNode with a live device/armed handler still pending
    // (avoids teardown-order crashes when run alone via --gtest_filter).
    ASSERT_TRUE( pollUntil( [&watch]() { return watch.expired(); }, std::chrono::seconds( 20 ) ) )
        << "armed retry handler must release the device after the timer fires";
}

// ---------------------------------------------------------------------------
// RetryChainCompletesAfterReferenceDropped — end-to-end retry survival.
// ---------------------------------------------------------------------------

TEST_F( IPFSDeviceRetryTest, RetryChainCompletesAfterReferenceDropped )
{
    IOContextRunner runner;
    auto            device = IPFSDevice::createWithBitswap( runner.ioc(), s_clientNode->getBitswap(), nullptr ).value();
    const auto      cid    = libp2p::multi::ContentIdentifierCodec::fromString( kNeverPublishedCid ).value();

    auto                      fired      = std::make_shared<bool>( false );
    auto                      gotFailure = std::make_shared<bool>( false );
    IPFSDevice::CompletionCallback callback =
        [fired, gotFailure]( std::shared_ptr<boost::asio::io_context>, IPFSDevice::ResultType result, bool, bool )
        {
            *fired = true;
            if ( !result.has_value() )
            {
                *gotFailure = true;
            }
        };

    // Arm FIRST, then drop the strong reference before the timer fires.
    device->StartFindingPeersWithRetry( runner.ioc(), cid, "uaf_probe.bin", 0, false, false, callback );
    std::weak_ptr<IPFSDevice> watch = device;
    device.reset();

    // Verified chain: timer fires at +10s -> StartFindingPeers -> no DHT -> warn
    // -> RequestBlockMain -> bitswap RequestContent -> no providers -> failure
    // path posts handle_read (CANNOT_DECODE) onto runner.ioc() -> *fired = true.
    // handle_read firing proves the ENTIRE retry chain executed on a valid
    // object after the last external reference was dropped.
    ASSERT_TRUE( pollUntil( [fired]() { return *fired; }, std::chrono::seconds( 20 ) ) )
        << "retry chain must complete after the external reference is dropped";
    EXPECT_TRUE( *gotFailure ) << "completion was the expected failure, not a hang";

    // Teardown hygiene: the handler chain must release the device.
    EXPECT_TRUE( pollUntil( [&watch]() { return watch.expired(); }, std::chrono::seconds( 5 ) ) );
}
