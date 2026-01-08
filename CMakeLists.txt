cmake_minimum_required(VERSION 3.15)
project(matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(matching_engine
    src/matching_engine.cpp
    src/order_book.cpp
)

target_include_directories(matching_engine PUBLIC src)

add_executable(matching_engine_app src/main.cpp)
target_link_libraries(matching_engine_app PRIVATE matching_engine)

add_executable(test_matching tests/test_matching.cpp)
target_link_libraries(test_matching PRIVATE matching_engine)

enable_testing()
add_test(NAME test_matching COMMAND test_matching)
