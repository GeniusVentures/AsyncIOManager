// FileSaver.hpp

#pragma once

#include <string>
#include <memory>

using namespace std;

class FileSaver {
public:
    using ResultType = outcome::result<std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>>;
    virtual ~FileSaver() {}
    virtual void SaveFile(std::string filename, shared_ptr<void> data) = 0;
    virtual void SaveASync(std::shared_ptr<boost::asio::io_context> ioc, std::function<void(std::shared_ptr<boost::asio::io_context> ioc)> handle_write, std::string filename, ResultType data, std::string suffix) = 0;
};
