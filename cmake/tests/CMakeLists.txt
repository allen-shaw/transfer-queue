# --- Tests ---
enable_testing()
find_package(GTest REQUIRED)

add_executable(test-dedup-filter
    ${CMAKE_SOURCE_DIR}/tests/test_dedup_filter.cc
    ${CMAKE_SOURCE_DIR}/src/server/dedup_filter.cc
)

target_include_directories(test-dedup-filter PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(test-dedup-filter PRIVATE
    GTest::gtest_main
    protobuf::libprotobuf
)

add_test(NAME DedupFilter COMMAND test-dedup-filter)

add_executable(test-buffer-shard
    ${CMAKE_SOURCE_DIR}/tests/test_buffer_shard.cc
    ${CMAKE_SOURCE_DIR}/src/server/buffer_shard.cc
    ${CMAKE_SOURCE_DIR}/src/server/dedup_filter.cc
    ${CMAKE_SOURCE_DIR}/src/server/disk_storage.cc
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
    ${CMAKE_SOURCE_DIR}/tests/test_buffer_manager.cc
    ${CMAKE_SOURCE_DIR}/src/server/buffer_manager.cc
    ${CMAKE_SOURCE_DIR}/src/server/buffer_shard.cc
    ${CMAKE_SOURCE_DIR}/src/server/dedup_filter.cc
    ${CMAKE_SOURCE_DIR}/src/server/disk_storage.cc
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
    ${CMAKE_SOURCE_DIR}/tests/test_disk_storage.cc
    ${CMAKE_SOURCE_DIR}/src/server/disk_storage.cc
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
    ${CMAKE_SOURCE_DIR}/tests/test_buffer_shard_spill.cc
    ${CMAKE_SOURCE_DIR}/src/server/buffer_shard.cc
    ${CMAKE_SOURCE_DIR}/src/server/disk_storage.cc
    ${CMAKE_SOURCE_DIR}/src/server/dedup_filter.cc
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
