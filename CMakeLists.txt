cmake_minimum_required(VERSION 3.26)
project(transfer-queue CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Seastar as a subdirectory (git submodule)
add_subdirectory(seastar)

# --- Protobuf code generation ---
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/transferqueue.proto)

# --- Common include directories ---
set(TQ_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}           # generated .pb.h
)

# --- Add component CMakeLists.txt files ---
add_subdirectory(cmake/server)
add_subdirectory(cmake/client)
add_subdirectory(cmake/examples)
add_subdirectory(cmake/tests)
