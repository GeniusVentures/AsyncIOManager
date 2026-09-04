#add_subdirectory(application)
add_executable(FileExample FileExample.cpp)

add_dependencies(FileExample AsyncIOManager)
target_link_libraries(FileExample PRIVATE AsyncIOManager spdlog::spdlog protobuf::libprotobuf)
include_directories(../include)
