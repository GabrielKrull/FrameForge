cmake_minimum_required(VERSION 3.20)

# Test: Metrics
add_executable(test_metrics
    test_metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Metrics.cpp
)
target_include_directories(test_metrics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME Metrics COMMAND test_metrics)

# Test: Logger
add_executable(test_logger
    test_logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Logger.cpp
)
target_include_directories(test_logger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME Logger COMMAND test_logger)

# Test: Bounded queue / frame hand-off
add_executable(test_frame_queue
    test_frame_queue.cpp
)
target_include_directories(test_frame_queue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME FrameQueue COMMAND test_frame_queue)
