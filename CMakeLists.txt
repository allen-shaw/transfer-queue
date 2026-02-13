cmake_minimum_required(VERSION 3.26)
project(transfer-queue CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Seastar as a subdirectory (git submodule)
add_subdirectory(seastar)

# Main executable
add_executable(transfer-queue src/main.cc)
target_link_libraries(transfer-queue PRIVATE Seastar::seastar)
