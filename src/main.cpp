#include "core/common.h"
#include "diagnostics/Logger.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Telemetry.h"
#include "capture/CaptureRunner.h"
#include "capture/DeviceCaptureSession.h"
#include "capture/DeviceEnumerator.h"
#include "capture/ReconnectPolicy.h"
#include "capture/VideoCapturer.h"
#include "capture/VirtualCameraSupport.h"
#include "preview/PreviewController.h"
#include "audio/AudioCapturer.h"
#include "audio/AudioEnumerator.h"
#include "audio/AudioMetrics.h"
#include "audio/AudioRenderer.h"
#include "audio/AVSyncTracker.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <thread>

using namespace frameforge;

namespace {

struct Options {
    int    deviceIndex = 0;
    double durationSec = 5.0;
    bool   listOnly    = false;
    bool   verbose     = false;
    bool   preview     = false;
    int    mediaTypeIndex = -1; // -1 = auto

    // Fase 3: recovery/telemetry
    int    reconnectAttempts = 5;    // 0 = unlimited
    int    reconnectBaseDelayMs = 250;
    int    reconnectMaxDelayMs = 5000;
    std::string telemetryPath;
    std::string logPath;
    std::uintmax_t logMaxBytes = 4u * 1024u * 1024u;
    int    logMaxFiles = 3;

    // Fase 4: audio
    int    audioDeviceIndex = -1; // -1 = auto-select or default
    bool   noAudio          = false;
    bool   audioMonitor     = false;
    bool   virtualCameraCheck = false;
};

void PrintUsage() {
    std::cout <<
        "FrameForge — Video Capture Diagnostics\n"
        "\n"
        "Usage: frameforge [options]\n"
        "  --device <n>            Device index (default: 0)\n"
        "  --duration <sec>        Capture duration in seconds (default: 5.0;\n"
        "                          0 = run until the preview window is closed)\n"
        "  --type <n>              Media type index (default: auto-select)\n"
        "  --preview               Show a native preview window (D3D11)\n"
        "  --virtual-camera-check  Check Windows software virtual camera API support\n"
        "  --list                  List devices and media types, then exit\n"
        "\n"
        "Recovery and diagnostics:\n"
        "  --reconnect <n>         Consecutive reconnect attempts (default: 5;\n"
        "                          0 = unlimited)\n"
        "  --reconnect-delay <ms>  Initial backoff delay (default: 250)\n"
        "  --reconnect-max <ms>    Maximum backoff delay (default: 5000)\n"
        "  --telemetry <path>      Export a JSON telemetry report on exit\n"
        "  --log-file <path>       Also write logs to a file\n"
        "  --log-max-bytes <n>     Log rotation threshold (default: 4194304)\n"
        "  --log-max-files <n>     Rotated log archives kept (default: 3)\n"
        "\n"
        "Audio options (Fase 4):\n"
        "  --audio-device <n>      Audio capture device index (default: auto)\n"
        "  --no-audio              Disable audio capture\n"
        "  --audio-monitor         Play captured audio locally on default render device\n"
        "\n"
        "  --verbose               Show debug-level logs\n"
        "  --help                  Show this help\n";
}

Options ParseArgs(int argc, char* argv[]) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else if (arg == "--device" && i + 1 < argc) {
            opts.deviceIndex = std::atoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            opts.durationSec = std::atof(argv[++i]);
        } else if (arg == "--type" && i + 1 < argc) {
            opts.mediaTypeIndex = std::atoi(argv[++i]);
        } else if (arg == "--list") {
            opts.listOnly = true;
        } else if (arg == "--preview") {
            opts.preview = true;
        } else if (arg == "--virtual-camera-check") {
            opts.virtualCameraCheck = true;
        } else if (arg == "--reconnect" && i + 1 < argc) {
            opts.reconnectAttempts = std::atoi(argv[++i]);
        } else if (arg == "--reconnect-delay" && i + 1 < argc) {
            opts.reconnectBaseDelayMs = std::atoi(argv[++i]);
        } else if (arg == "--reconnect-max" && i + 1 < argc) {
            opts.reconnectMaxDelayMs = std::atoi(argv[++i]);
        } else if (arg == "--telemetry" && i + 1 < argc) {
            opts.telemetryPath = argv[++i];
        } else if (arg == "--log-file" && i + 1 < argc) {
            opts.logPath = argv[++i];
        } else if (arg == "--log-max-bytes" && i + 1 < argc) {
            opts.logMaxBytes = static_cast<std::uintmax_t>(
                std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--log-max-files" && i + 1 < argc) {
            opts.logMaxFiles = std::atoi(argv[++i]);
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--audio-device" && i + 1 < argc) {
            opts.audioDeviceIndex = std::atoi(argv[++i]);
        } else if (arg == "--no-audio") {
            opts.noAudio = true;
        } else if (arg == "--audio-monitor") {
            opts.audioMonitor = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            PrintUsage();
            std::exit(1);
        }
    }
    return opts;
}

