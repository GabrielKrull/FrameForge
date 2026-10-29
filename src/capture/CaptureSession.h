#pragma once

#include "core/Frame.h"
#include "core/common.h"
#include "diagnostics/Metrics.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace frameforge {

// Capture-side frame callback. Matches VideoCapturer::FrameCallback.
using FrameSink = std::function<void(Frame&&)>;

// Description of the mode actually negotiated when the session opened.
struct CaptureSessionInfo {
    std::string deviceName;
    std::string symbolicLink;
    uint32_t    width          = 0;
    uint32_t    height         = 0;
    uint32_t    stride         = 0;
    double      fps            = 0.0;
    FrameFormat format         = FrameFormat::NV12;
    int         mediaTypeIndex = -1;
};

// ============================================================
// Minimal capture session interface.
//
// A session owns exactly one source reader (or equivalent). The
// recovery loop creates a brand new session for every attempt, so
// a broken session is never reused after a failure. Implementations
// must be safe to destroy without a prior Close().
// ============================================================
class ICaptureSession {
public:
    virtual ~ICaptureSession() = default;

    // Acquire the device, negotiate the mode and get ready to read.
    virtual HRESULT Open() = 0;

    // Synchronous capture loop. durationSec <= 0 runs until stopFlag.
    virtual HRESULT Run(double durationSec,
                        Metrics& metrics,
                        double expectedFPS,
                        const FrameSink& onFrame,
                        const std::atomic_bool* stopFlag) = 0;

    // Release every device/graphics resource. Idempotent.
    virtual void Close() = 0;

    // Valid after a successful Open(); retained after Close() for
    // diagnostics.
    virtual const CaptureSessionInfo& Info() const = 0;

    virtual bool IsOpen() const = 0;
};

using CaptureSessionFactory =
    std::function<std::unique_ptr<ICaptureSession>()>;

} // namespace frameforge
