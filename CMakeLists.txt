cmake_minimum_required(VERSION 3.20)
project(FrameForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX- /utf-8 /permissive-)
endif()

# === Main executable ===
add_executable(frameforge
    src/main.cpp
    src/diagnostics/Logger.cpp
    src/diagnostics/Metrics.cpp
    src/diagnostics/Telemetry.cpp
    src/capture/DeviceEnumerator.cpp
    src/capture/VideoCapturer.cpp
    src/capture/DeviceCaptureSession.cpp
    src/capture/CaptureRunner.cpp
    src/capture/VirtualCameraSupport.cpp
    src/preview/D3D11Renderer.cpp
    src/preview/PreviewWindow.cpp
    src/preview/PreviewController.cpp
    src/audio/AudioEnumerator.cpp
    src/audio/AudioCapturer.cpp
    src/audio/AudioMetrics.cpp
    src/audio/AVSyncTracker.cpp
    src/audio/AudioRenderer.cpp
)

target_include_directories(frameforge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(frameforge PRIVATE
    mf
    mfplat
    mfreadwrite
    mfuuid
    mfsensorgroup
    ole32
    propsys
    d3d11
    dxgi
    d3dcompiler
)

# === Tests ===
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