void PrintStats(const FrameStats& s) {
    std::cout << std::format(
        "\n"
        "=== Capture Statistics ===\n"
        "  Frames received      : {}\n"
        "  Duration             : {:.3f} s\n"
        "  Effective FPS        : {:.2f}\n"
        "  Interval min         : {:.3f} ms\n"
        "  Interval max         : {:.3f} ms\n"
        "  Interval avg         : {:.3f} ms\n"
        "  Interval stddev      : {:.3f} ms\n"
        "  Estimated dropped    : {}\n",
        s.totalFrames,
        s.durationSec,
        s.effectiveFPS,
        s.intervalMinMs,
        s.intervalMaxMs,
        s.intervalAvgMs,
        s.intervalStdDev,
        s.estimatedDrops);
}

void PrintAudioStats(const AudioStats& s,
                     const AVSyncStats& sync,
                     const std::string& deviceName,
                     const std::string& formatDesc) {
    std::cout << std::format(
        "\n"
        "=== Audio Capture Statistics ===\n"
        "  Device               : {}\n"
        "  Format               : {}\n"
        "  Packets received     : {}\n"
        "  Sample frames        : {}\n"
        "  Duration             : {:.3f} s\n"
        "  Effective rate       : {:.2f} Hz\n"
        "  Discontinuities      : {}\n"
        "  Silent packets       : {}\n"
        "  Interval min         : {:.3f} ms\n"
        "  Interval max         : {:.3f} ms\n"
        "  Interval avg         : {:.3f} ms\n"
        "  Interval stddev      : {:.3f} ms\n"
        "\n"
        "=== Audio/Video Synchronization ===\n"
        "  Current A/V offset   : {:.2f} ms\n"
        "  Average A/V offset   : {:.2f} ms\n"
        "  Offset range         : [{:.2f} ms .. {:.2f} ms]\n"
        "  Estimated clock drift: {:.1f} PPM ({:.2f} ms/min)\n"
        "  In-sync (±50ms)      : {}\n",
        deviceName.empty() ? "<none>" : deviceName,
        formatDesc.empty() ? "<unknown>" : formatDesc,
        s.totalPackets,
        s.totalSampleFrames,
        s.durationSec,
        s.effectiveSampleRate,
        s.discontinuities,
        s.silentPackets,
        s.intervalMinMs,
        s.intervalMaxMs,
        s.intervalAvgMs,
        s.intervalStdDevMs,
        sync.currentOffsetMs,
        sync.avgOffsetMs,
        sync.minOffsetMs, sync.maxOffsetMs,
        sync.driftPpm, sync.driftMsPerMin,
        sync.inSync50ms ? "PASS" : "FAIL");
}

void PrintPreviewStats(const PreviewStats& s) {
    std::cout << std::format(
        "\n"
        "=== Preview Statistics ===\n"
        "  Final state          : {}\n"
        "  Duration             : {:.3f} s\n"
        "  Frames received      : {}\n"
        "  Queue drops          : {}\n"
        "  Skipped (stale)      : {}\n"
        "  Frames presented     : {}\n"
        "  Presented FPS        : {:.2f}\n"
        "  Capture errors       : {}\n"
        "  Reconnect attempts   : {}\n"
        "  Successful opens     : {}\n"
        "  Capture result       : {}\n",
        CaptureStateName(s.state),
        s.durationSec,
        s.received,
        s.queueDropped,
        s.skipped,
        s.presented,
        s.presentedFPS,
        s.captureErrors,
        s.reconnectAttempts,
        s.successfulOpens,
        HResultToString(s.captureResult));
}

