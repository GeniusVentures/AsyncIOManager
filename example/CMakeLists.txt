#add_subdirectory(application)
FILE(GLOB MNN_LIBS "${MNN_LIBRARY_DIR}/*")
add_executable(MNNExample MNNExample.cpp)

add_dependencies(MNNExample AsyncIOManager)
target_link_libraries(MNNExample PRIVATE AsyncIOManager spdlog::spdlog protobuf::libprotobuf)
include_directories(../include)
