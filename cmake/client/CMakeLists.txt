# --- TransferQueue Client ---
add_library(transferqueue-client
    ${CMAKE_SOURCE_DIR}/src/client/client.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(transferqueue-client PRIVATE ${TQ_INCLUDE_DIRS})

target_link_libraries(transferqueue-client PRIVATE
    Seastar::seastar
    protobuf::libprotobuf
)
