#include "core/common.h"
#include "diagnostics/Logger.h"
#include "diagnostics/Metrics.h"
#include "capture/DeviceEnumerator.h"
#include "capture/VideoCapturer.h"

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

using namespace frameforge;

namespace {

struct Options {
    int    deviceIndex = 0;
    double durationSec = 5.0;
    bool   listOnly    = false;
    bool   verbose     = false;
    int    mediaTypeIndex = -1; // -1 = auto
};

void PrintUsage() {
    std::cout <<
        "FrameForge — Video Capture Diagnostics\n"
        "\n"
        "Usage: frameforge [options]\n"
        "  --device <n>       Device index (default: 0)\n"
        "  --duration <sec>   Capture duration in seconds (default: 5.0)\n"
        "  --type <n>         Media type index (default: auto-select)\n"
        "  --list             List devices and media types, then exit\n"
        "  --verbose          Show debug-level logs\n"
        "  --help             Show this help\n";
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
        } else if (arg == "--verbose") {
            opts.verbose = true;
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

} // anonymous namespace

int main(int argc, char* argv[]) {
    Options opts = ParseArgs(argc, argv);

    if (opts.verbose) {
        Logger::Global().SetLevel(LogLevel::Debug);
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

    // Activate selected device
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

    // Open capture
    hr = capturer.OpenCapture();
    if (FAILED(hr)) {
        std::cerr << std::format(
            "Failed to open capture: {}\n", HResultToString(hr));
        return 1;
    }

    // Run capture and collect metrics
    Metrics metrics(capturer.QpcFrequency());
    double expectedFPS = 0.0;
    {
        int selIdx = (opts.mediaTypeIndex >= 0)
            ? opts.mediaTypeIndex : 0;
        // Use the auto-selected type's FPS if available
        if (selIdx >= 0 && selIdx < static_cast<int>(types.size())) {
            expectedFPS = types[selIdx].Fps();
        }
    }

    hr = capturer.RunCapture(opts.durationSec, metrics, expectedFPS);
    if (FAILED(hr)) {
        std::cerr << std::format(
            "Capture failed: {}\n", HResultToString(hr));
        return 1;
    }

    // Print results
    FrameStats stats = metrics.ComputeStats();
    PrintStats(stats);

    capturer.Close();
    enumerator.ReleaseActivators();

    Logger::Global().Info("Main", "Done");
    return 0;
}
