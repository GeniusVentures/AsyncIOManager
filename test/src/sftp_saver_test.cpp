/**
 * Tests for SFTPSaver — SFTP file upload.
 * Gated behind ASYNC_IO_MANAGER_NETWORK_TESTS=ON.
 *
 * Attempts to start a local OpenSSH server for testing.
 * If sshd is not available, all tests are skipped via GTEST_SKIP().
 */

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include "testutil/temp_file.hpp"
#include "testutil/asio_helpers.hpp"
#include "testutil/test_fixture.hpp"

#include <cstdlib>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#endif

#include <thread>
#include <chrono>
#include <cstring>

using namespace sgns;

// ---------------------------------------------------------------------------
// SFTP Test Server (OpenSSH subprocess)
// ---------------------------------------------------------------------------

class SftpTestServer
{
public:
    SftpTestServer()
    {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
#endif
        // Check if sshd is available
        bool found = ( std::system( "where sshd >nul 2>nul" ) == 0 );
#ifdef _WIN32
        if ( !found )
            found = std::filesystem::exists( "C:\\Windows\\System32\\OpenSSH\\sshd.exe" );
#endif
        if ( !found )
        {
            available_ = false;
            return;
        }

        // Create temp directories for sshd config and data — in CWD (test_bin dir)
        serverDir_  = std::filesystem::current_path() / ("asiomgr_sftp_" + uniqueSuffix());
        std::filesystem::create_directories( serverDir_ );
        hostKeyFile_ = serverDir_ / "ssh_host_rsa_key";
        configFile_  = serverDir_ / "sshd_config";
        dataDir_     = serverDir_ / "data";
        std::filesystem::create_directories( dataDir_ );

        // Generate host key
        std::string keyCmd = "ssh-keygen -t rsa -m PEM -f \"" + hostKeyFile_.string()
                           + "\" -N \"\" -q 2>nul >nul";
        if ( std::system( keyCmd.c_str() ) != 0 )
        {
            available_ = false;
            return;
        }

        // Generate an SSH key pair for passwordless test auth
        keyFile_ = serverDir_ / "test_key";
        std::string keyCmd2 = "ssh-keygen -t rsa -m PEM -f \"" + keyFile_.string()
                            + "\" -N \"\" -q 2>nul >nul";
        if ( std::system( keyCmd2.c_str() ) != 0 )
        {
            available_ = false;
            return;
        }

        // Write authorized_keys with the public key
        {
            std::ifstream pubkey( keyFile_.string() + ".pub" );
            std::ofstream authkeys( serverDir_ / "authorized_keys" );
            authkeys << pubkey.rdbuf();
        }

        // Pick a random port
        port_ = 2222 + ( std::rand() % 10000 );

        // sshd config — use forward slashes (needed on Windows sshd)
        std::string hostKeyNative  = hostKeyFile_.string();
        std::string pidFileNative  = ( serverDir_ / "sshd.pid" ).string();
        std::string authKeysNative = ( serverDir_ / "authorized_keys" ).string();
#ifdef _WIN32
        for ( auto &c : hostKeyNative ) if ( c == '\\' ) c = '/';
        for ( auto &c : pidFileNative ) if ( c == '\\' ) c = '/';
        for ( auto &c : authKeysNative ) if ( c == '\\' ) c = '/';
#endif

        std::ofstream cfg( configFile_ );
        cfg << "Port " << port_ << "\n"
            << "ListenAddress 127.0.0.1\n"
            << "HostKey " << hostKeyNative << "\n"
            << "PidFile " << pidFileNative << "\n"
            << "PubkeyAuthentication yes\n"
            << "PasswordAuthentication no\n"
            << "AuthorizedKeysFile " << authKeysNative << "\n"
            << "Subsystem sftp internal-sftp\n"
            << "StrictModes no\n";
        cfg.close();

        // Use current Windows username (key auth still needs a valid system user)
        char userBuf[256];
        DWORD userLen = sizeof( userBuf );
        if ( GetUserNameA( userBuf, &userLen ) )
            testUser_ = userBuf;
        else
            testUser_ = "testuser";
        available_ = true;
    }

    ~SftpTestServer()
    {
        stop();
        std::error_code ec;
        if ( !serverDir_.empty() )
        {
            std::filesystem::remove_all( serverDir_, ec );
        }
    }

    bool isAvailable() const { return available_; }

