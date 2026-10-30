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

# Test: Capture lifecycle state machine
add_executable(test_capture_state
    test_capture_state.cpp
)
target_include_directories(test_capture_state PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME CaptureState COMMAND test_capture_state)

# Test: Reconnect policy / backoff
add_executable(test_reconnect_policy
    test_reconnect_policy.cpp
)
target_include_directories(test_reconnect_policy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME ReconnectPolicy COMMAND test_reconnect_policy)

# Test: Telemetry counters, events and JSON export
add_executable(test_telemetry
    test_telemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Telemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Logger.cpp
)
target_include_directories(test_telemetry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME Telemetry COMMAND test_telemetry)

# Test: Capture runner recovery loop with a scripted fake session
add_executable(test_capture_runner
    test_capture_runner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/capture/CaptureRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Telemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Logger.cpp
)
target_include_directories(test_capture_runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME CaptureRunner COMMAND test_capture_runner)

# Test: Audio metrics / packet stats
add_executable(test_audio_metrics
    test_audio_metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/AudioMetrics.cpp
)
target_include_directories(test_audio_metrics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME AudioMetrics COMMAND test_audio_metrics)

# Test: Audio/Video synchronization tracker & drift estimation
add_executable(test_av_sync
    test_av_sync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/AVSyncTracker.cpp
)
target_include_directories(test_av_sync PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME AVSync COMMAND test_av_sync)

# Test: Audio device enumeration & format parser
add_executable(test_audio_enumerator
    test_audio_enumerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/AudioEnumerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics/Logger.cpp
)
target_include_directories(test_audio_enumerator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_audio_enumerator PRIVATE
    ole32
    propsys
)
add_test(NAME AudioEnumerator COMMAND test_audio_enumerator)
