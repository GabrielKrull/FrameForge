#pragma once

#include "capture/CaptureSession.h"
#include "capture/DeviceEnumerator.h"
#include "capture/VideoCapturer.h"

#include <memory>
#include <string>
#include <vector>

namespace frameforge {

// ============================================================
// How a physical device should be acquired.
//
// We keep the symbolic link (stable across re-enumeration) and the
// friendly name as a fallback. 'mediaTypeIndex' is the index chosen
// during the initial probe; if it is no longer valid after a
// reconnect the session falls back to auto-selection.
// ============================================================
struct DeviceCaptureOptions {
    int         deviceIndex            = 0;
    std::string preferredSymbolicLink;
    std::string preferredName;
    int         mediaTypeIndex         = -1; // -1 = auto
    bool        configurePreviewOutput = false;
};

// ============================================================
// ICaptureSession backed by Media Foundation + VideoCapturer.
// Every Open() re-enumerates devices so a USB reconnect is picked
// up without restarting the process.
// ============================================================
class DeviceCaptureSession final : public ICaptureSession {
public:
    explicit DeviceCaptureSession(DeviceCaptureOptions options);
    ~DeviceCaptureSession() override;

    DeviceCaptureSession(const DeviceCaptureSession&) = delete;
    DeviceCaptureSession& operator=(const DeviceCaptureSession&) = delete;

    HRESULT Open() override;
    HRESULT Run(double durationSec,
                Metrics& metrics,
                double expectedFPS,
                const FrameSink& onFrame,
                const std::atomic_bool* stopFlag) override;
    void Close() override;

    const CaptureSessionInfo& Info() const override { return m_info; }
    bool IsOpen() const override { return m_open; }

private:
    DeviceCaptureOptions m_options;
    std::unique_ptr<VideoCapturer> m_capturer;
    CaptureSessionInfo m_info;
    bool m_open = false;
};

} // namespace frameforge
