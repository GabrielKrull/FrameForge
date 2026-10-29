#include "capture/DeviceCaptureSession.h"

#include "diagnostics/Logger.h"

#include <algorithm>
#include <format>
#include <utility>

namespace frameforge {

namespace {

// Pick the device that best matches the original selection. The
// symbolic link identifies the physical device even when the
// enumeration order changes after a reconnect.
int FindDevice(const std::vector<DeviceInfo>& devices,
               const DeviceCaptureOptions& options) {
    if (!options.preferredSymbolicLink.empty()) {
        for (const auto& device : devices) {
            if (device.symbolicLink == options.preferredSymbolicLink) {
                return device.index;
            }
        }
    }

    if (!options.preferredName.empty()) {
        for (const auto& device : devices) {
            if (device.friendlyName == options.preferredName) {
                return device.index;
            }
        }
    }

    if (options.deviceIndex >= 0 &&
        options.deviceIndex < static_cast<int>(devices.size())) {
        return options.deviceIndex;
    }

    return -1;
}

} // anonymous namespace

DeviceCaptureSession::DeviceCaptureSession(DeviceCaptureOptions options)
    : m_options(std::move(options)) {
}

DeviceCaptureSession::~DeviceCaptureSession() {
    Close();
}

HRESULT DeviceCaptureSession::Open() {
    Close();

    auto fail = [this](HRESULT hr) {
        if (m_capturer) {
            m_capturer->Close();
            m_capturer.reset();
        }
        m_open = false;
        return hr;
    };

    // Fresh enumeration on every open: this is what makes USB
    // reconnection work without restarting the process.
    DeviceEnumerator enumerator;
    std::vector<DeviceInfo> devices;
    HRESULT hr = enumerator.EnumerateVideo(devices);
    if (FAILED(hr)) {
        Logger::Global().Error("DeviceCaptureSession",
            std::format("Device enumeration failed: {}", HResultToString(hr)));
        return fail(hr);
    }

    if (devices.empty()) {
        Logger::Global().Warn("DeviceCaptureSession",
            "No video capture device is currently connected");
        return fail(HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED));
    }

    const int index = FindDevice(devices, m_options);
    if (index < 0) {
        Logger::Global().Warn("DeviceCaptureSession",
            std::format("Requested device not found (\"{}\")",
                m_options.preferredName));
        return fail(HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED));
    }

    const DeviceInfo& device = devices[static_cast<std::size_t>(index)];
    Logger::Global().Info("DeviceCaptureSession",
        std::format("Opening device [{}] \"{}\"", index,
            device.friendlyName));

    m_capturer = std::make_unique<VideoCapturer>();

    hr = m_capturer->Initialize(device.activate);
    // The media source has been activated; the enumerator's activator
    // objects are no longer needed.
    enumerator.ReleaseActivators();

    if (FAILED(hr)) {
        Logger::Global().Error("DeviceCaptureSession",
            std::format("Failed to initialize capturer: {}",
                HResultToString(hr)));
        return fail(hr);
    }

    std::vector<MediaTypeInfo> types;
    hr = m_capturer->QueryMediaTypes(types);
    if (FAILED(hr) || types.empty()) {
        Logger::Global().Error("DeviceCaptureSession",
            std::format("Failed to query media types: {} ({} type(s))",
                HResultToString(hr), types.size()));
        return fail(FAILED(hr) ? hr : E_FAIL);
    }

    const int requested = m_options.mediaTypeIndex;
    if (requested >= 0 && requested < static_cast<int>(types.size())) {
        hr = m_capturer->SelectMediaType(requested);
        if (FAILED(hr)) {
            Logger::Global().Warn("DeviceCaptureSession",
                std::format("Preferred media type [{}] failed ({}); "
                            "falling back to auto-selection",
                    requested, HResultToString(hr)));
            hr = m_capturer->SelectMediaTypeAuto();
        }
    } else {
        if (requested >= 0) {
            Logger::Global().Warn("DeviceCaptureSession",
                std::format("Preferred media type [{}] is no longer "
                            "available; falling back to auto-selection",
                    requested));
        }
        hr = m_capturer->SelectMediaTypeAuto();
    }

    if (FAILED(hr)) {
        Logger::Global().Error("DeviceCaptureSession",
            std::format("Failed to select media type: {}",
                HResultToString(hr)));
        return fail(hr);
    }

    if (m_options.configurePreviewOutput) {
        hr = m_capturer->ConfigurePreviewOutput();
        if (FAILED(hr)) {
            Logger::Global().Error("DeviceCaptureSession",
                std::format("Failed to configure preview output: {}",
                    HResultToString(hr)));
            return fail(hr);
        }
    }

    hr = m_capturer->OpenCapture();
    if (FAILED(hr)) {
        Logger::Global().Error("DeviceCaptureSession",
            std::format("Failed to open capture: {}",
                HResultToString(hr)));
        return fail(hr);
    }

    m_info.deviceName      = device.friendlyName;
    m_info.symbolicLink    = device.symbolicLink;
    m_info.width           = m_capturer->OutputWidth();
    m_info.height          = m_capturer->OutputHeight();
    m_info.stride          = m_capturer->OutputStride();
    m_info.fps             = m_capturer->OutputFPS();
    m_info.format          = m_capturer->OutputFormat();
    m_info.mediaTypeIndex  = m_capturer->SelectedMediaTypeIndex();

    m_open = true;

    Logger::Global().Info("DeviceCaptureSession", std::format(
        "Session opened: {}x{} stride {} @ {:.2f} FPS ({})",
        m_info.width, m_info.height, m_info.stride, m_info.fps,
        VideoSubtypeToString(m_capturer->OutputSubtype())));

    return S_OK;
}

HRESULT DeviceCaptureSession::Run(
    double durationSec,
    Metrics& metrics,
    double expectedFPS,
    const FrameSink& onFrame,
    const std::atomic_bool* stopFlag) {

    if (!m_capturer || !m_open) {
        return E_NOT_VALID_STATE;
    }

    // resetMetrics=false: frame timestamps accumulate across
    // reconnections so the final report covers the whole run.
    return m_capturer->RunCapture(
        durationSec, metrics, expectedFPS, onFrame, stopFlag, false);
}

void DeviceCaptureSession::Close() {
    if (m_capturer) {
        m_capturer->Close();
        m_capturer.reset();
    }
    m_open = false;
}

} // namespace frameforge
