#pragma once

#include "capture/CaptureRunner.h"
#include "diagnostics/CaptureState.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Telemetry.h"
#include "preview/D3D11Renderer.h"
#include "preview/FrameQueue.h"
#include "preview/PreviewWindow.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace frameforge {

// ============================================================
// Snapshot of the preview pipeline counters.
//
// Every captured frame is accounted for exactly once:
//   received      = frames handed to the queue by the capture thread
//   queueDropped  = frames discarded by the bounded queue (drop-oldest)
//                   plus frames still queued when the preview exits
//   skipped       = frames popped but replaced by a newer frame
//                   before they could be rendered
//   presented     = frames actually rendered and presented
//
// Invariant at exit: received == presented + skipped + queueDropped.
// ============================================================
struct PreviewStats {
    uint64_t received      = 0;
    uint64_t queueDropped  = 0;
    uint64_t skipped       = 0;
    uint64_t presented     = 0;
    double   durationSec   = 0.0;
    double   presentedFPS  = 0.0;
    HRESULT  captureResult = S_OK;

    // Recovery/telemetry view
    CaptureState state            = CaptureState::Disconnected;
    uint64_t     captureErrors    = 0;
    uint64_t     reconnectAttempts = 0;
    uint64_t     successfulOpens  = 0;
};

// ============================================================
// Owns the preview pipeline:
//   CaptureRunner -> bounded queue -> UI/render thread
//
// The capture thread only copies samples and enqueues them; all
// windowing and GPU work happens on the thread that calls Run().
// The window title shows the real capture state and counters, so
// a reconnect (or a dead device) is never displayed as "active".
// ============================================================
class PreviewController {
public:
    PreviewController(CaptureRunner& runner,
                      Telemetry& telemetry,
                      double durationSec,
                      double expectedFPS,
                      std::size_t queueCapacity = 3);

    PreviewController(const PreviewController&) = delete;
    PreviewController& operator=(const PreviewController&) = delete;

    ~PreviewController();

    // Create the window and renderer, then start the capture thread.
    HRESULT Start(const wchar_t* title, int clientWidth, int clientHeight);

    // Pump window messages and render frames until the window closes
    // or the capture duration elapses. Always stops the capture thread.
    // Returns a process exit code (0 = normal).
    int Run();

    // Stop capture and tear down graphics/window resources. Idempotent.
    void Stop();

    PreviewStats GetStats() const;

    // Optional observer tapped on each frame received by the capture thread
    void SetFrameObserver(std::function<void(const Frame&)> observer) {
        m_frameObserver = std::move(observer);
    }

private:
    void CaptureThreadFunc();
    void UpdateWindowTitle(double elapsedSec);

    CaptureRunner& m_runner;
    Telemetry&     m_telemetry;
    double         m_durationSec;
    double         m_expectedFPS;

    FrameQueue     m_queue;
    Metrics        m_metrics;

    PreviewWindow  m_window;
    D3D11Renderer  m_renderer;

    std::atomic_bool m_stop{ false };
    std::atomic<HWND> m_windowHandle{ nullptr };
    std::thread      m_captureThread;
    bool             m_started = false;

    // UI-thread counters
    uint64_t m_skipped   = 0;
    uint64_t m_presented = 0;
    double   m_duration  = 0.0;
    double   m_presentedFPS = 0.0;

    int64_t     m_lastTitleQpc = 0;
    std::string m_lastTitleText;

    // Capture-thread result (read after join)
    HRESULT m_captureResult = S_OK;
    std::function<void(const Frame&)> m_frameObserver;
};

} // namespace frameforge
