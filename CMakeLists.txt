cmake_minimum_required(VERSION 3.26)
project(transfer-queue CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Seastar as a subdirectory (git submodule)
add_subdirectory(seastar)

# --- Protobuf code generation ---
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/echo.proto)

# --- Main executable ---
add_executable(transfer-queue
    src/main.cc
    src/echo_server.cc
    src/echo_client.cc
    ${PROTO_SRCS}
)

target_include_directories(transfer-queue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # public headers (include/transfer_queue/*.h)
    ${CMAKE_CURRENT_BINARY_DIR}           # generated echo.pb.h
)

target_link_libraries(transfer-queue PRIVATE
    Seastar::seastar
    protobuf::libprotobuf
)
