# --- Example executables ---
add_executable(write-example
    ${CMAKE_SOURCE_DIR}/examples/write_example.cc
)

target_include_directories(write-example PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(write-example PRIVATE
    transferqueue-client
    Seastar::seastar
)

add_executable(read-example
    ${CMAKE_SOURCE_DIR}/examples/read_example.cc
)

target_include_directories(read-example PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(read-example PRIVATE
    transferqueue-client
    Seastar::seastar
)

add_executable(status-example
    ${CMAKE_SOURCE_DIR}/examples/status_example.cc
)

target_include_directories(status-example PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(status-example PRIVATE
    transferqueue-client
    Seastar::seastar
)

# --- Streaming Test ---
add_executable(streaming-test
    ${CMAKE_SOURCE_DIR}/tests/test_streaming.cc
)

target_include_directories(streaming-test PRIVATE ${TQ_INCLUDE_DIRS} ${SEASTAR_INCLUDE_DIRS})

target_link_libraries(streaming-test PRIVATE
    transferqueue-client
    Seastar::seastar
)
