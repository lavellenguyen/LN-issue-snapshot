cmake_minimum_required(VERSION 3.14)
project(issue_snapshot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# Library holding all the real logic
add_library(snapshot_lib src/http_client.cpp src/snapshot.cpp)
target_include_directories(snapshot_lib PUBLIC include third_party)
target_link_libraries(snapshot_lib PUBLIC CURL::libcurl SQLite::SQLite3)

# Command-line program
add_executable(issue_snapshot src/main.cpp)
target_link_libraries(issue_snapshot snapshot_lib)

# Tests
enable_testing()
add_executable(run_tests tests/test_snapshot.cpp)
target_link_libraries(run_tests snapshot_lib)
add_test(NAME all_tests COMMAND run_tests)
