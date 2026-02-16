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

# --- TransferQueue Server ---
add_executable(transferqueue-server
    src/server/main.cc
    src/server/buffer_shard.cc
    src/server/buffer_manager.cc
    src/server/http_handler.cc
    src/server/rpc_handler.cc
    src/server/dedup_filter.cc
    src/server/metrics.cc
    src/server/disk_storage.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(transferqueue-server PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(transferqueue-server PRIVATE
    Seastar::seastar
    protobuf::libprotobuf
)

# --- TransferQueue Client ---
add_library(transferqueue-client
    src/client/client.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(transferqueue-client PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(transferqueue-client PRIVATE
    Seastar::seastar
    protobuf::libprotobuf
)

# --- Example executables ---
add_executable(write-example
    examples/write_example.cc
)

target_include_directories(write-example PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(write-example PRIVATE
    transferqueue-client
    Seastar::seastar
)

add_executable(read-example
    examples/read_example.cc
)

target_include_directories(read-example PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(read-example PRIVATE
    transferqueue-client
    Seastar::seastar
)

add_executable(status-example
    examples/status_example.cc
)

target_include_directories(status-example PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(status-example PRIVATE
    transferqueue-client
    Seastar::seastar
)

# --- Streaming Test ---
add_executable(streaming-test
    tests/test_streaming.cc
)

target_include_directories(streaming-test PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(streaming-test PRIVATE
    transferqueue-client
    Seastar::seastar
)

# --- Tests ---
enable_testing()
find_package(GTest REQUIRED)

add_executable(test-dedup-filter
    tests/test_dedup_filter.cc
    src/server/dedup_filter.cc
)

target_include_directories(test-dedup-filter PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(test-dedup-filter PRIVATE
    GTest::gtest_main
    protobuf::libprotobuf
)

add_test(NAME DedupFilter COMMAND test-dedup-filter)

add_executable(test-buffer-shard
    tests/test_buffer_shard.cc
    src/server/buffer_shard.cc
    src/server/dedup_filter.cc
    src/server/disk_storage.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(test-buffer-shard PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(test-buffer-shard PRIVATE
    GTest::gtest_main
    Seastar::seastar
    protobuf::libprotobuf
)

add_test(NAME BufferShard COMMAND test-buffer-shard)

add_executable(test-buffer-manager
    tests/test_buffer_manager.cc
    src/server/buffer_manager.cc
    src/server/buffer_shard.cc
    src/server/dedup_filter.cc
    src/server/disk_storage.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(test-buffer-manager PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(test-buffer-manager PRIVATE
    GTest::gtest
    Seastar::seastar
    protobuf::libprotobuf
)


add_test(NAME BufferManager COMMAND test-buffer-manager)

add_executable(test-disk-storage
    tests/test_disk_storage.cc
    src/server/disk_storage.cc
    ${PROTO_SRCS}
)

target_include_directories(test-disk-storage PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(test-disk-storage PRIVATE
    GTest::gtest
    Seastar::seastar
    protobuf::libprotobuf
)

add_test(NAME DiskStorage COMMAND test-disk-storage)

add_executable(test-buffer-shard-spill
    tests/test_buffer_shard_spill.cc
    src/server/buffer_shard.cc
    src/server/disk_storage.cc
    src/server/dedup_filter.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(test-buffer-shard-spill PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(test-buffer-shard-spill PRIVATE
    GTest::gtest
    Seastar::seastar
    protobuf::libprotobuf
)

add_test(NAME BufferShardSpill COMMAND test-buffer-shard-spill)
