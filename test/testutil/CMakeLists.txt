add_library(asiomgr_testutil INTERFACE)
target_include_directories(asiomgr_testutil INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asiomgr_testutil INTERFACE
    AsyncIOManager
    GTest::gtest
    ${Boost_LIBRARIES}
    spdlog::spdlog
)
