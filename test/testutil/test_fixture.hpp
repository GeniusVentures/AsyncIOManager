#pragma once

#include <gtest/gtest.h>
#include "FileManager.hpp"
#include <memory>
#include <string>
#include <vector>

/// @brief Test fixture that initializes FileManager singletons before each test.
/// Provides helper methods for constructing ResultType data.
class FileManagerTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        FileManager::InitializeSingletons();
    }

    /// @brief Build a single-file ResultType for saving/loading tests.
    static FileManager::ResultType makeSingleFileResult( const std::string &filename,
                                                         const std::vector<char> &content )
    {
        auto paths    = std::vector<std::string>{ filename };
        auto contents = std::vector<std::vector<char>>{ content };
        auto pair     = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(
            std::move( paths ), std::move( contents ) );
        return outcome::success( std::move( pair ) );
    }

    /// @brief Build a single-file ResultType from a string.
    static FileManager::ResultType makeSingleFileResult( const std::string &filename, const std::string &content )
    {
        std::vector<char> vec( content.begin(), content.end() );
        return makeSingleFileResult( filename, vec );
    }

    /// @brief Build a multi-file ResultType.
    static FileManager::ResultType makeMultiFileResult(
        const std::vector<std::string>              &filenames,
        const std::vector<std::vector<char>>        &contents )
    {
        auto pair = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>(
            filenames, contents );
        return outcome::success( std::move( pair ) );
    }

    /// @brief Extract the file data from a ResultType (asserts success).
    static const std::pair<std::vector<std::string>, std::vector<std::vector<char>>> &
    unwrapResult( const FileManager::ResultType &result )
    {
        EXPECT_TRUE( result.has_value() );
        EXPECT_NE( result.value(), nullptr );
        return *result.value();
    }
};
