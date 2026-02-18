cmake_minimum_required(VERSION 3.15)
project(matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(matching_engine
    src/csv_replay.cpp
    src/execution_backtest.cpp
    src/matching_engine.cpp
    src/order_book.cpp
    src/replay_rows.cpp
)

target_include_directories(matching_engine PUBLIC src)

add_executable(matching_engine_app src/main.cpp)
target_link_libraries(matching_engine_app PRIVATE matching_engine)

add_executable(test_matching tests/test_matching.cpp)
target_link_libraries(test_matching PRIVATE matching_engine)

add_executable(test_csv_replay tests/test_csv_replay.cpp)
target_link_libraries(test_csv_replay PRIVATE matching_engine)
target_compile_definitions(test_csv_replay PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")

add_executable(test_execution_backtest tests/test_execution_backtest.cpp)
target_link_libraries(test_execution_backtest PRIVATE matching_engine)
target_compile_definitions(test_execution_backtest PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")

enable_testing()
add_test(NAME test_matching COMMAND test_matching)
add_test(NAME test_csv_replay COMMAND test_csv_replay)
add_test(NAME test_execution_backtest COMMAND test_execution_backtest)
