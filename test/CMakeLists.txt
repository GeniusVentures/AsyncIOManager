include(../cmake/functions.cmake)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_subdirectory(testutil)
add_subdirectory(src)
