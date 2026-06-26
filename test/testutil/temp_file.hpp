#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace
{
    inline std::string uniqueSuffix()
    {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::to_string( now );
    }
}

/// @brief RAII temporary file that cleans up on destruction.
class TempFile
{
public:
    TempFile() : TempFile( "" ) {}

    explicit TempFile( const std::string &content )
    {
        path_ = std::filesystem::temp_directory_path() / ("asiomgr_test_" + uniqueSuffix());
        write( content );
    }

    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove( path_, ec );
    }

    TempFile( const TempFile & )            = delete;
    TempFile &operator=( const TempFile & ) = delete;

    const std::filesystem::path &path() const { return path_; }
    std::string                  pathString() const { return path_.string(); }

    void write( const std::string &content )
    {
        std::ofstream ofs( path_, std::ios::binary );
        ofs.write( content.data(), content.size() );
    }

    std::string read() const
    {
        std::ifstream ifs( path_, std::ios::binary );
        return std::string( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    }

private:
    std::filesystem::path path_;
};

/// @brief RAII temporary directory that cleans up on destruction.
class TempDir
{
public:
    TempDir()
    {
        path_ = std::filesystem::temp_directory_path() / ("asiomgr_test_dir_" + uniqueSuffix());
        std::filesystem::create_directories( path_ );
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all( path_, ec );
    }

    TempDir( const TempDir & )            = delete;
    TempDir &operator=( const TempDir & ) = delete;

    const std::filesystem::path &path() const { return path_; }
    std::string                  pathString() const { return path_.string(); }

    /// @brief Write content to a file within this temp directory.
    /// Creates parent directories as needed. Returns the full path.
    std::filesystem::path writeFile( const std::string &relativePath, const std::string &content ) const
    {
        auto fullPath = path_ / relativePath;
        std::filesystem::create_directories( fullPath.parent_path() );
        std::ofstream ofs( fullPath, std::ios::binary );
        ofs.write( content.data(), content.size() );
        return fullPath;
    }

private:
    std::filesystem::path path_;
};
