// FileParsers.hpp

#pragma once

#include <string>
#include <memory>

class FileParser
{
public:
    virtual ~FileParser() {}

    virtual std::shared_ptr<void> ParseData( std::shared_ptr<void> data )               = 0;
    virtual std::shared_ptr<void> ParseASync( std::shared_ptr<std::vector<char>> data ) = 0;
};