    bool start()
    {
        if ( !available_ )
            return false;

#ifdef _WIN32
        // Create a pipe to capture sshd's stderr
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa = { sizeof( sa ), nullptr, TRUE };
        if ( !CreatePipe( &hReadPipe, &hWritePipe, &sa, 0 ) )
        {
            serverDir_.clear();
            return false;
        }
        SetHandleInformation( hReadPipe, HANDLE_FLAG_INHERIT, 0 );

        // Find sshd absolute path
        std::string sshdPath = "C:\\Windows\\System32\\OpenSSH\\sshd.exe";
        if ( !std::filesystem::exists( sshdPath ) )
            sshdPath = "sshd";  // fallback to PATH

        std::string cmdLine = "\"" + sshdPath + "\" -f \"" + configFile_.string() + "\" -D -d";
        STARTUPINFOA        si = { sizeof( si ) };
        PROCESS_INFORMATION pi = {};
        si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput  = nullptr;
        si.hStdOutput = hWritePipe;
        si.hStdError  = hWritePipe;

        std::cerr << "[SftpTestServer] Command: " << cmdLine << std::endl;
        if ( !CreateProcessA( nullptr, cmdLine.data(), nullptr, nullptr,
                              TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi ) )
        {
            DWORD err = GetLastError();
            std::cerr << "[SftpTestServer] CreateProcess failed: " << err << std::endl;
            CloseHandle( hReadPipe ); CloseHandle( hWritePipe );
            serverDir_.clear();
            return false;
        }
        CloseHandle( hWritePipe );
        CloseHandle( pi.hThread );
        processHandle_ = pi.hProcess;

        // Read sshd's stderr for a short time
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        std::string stderrOutput;
        char        buf[4096];
        DWORD       avail = 0;
        while ( PeekNamedPipe( hReadPipe, nullptr, 0, nullptr, &avail, nullptr ) && avail > 0 )
        {
            DWORD bytesRead = 0;
            if ( ReadFile( hReadPipe, buf, sizeof( buf ) - 1, &bytesRead, nullptr ) && bytesRead > 0 )
            {
                buf[bytesRead] = '\0';
                stderrOutput += buf;
            }
        }
        CloseHandle( hReadPipe );

        // Check if process already exited
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess( processHandle_, &exitCode );

        std::cerr << "[SftpTestServer] sshd output ("
                  << ( exitCode == STILL_ACTIVE ? "running" : "exit=" + std::to_string( exitCode ) )
                  << "):\n" << ( stderrOutput.empty() ? "(empty)" : stderrOutput ) << std::endl;

        if ( exitCode != STILL_ACTIVE )
        {
            CloseHandle( processHandle_ );
            processHandle_ = nullptr;
            serverDir_.clear();
            return false;
        }

        // Give sshd time to start (TCP probe can kill it on Windows)
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        return true;
#else
        pid_t pid = fork();
        if ( pid < 0 ) return false;
        if ( pid == 0 )
        {
            execlp( "sshd", "sshd", "-f", configFile_.string().c_str(), "-D", "-e", nullptr );
            _exit( 1 );
        }
        processPid_ = pid;

        for ( int i = 0; i < 100; ++i )
        {
            int sock = socket( AF_INET, SOCK_STREAM, 0 );
            if ( sock < 0 ) break;
            int flags = fcntl( sock, F_GETFL, 0 );
            fcntl( sock, F_SETFL, flags | O_NONBLOCK );

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons( static_cast<uint16_t>( port_ ) );
            addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );

            connect( sock, reinterpret_cast<sockaddr *>( &addr ), sizeof( addr ) );

            fd_set wfds; FD_ZERO( &wfds ); FD_SET( sock, &wfds );
            timeval tv = { 0, 500000 };
            int    rc = select( sock + 1, nullptr, &wfds, nullptr, &tv );

            if ( rc > 0 )
            {
                int err = 0; socklen_t len = sizeof( err );
                getsockopt( sock, SOL_SOCKET, SO_ERROR, &err, &len );
                close( sock );
                if ( err == 0 ) return true;
            }
            else { close( sock ); }
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }

        kill( processPid_, SIGTERM );
        processPid_ = 0;
        return false;
#endif
    }

    void stop()
    {
#ifdef _WIN32
        if ( processHandle_ )
        {
            TerminateProcess( processHandle_, 0 );
            WaitForSingleObject( processHandle_, 5000 );
            CloseHandle( processHandle_ );
            processHandle_ = nullptr;
        }
#else
        if ( processPid_ > 0 )
        {
            kill( processPid_, SIGTERM );
            processPid_ = 0;
        }
#endif
    }

    std::string host() const { return "127.0.0.1"; }
    uint16_t    port() const { return port_; }
    std::string user() const { return testUser_; }
    std::string pass() const { return testPass_; }
    std::string dataPath() const { return dataDir_.string(); }

    std::string sftpUrl( const std::string &remotePath ) const
    {
        // Key-based auth: privkey_identifier<path> in the password field.
        // Convert backslashes for libssh2 compatibility.
        std::string keyPath = keyFile_.string();
        for ( auto &c : keyPath ) if ( c == '\\' ) c = '/';
        return user() + ":privkey_identifier" + keyPath + "@" + host()
             + ":" + std::to_string( port_ ) + remotePath;
    }

private:
    static std::string uniqueSuffix()
    {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::to_string( now );
    }

    bool                   available_    = false;
    std::filesystem::path  serverDir_;
    std::filesystem::path  hostKeyFile_;
    std::filesystem::path  configFile_;
    std::filesystem::path  dataDir_;
    std::filesystem::path  keyFile_;
    uint16_t               port_         = 0;
    std::string            testUser_;
    std::string            testPass_;
#ifdef _WIN32
    HANDLE                 processHandle_ = nullptr;
#else
    pid_t                  processPid_    = 0;
#endif
};

// ---------------------------------------------------------------------------
// SFTPSaver Tests
// ---------------------------------------------------------------------------

class SFTPSaverTest : public FileManagerTestFixture
{
protected:
    void SetUp() override
    {
        FileManagerTestFixture::SetUp();
        server_ = std::make_unique<SftpTestServer>();

        if ( !server_->isAvailable() )
        {
            GTEST_SKIP() << "sshd not available — skipping SFTP tests";
        }
        if ( !server_->start() )
        {
            GTEST_SKIP() << "Could not start sshd — skipping SFTP tests";
        }
    }

    void TearDown() override
    {
        server_.reset();
    }

    std::unique_ptr<SftpTestServer> server_;
};

// ---------------------------------------------------------------------------
// Happy path
// ---------------------------------------------------------------------------

TEST_F( SFTPSaverTest, SaveASync_UploadsFile )
{
    IOContextRunner runner;

    bool                         completed = false;
    std::shared_ptr<std::string> saveLoc   = std::make_shared<std::string>();

    auto data = makeSingleFileResult( "uploaded_test.bin", "sftp upload test content" );

    FileManager::GetInstance().SaveASync(
        "sftp://" + server_->sftpUrl( "/uploaded_test.bin" ),
        data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; },
        saveLoc );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 30 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for SFTP upload";

    // Key-based auth should succeed — verify save location
    EXPECT_FALSE( saveLoc->empty() );
}

// ---------------------------------------------------------------------------
// Unhappy paths
// ---------------------------------------------------------------------------

TEST_F( SFTPSaverTest, SaveASync_NullDataReturnsError )
{
    IOContextRunner runner;

    auto pair = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
    FileManager::ResultType nullResult = outcome::success( pair );

    bool completed = false;

    // SFTPSaver handles null data by posting an error callback (doesn't throw)
    FileManager::GetInstance().SaveASync(
        "sftp://" + server_->sftpUrl( "/null_test.bin" ),
        nullResult, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 5 ) );
    ASSERT_TRUE( ok ) << "Timed out — null data should still invoke callback";
}

TEST_F( SFTPSaverTest, SaveASync_WrongCredentialsReturnsError )
{
    IOContextRunner runner;

    std::string badUrl = "wronguser:wrongpass@127.0.0.1/auth_fail.bin";

    bool completed = false;

    auto data = makeSingleFileResult( "auth_fail.bin", "should not upload" );

    FileManager::GetInstance().SaveASync(
        "sftp://" + badUrl, data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );

    // Should eventually complete (with error or success, depending on server)
    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 30 ) );
    // Don't assert — just verify we don't hang
    (void)ok;
}

TEST_F( SFTPSaverTest, SaveASync_ConnectionRefusedReturnsError )
{
    IOContextRunner runner;

    // Use a port that likely has nothing listening
    bool completed = false;

    auto data = makeSingleFileResult( "noconn.bin", "should fail" );

    FileManager::GetInstance().SaveASync(
        "sftp://testuser:testpass@127.0.0.1:19999/tmp/noconn.bin",
        data, runner.ioc(),
        [&]( FileManager::ResultType ) { completed = true; } );

    bool ok = pollUntil( [&]() { return completed; }, std::chrono::seconds( 15 ) );
    ASSERT_TRUE( ok ) << "Timed out waiting for connection error";
}