void PrintTelemetry(const TelemetrySnapshot& s) {
    std::cout << std::format(
        "\n"
        "=== Telemetry ===\n"
        "  State                : {}\n"
        "  Device               : {}\n"
        "  Mode                 : {}x{} @ {:.2f} FPS ({})\n"
        "  Frames received      : {}\n"
        "  Frames dropped       : {}\n"
        "  Frames skipped       : {}\n"
        "  Frames presented     : {}\n"
        "  Frames rejected      : {}\n"
        "  Capture errors       : {}\n"
        "  Reconnect attempts   : {}\n"
        "  Reconnect failures   : {}\n"
        "  Successful opens     : {}\n"
        "  Input FPS (lifetime) : {:.2f}\n"
        "  Shown FPS (lifetime) : {:.2f}\n"
        "  Uptime               : {:.2f} s\n",
        CaptureStateName(s.state),
        s.session.deviceName.empty() ? "<none>" : s.session.deviceName,
        s.session.width,
        s.session.height,
        s.session.fps,
        s.session.format == FrameFormat::NV12 ? "NV12" : "BGRA",
        s.framesReceived,
        s.framesDropped,
        s.framesSkipped,
        s.framesPresented,
        s.framesRejected,
        s.captureErrors,
        s.reconnectAttempts,
        s.reconnectFailures,
        s.successfulOpens,
        s.effectiveInputFPS,
        s.effectivePresentedFPS,
        s.uptimeSec);
}

