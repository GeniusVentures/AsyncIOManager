/**
 * @file ipfs_device_test.cpp
 * @brief Regression coverage for use-after-free lifetime bugs in IPFSDevice async callbacks.
 *
 * Two lambdas previously captured raw `this` while their async operation could
 * outlive the last external shared_ptr<IPFSDevice> (e.g. the process-global
 * FileManager singleton is setBitswap'd to the next node): the 10s dhtretry_
 * timer handler in StartFindingPeersWithRetry, and the provider-query callback
 * passed to dht_->FindProviders in StartFindingPeers. These tests prove that
 * each in-flight operation keeps the device alive (weak_ptr does not expire)
 * and that the full retry chain completes on a valid object after the external
 * reference is dropped.
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

// ---------------------------------------------------------------------------
// FindProvidersCallbackKeepsDeviceAlive — the second use-after-free regression:
// the provider-query callback previously captured raw `this` ([=]) and runs
// async once a DHT is attached; it fires ~20s later on a freed device.
// ---------------------------------------------------------------------------

TEST_F( IPFSDeviceRetryTest, FindProvidersCallbackKeepsDeviceAlive )
{
    IOContextRunner runner;

    // Minimal DHT stack mirroring IPFSDevice's singleton constructor: kademlia
    // via the host injector, IpfsDHT with NO bootstrap addresses (hermetic —
    // nothing ever leaves the process).
    libp2p::protocol::kademlia::Config kademlia_config;
    auto                               injector = libp2p::injector::makeHostInjector(
        libp2p::injector::makeKademliaInjector( libp2p::injector::useKademliaConfig( kademlia_config ) ) );
    auto host     = injector.create<std::shared_ptr<libp2p::Host>>();
    auto kademlia = injector.create<std::shared_ptr<libp2p::protocol::kademlia::Kademlia>>();

    // Seed one unreachable peer into the routing table. With an EMPTY table the
    // executor finds nothing to dial and completes the query synchronously
    // (verified against find_providers_executor.cpp: spawn() calls done() when
    // requests_in_progress_ == 0). One seeded peer makes newStream() dial
    // asynchronously — the handler stays stored in the executor while the dial
    // pends, which is the in-flight window the GT crash hit.
    const auto cid     = libp2p::multi::ContentIdentifierCodec::fromString( kNeverPublishedCid ).value();
    const auto seedId  = libp2p::peer::PeerId::fromHash( cid.content_address ).value();
    const libp2p::peer::PeerInfo seedPeer{
        seedId, { libp2p::multi::Multiaddress::create( "/ip4/127.0.0.1/tcp/4001" ).value() } };
    kademlia->addPeer( seedPeer, true );

    auto dht = std::make_shared<sgns::ipfs_lite::ipfs::dht::IpfsDHT>( kademlia, std::vector<std::string>{}, runner.ioc() );

    auto device = IPFSDevice::createWithBitswap( runner.ioc(), s_clientNode->getBitswap(), dht ).value();

    auto                      fired = std::make_shared<bool>( false );
    IPFSDevice::CompletionCallback callback =
        [fired]( std::shared_ptr<boost::asio::io_context>, IPFSDevice::ResultType, bool, bool )
        {
            *fired = true;
        };

    // Start the provider query, then drop the last EXTERNAL reference while
    // the FindProviders query is in flight (the UAF window).
    device->StartFindingPeers( runner.ioc(), cid, "dht_probe.bin", 0, false, false, callback );
    std::weak_ptr<IPFSDevice> watch = device;
    device.reset();

    // Verified: with a seeded routing table the dial pends on the (unrun)
    // libp2p io_context, so the handler stays stored in the executor across
    // the reset. Pre-fix ([=] captures raw this, nothing owns the device) the
    // weak_ptr expires immediately; post-fix the stored callback holds self.
    EXPECT_NE( watch.lock(), nullptr ) << "device was freed while FindProviders query was in flight (use-after-free)";

    // No expiry wait: a pending query legitimately keeps the device alive until
    // this scope ends and the DHT stack (dht/kademlia/host, declared after
    // runner) tears down, destroying the stored callback on this thread.
}
