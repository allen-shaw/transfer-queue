# --- TransferQueue Server ---
add_executable(transferqueue-server
    ${CMAKE_SOURCE_DIR}/src/server/main.cc
    ${CMAKE_SOURCE_DIR}/src/server/buffer_shard.cc
    ${CMAKE_SOURCE_DIR}/src/server/buffer_manager.cc
    ${CMAKE_SOURCE_DIR}/src/server/http_handler.cc
    ${CMAKE_SOURCE_DIR}/src/server/rpc_handler.cc
    ${CMAKE_SOURCE_DIR}/src/server/dedup_filter.cc
    ${CMAKE_SOURCE_DIR}/src/server/metrics.cc
    ${CMAKE_SOURCE_DIR}/src/server/disk_storage.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(transferqueue-server PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(transferqueue-server PRIVATE
    Seastar::seastar
    protobuf::libprotobuf
)