// Fit the video size into a sane default window while preserving
// its aspect ratio.
void FitWindowSize(UINT32 sourceWidth, UINT32 sourceHeight,
                   int& windowWidth, int& windowHeight) {
    constexpr double kMaxWidth  = 1280.0;
    constexpr double kMaxHeight = 720.0;

    if (sourceWidth == 0 || sourceHeight == 0) {
        windowWidth  = 1280;
        windowHeight = 720;
        return;
    }

    double scale = 1.0;
    if (sourceWidth > kMaxWidth || sourceHeight > kMaxHeight) {
        scale = std::min(kMaxWidth / sourceWidth, kMaxHeight / sourceHeight);
    }

    windowWidth  = static_cast<int>(sourceWidth * scale);
    windowHeight = static_cast<int>(sourceHeight * scale);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    Options opts = ParseArgs(argc, argv);

    if (opts.verbose) {
        Logger::Global().SetLevel(LogLevel::Debug);
    }

    if (!opts.logPath.empty()) {
        if (!Logger::Global().SetFileOutput(
                opts.logPath, opts.logMaxBytes, opts.logMaxFiles)) {
            std::cerr << "Warning: could not open log file: "
                      << opts.logPath << "\n";
        }
    }

    // Initialize COM and Media Foundation
    ComInitializer com;
    if (!com.IsInitialized()) {
        std::cerr << std::format(
            "COM initialization failed: {}\n", HResultToString(com.GetResult()));
        return 1;
    }

    MFInitializer mf;
    if (!mf.IsInitialized()) {
        std::cerr << std::format(
            "MF initialization failed: {}\n", HResultToString(mf.GetResult()));
        return 1;
    }

    Logger::Global().Info("Main", "COM and Media Foundation initialized");

    if (opts.virtualCameraCheck) {
        const VirtualCameraSupport support = QueryVirtualCameraSupport();
        if (FAILED(support.result)) {
            std::cerr << std::format(
                "Windows virtual camera support query failed: {}\n",
                HResultToString(support.result));
            return 1;
        }
        std::cout << "Windows Media Foundation software virtual camera API: "
                  << (support.supported ? "supported" : "not supported") << "\n"
                  << "This checks platform capability only; it does not create a camera,\n"
                  << "register a media source, or verify OBS compatibility.\n";
        return support.supported ? 0 : 2;
    }

    // Enumerate devices
    DeviceEnumerator enumerator;
    std::vector<DeviceInfo> devices;
    HRESULT hr = enumerator.EnumerateVideo(devices);
    if (FAILED(hr)) {
        std::cerr << std::format(
            "Device enumeration failed: {}\n", HResultToString(hr));
        return 1;
    }

    if (devices.empty()) {
        std::cerr << "No video capture devices found.\n";
        return 1;
    }

    // Print available devices
    std::cout << "\n=== Video Capture Devices ===\n";
    for (const auto& dev : devices) {
        std::cout << std::format("  [{}] {}\n", dev.index, dev.friendlyName);
    }
    std::cout << "\n";

    if (opts.deviceIndex < 0 ||
        opts.deviceIndex >= static_cast<int>(devices.size())) {
        std::cerr << std::format(
            "Device index {} out of range (0..{})\n",
            opts.deviceIndex, static_cast<int>(devices.size()) - 1);
        return 1;
    }

    // Activate selected device (probe only)
    IMFActivate* activate = devices[opts.deviceIndex].activate;
    Logger::Global().Info("Main",
        std::format("Selected device [{}]: \"{}\"",
            opts.deviceIndex, devices[opts.deviceIndex].friendlyName));

    // Create capturer and initialize
    VideoCapturer capturer;
    hr = capturer.Initialize(activate);
    if (FAILED(hr)) {
        std::cerr << std::format(
            "Failed to initialize capturer: {}\n", HResultToString(hr));
        return 1;
    }

    // Query available media types
    std::vector<MediaTypeInfo> types;
    hr = capturer.QueryMediaTypes(types);
    if (FAILED(hr)) {
        std::cerr << std::format(
            "Failed to query media types: {}\n", HResultToString(hr));
        return 1;
    }

    if (types.empty()) {
        std::cerr << "No media types available on this device.\n";
        return 1;
    }

    // Print media types
    std::cout << "=== Available Media Types ===\n";
    for (const auto& t : types) {
        std::cout << std::format(
            "  [{}] {} {}x{} @ {:.2f} FPS\n",
            t.index, t.SubtypeName(), t.width, t.height, t.Fps());
    }
    std::cout << "\n";

    if (opts.listOnly) {
        AudioEnumerator audioEnum;
        std::vector<AudioDeviceInfo> audioDevs;
        if (SUCCEEDED(audioEnum.EnumerateCapture(audioDevs)) && !audioDevs.empty()) {
            std::cout << "=== Audio Capture Devices ===\n";
            for (const auto& adev : audioDevs) {
                std::cout << std::format("  [{}] {}{}\n",
                    adev.index, adev.friendlyName, adev.isDefault ? " [Default]" : "");
                std::cout << std::format("      Native format: {}\n",
                    adev.mixFormat.ToString());
            }
            std::cout << "\n";
        }
        capturer.Close();
        enumerator.ReleaseActivators();
        return 0;
    }

    // Select media type
    if (opts.mediaTypeIndex >= 0) {
        hr = capturer.SelectMediaType(opts.mediaTypeIndex);
    } else {
        hr = capturer.SelectMediaTypeAuto();
    }

    if (FAILED(hr)) {
        std::cerr << std::format(
            "Failed to select media type: {}\n", HResultToString(hr));
        return 1;
    }

    // Request a preview-friendly output layout (NV12, BGRA fallback)
    if (opts.preview) {
        hr = capturer.ConfigurePreviewOutput();
        if (FAILED(hr)) {
            std::cerr << std::format(
                "Failed to configure preview output: {}\n",
                HResultToString(hr));
            capturer.Close();
            enumerator.ReleaseActivators();
            return 1;
        }
    }

    // Expected FPS must come from the type actually selected, not
    // from a hard-coded index.
    const int selectedIndex = capturer.SelectedMediaTypeIndex();
    double expectedFPS = 0.0;
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(types.size())) {
        expectedFPS = types[selectedIndex].Fps();
    }
    if (opts.preview && capturer.OutputFPS() > 0.0) {
        expectedFPS = capturer.OutputFPS();
    }

    const int windowWidthHint = static_cast<int>(capturer.OutputWidth());
    const int windowHeightHint = static_cast<int>(capturer.OutputHeight());

    // The probe has done its job. The real capture session is created
    // and re-created by CaptureRunner (fresh enumeration per attempt).
    capturer.Close();
    enumerator.ReleaseActivators();

    const DeviceInfo& selectedDevice = devices[opts.deviceIndex];

    Telemetry telemetry;

    ReconnectPolicy policy;
    policy.maxAttempts = std::max(0, opts.reconnectAttempts);
    policy.baseDelayMs = std::max(0, opts.reconnectBaseDelayMs);
    policy.maxDelayMs  = std::max(policy.baseDelayMs,
                                  std::max(0, opts.reconnectMaxDelayMs));

    DeviceCaptureOptions sessionOptions;
    sessionOptions.deviceIndex            = opts.deviceIndex;
    sessionOptions.preferredSymbolicLink  = selectedDevice.symbolicLink;
    sessionOptions.preferredName          = selectedDevice.friendlyName;
    sessionOptions.mediaTypeIndex         = selectedIndex;
    sessionOptions.configurePreviewOutput = opts.preview;

    CaptureSessionFactory factory = [sessionOptions]() {
        return std::unique_ptr<ICaptureSession>(
            std::make_unique<DeviceCaptureSession>(sessionOptions));
    };

    CaptureRunner runner(std::move(factory), policy, telemetry);

    auto exportTelemetry = [&]() {
        if (opts.telemetryPath.empty()) {
            return;
        }
        if (telemetry.ExportJson(opts.telemetryPath)) {
            std::cout << "Telemetry report written to: "
                      << opts.telemetryPath << "\n";
        } else {
            std::cerr << "Failed to write telemetry report: "
                      << opts.telemetryPath << "\n";
        }
    };

    // === Audio pipeline setup (Fase 4) ===
    std::unique_ptr<AudioCapturer> audioCapturer;
    std::unique_ptr<AudioRenderer> audioRenderer;
    AudioMetrics audioMetrics(telemetry.QpcFrequency());
    AVSyncTracker avSync(telemetry.QpcFrequency());

    if (!opts.noAudio) {
        AudioEnumerator audioEnum;
        std::vector<AudioDeviceInfo> audioDevs;
        if (SUCCEEDED(audioEnum.EnumerateCapture(audioDevs)) && !audioDevs.empty()) {
            const int aIndex = AudioEnumerator::FindDevice(
                audioDevs, opts.audioDeviceIndex, selectedDevice.friendlyName);
            if (aIndex >= 0 && aIndex < static_cast<int>(audioDevs.size())) {
                const auto& aDev = audioDevs[static_cast<size_t>(aIndex)];
                audioCapturer = std::make_unique<AudioCapturer>();
                hr = audioCapturer->Initialize(aDev.id);
                if (SUCCEEDED(hr)) {
                    telemetry.SetAudioInfo(audioCapturer->DeviceName(),
                                           audioCapturer->Format().ToString());
                    avSync.SetNominalRates(expectedFPS,
                                           audioCapturer->Format().sampleRate);

                    if (opts.audioMonitor) {
                        audioRenderer = std::make_unique<AudioRenderer>();
                        hr = audioRenderer->Initialize(audioCapturer->Format());
                        if (SUCCEEDED(hr)) {
                            audioRenderer->Start();
                        } else {
                            Logger::Global().Warn("Main", "Could not start audio monitor");
                        }
                    }

                    hr = audioCapturer->Start([&](AudioPacket packet) {
                        telemetry.OnAudioPacket(packet.frames,
                                                packet.IsDiscontinuity(),
                                                packet.IsSilent());
                        audioMetrics.RecordPacket(packet,
                            audioCapturer->Format().sampleRate);
                        avSync.RecordAudioPacket(packet.qpcTimestamp,
                                                 packet.frames,
                                                 packet.devicePosition);

                        const AVSyncStats syncStats = avSync.ComputeStats();
                        telemetry.SetAVSync(syncStats.currentOffsetMs,
                                            syncStats.driftPpm,
                                            syncStats.inSync50ms);

                        if (audioRenderer) {
                            audioRenderer->RenderPacket(packet);
                        }
                    });

                    if (FAILED(hr)) {
                        Logger::Global().Warn("Main",
                            std::format("Failed to start audio capturer: {}",
                                HResultToString(hr)));
                        audioCapturer.reset();
                    }
                } else {
                    Logger::Global().Warn("Main",
                        std::format("Failed to initialize audio device: {}",
                            HResultToString(hr)));
                    audioCapturer.reset();
                }
            }
        }
    }

    // === Native preview path ===
    if (opts.preview) {
        int windowWidth = 0, windowHeight = 0;
        FitWindowSize(static_cast<UINT32>(windowWidthHint),
                      static_cast<UINT32>(windowHeightHint),
                      windowWidth, windowHeight);

        PreviewController preview(runner, telemetry, opts.durationSec,
                                  expectedFPS);
        preview.SetFrameObserver([&](const Frame& frame) {
            avSync.RecordVideoFrame(frame.arrivalQpc, frame.timestamp);
        });
        hr = preview.Start(L"FrameForge - Initializing",
                           windowWidth, windowHeight);
        if (FAILED(hr)) {
            std::cerr << std::format(
                "Failed to start preview: {}\n", HResultToString(hr));
            return 1;
        }

        const int exitCode = preview.Run();

        if (audioCapturer) {
            audioCapturer->Stop();
        }
        if (audioRenderer) {
            audioRenderer->Stop();
        }
        const PreviewStats stats = preview.GetStats();
        PrintPreviewStats(stats);
        if (audioCapturer) {
            PrintAudioStats(audioMetrics.ComputeStats(),
                            avSync.ComputeStats(),
                            audioCapturer->DeviceName(),
                            audioCapturer->Format().ToString());
        }
        PrintTelemetry(telemetry.Snapshot());
        exportTelemetry();

        Logger::Global().Info("Main", "Done");

        if (exitCode != 0) {
            return exitCode;
        }
        return FAILED(stats.captureResult) ? 1 : 0;
    }

    if (opts.durationSec <= 0.0) {
        std::cerr << "--duration must be greater than 0 without --preview\n";
        return 1;
    }

    // === Headless diagnostics path ===
    Metrics metrics(telemetry.QpcFrequency());

    // Periodic aggregated telemetry while a long run is in progress.
    // The reporter thread only wakes every few seconds; it is not on
    // the capture path. The final EmitSummary() below always runs.
    std::jthread reporter([&telemetry](std::stop_token token) {
        while (!token.stop_requested()) {
            telemetry.MaybeEmitSummary(Logger::Global(), 5.0);
            for (int i = 0; i < 50 && !token.stop_requested(); ++i) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100));
            }
        }
    });

    hr = runner.Run(CaptureRunConfig{ opts.durationSec, expectedFPS },
                    metrics,
                    [&](Frame frame) {
                        telemetry.OnFrameReceived();
                        avSync.RecordVideoFrame(frame.arrivalQpc, frame.timestamp);
                    },
                    nullptr);
    reporter.request_stop();
    reporter.join();

    if (audioCapturer) {
        audioCapturer->Stop();
    }
    if (audioRenderer) {
        audioRenderer->Stop();
    }
    telemetry.EmitSummary(Logger::Global());

    if (FAILED(hr)) {
        std::cerr << std::format(
            "Capture failed: {}\n", HResultToString(hr));
    }

    FrameStats stats = metrics.ComputeStats();
    PrintStats(stats);
    if (audioCapturer) {
        PrintAudioStats(audioMetrics.ComputeStats(),
                        avSync.ComputeStats(),
                        audioCapturer->DeviceName(),
                        audioCapturer->Format().ToString());
    }
    PrintTelemetry(telemetry.Snapshot());
    exportTelemetry();

    Logger::Global().Info("Main", "Done");
    return FAILED(hr) ? 1 : 0;
}
