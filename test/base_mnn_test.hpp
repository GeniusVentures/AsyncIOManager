#pragma once

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace test
{
    struct BaseMNNTest : public ::testing::Test
    {
        // not explicit, intentionally
        explicit BaseMNNTest( const fs::path &path );

        ~BaseMNNTest() override;

        /**
       * @brief Delete directory and all containing files
       */
        void clear();

        /**
       * @brief Create testing directory
       */
        void mkdir();

        /**
       * @brief Get test directory path
       * @return path to test directory
       */
        std::string getPathString() const;

        /**
       * @brief Create subdirectory in test directory
       * @param dirname is a new subdirectory name
       * @return full pathname to the new subdirectory
       */
        fs::path createDir( const fs::path &dirname ) const;

        /**
       * @brief create file in test directory
       * @param filename is a name of created file
       * @return full pathname to the new file
       */
        fs::path createFile( const fs::path &filename ) const;

        /**
       * @brief path exists
       * @param entity - file or directory to check
       * @return
       */
        bool exists( const fs::path &entity ) const;

        /**
       * @brief Create and clear directory before tests
       */
        void SetUp() override;

        /**
       * @brief Clear directory after tests
       */
        void TearDown() override;

    protected:
        fs::path base_path;
    };
}
