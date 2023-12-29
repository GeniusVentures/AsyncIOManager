// FileSaver.hpp

#ifndef FILESAVER_HPP
#define FILESAVER_HPP

#include <string>
#include <memory>

using namespace std;

class FileSaver {
public:
    virtual ~FileSaver() {}
    virtual void SaveFile(std::string filename, shared_ptr<void> data) = 0;
    virtual void SaveASync(std::shared_ptr<boost::asio::io_context> ioc, std::function<void(std::shared_ptr<boost::asio::io_context> ioc)> handle_write, std::string filename, std::shared_ptr<std::vector<char>> data) = 0;
};

#endif

