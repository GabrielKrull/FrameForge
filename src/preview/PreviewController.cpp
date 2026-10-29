#include "preview/PreviewController.h"

#include "core/common.h"
#include "diagnostics/Logger.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

namespace frameforge {

PreviewController::PreviewController(
    CaptureRunner& runner,
    Telemetry& telemetry,
    double durationSec,
    double expectedFPS,
    std::size_t queueCapacity)
    : m_runner(runner),
      m_telemetry(telemetry),
      m_durationSec(durationSec),
      m_expectedFPS(expectedFPS),
      m_queue(queueCapacity),
      m_metrics(telemetry.QpcFrequency()) {
}

PreviewController::~PreviewController() {
    Stop();
}

HRESULT PreviewController::Start(
    const wchar_t* title, int clientWidth, int clientHeight) {

    if (m_started) {
        return S_OK;
    }

    const int width  = clientWidth  > 0 ? clientWidth  : 1280;
    const int height = clientHeight > 0 ? clientHeight : 720;

    HRESULT hr = m_window.Create(title, width, height);
    if (FAILED(hr)) {
        return hr;
    }

    m_windowHandle.store(m_window.Handle());

    RECT client{};
    GetClientRect(m_window.Handle(), &client);
    const UINT renderWidth  = static_cast<UINT>(std::max<LONG>(1, client.right));
    const UINT renderHeight = static_cast<UINT>(std::max<LONG>(1, client.bottom));

    hr = m_renderer.Initialize(m_window.Handle(), renderWidth, renderHeight);
    if (FAILED(hr)) {
        Logger::Global().Error("PreviewController",
            std::format("Renderer initialization failed: {}",
                HResultToString(hr)));
        m_windowHandle.store(nullptr);
        m_window.Close();
        return hr;
    }

    m_window.SetResizeCallback([this](UINT newWidth, UINT newHeight) {
        m_renderer.Resize(newWidth, newHeight);
    });

    m_started = true;
    m_captureThread = std::thread(
        &PreviewController::CaptureThreadFunc, this);

    Logger::Global().Info("PreviewController",
        std::format("Preview started (queue capacity {})",
            m_queue.Capacity()));
    return S_OK;
}

void PreviewController::CaptureThreadFunc() {
    ComInitializer com;
    if (!com.IsInitialized()) {
        m_captureResult = com.GetResult();
        m_telemetry.OnCaptureError(std::format(
            "capture thread COM init failed: {}",
            HResultToString(m_captureResult)));
        Logger::Global().Error("PreviewController",
            std::format("Capture thread COM init failed: {}",
                HResultToString(m_captureResult)));
    } else {
        m_captureResult = m_runner.Run(
            CaptureRunConfig{ m_durationSec, m_expectedFPS },
            m_metrics,
            [this](Frame&& frame) {
                m_telemetry.OnFrameReceived();
                const FrameQueue::PushResult result =
                    m_queue.Push(std::move(frame));
                if (result == FrameQueue::PushResult::ReplacedOldest) {
                    m_telemetry.OnFramesDropped();
                } else if (result == FrameQueue::PushResult::Rejected) {
                    m_telemetry.OnFrameRejected();
                }
            },
            &m_stop);
    }

    if (FAILED(m_captureResult)) {
        Logger::Global().Error("PreviewController",
            std::format("Capture loop failed: {}",
                HResultToString(m_captureResult)));
    }

    // Capture ended on its own (duration elapsed, retries exhausted or
    // error): close the window so the UI loop can finish. A
    // user-initiated close simply makes this a no-op.
    HWND hwnd = m_windowHandle.load();
    if (hwnd && IsWindow(hwnd)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

void PreviewController::UpdateWindowTitle(double elapsedSec) {
    const int64_t now = m_telemetry.NowQpc();
    if (m_lastTitleQpc != 0 &&
        m_telemetry.SecondsSince(m_lastTitleQpc) < 0.25) {
        return;
    }
    m_lastTitleQpc = now;

    const TelemetrySnapshot snapshot = m_telemetry.Snapshot();

    const std::string mode = snapshot.session.width > 0
        ? std::format("{}x{} @ {:.1f}", snapshot.session.width,
                      snapshot.session.height, snapshot.session.fps)
        : std::string("negotiating");

    const double shownFPS = elapsedSec > 0.0
        ? static_cast<double>(m_presented) / elapsedSec
        : 0.0;

    const std::string text = std::format(
        "FrameForge - {} | {} | in {} | shown {:.1f} | drop {} | reconnects {}",
        CaptureStateName(snapshot.state),
        mode,
        snapshot.framesReceived,
        shownFPS,
        snapshot.framesDropped + snapshot.framesRejected,
        snapshot.reconnectAttempts);

    if (text != m_lastTitleText) {
        m_lastTitleText = text;
        m_window.SetTitle(Utf8ToWide(text).c_str());
    }
}

int PreviewController::Run() {
    if (!m_started) {
        Logger::Global().Error("PreviewController",
            "Run called before Start");
        return 1;
    }

    const auto uiStart = std::chrono::steady_clock::now();

    int exitCode = 0;

    while (m_window.IsOpen()) {
        if (!m_window.PumpMessages()) {
            break;
        }
        if (!m_window.IsOpen()) {
            break;
        }

        const double elapsedSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - uiStart).count();
        UpdateWindowTitle(elapsedSec);

        Frame frame;
        if (!m_queue.WaitPopFor(frame, std::chrono::milliseconds(10))) {
            continue;
        }

        // Collapse any backlog: only the freshest frame is worth showing.
        Frame newer;
        while (m_queue.TryPop(newer)) {
            ++m_skipped;
            m_telemetry.OnFrameSkipped();
            frame = std::move(newer);
        }

        HRESULT hr = m_renderer.Render(frame);
        if (SUCCEEDED(hr)) {
            hr = m_renderer.Present();
        }

        if (FAILED(hr)) {
            Logger::Global().Error("PreviewController",
                std::format("Preview render failed: {}",
                    HResultToString(hr)));
            exitCode = 1;
            break;
        }

        ++m_presented;
        m_telemetry.OnFramePresented();
    }

    m_duration = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - uiStart).count();

    Stop();

    // Any frame still queued was never shown; count it as discarded so
    // received == presented + skipped + queueDropped holds at exit.
    const std::size_t stillQueued = m_queue.Size();
    if (stillQueued > 0) {
        m_telemetry.OnFramesDropped(static_cast<uint64_t>(stillQueued));
    }
    m_queue.Clear();

    if (m_duration > 0.0) {
        m_presentedFPS = static_cast<double>(m_presented) / m_duration;
    }

    // Final state is shown in the counters/report, not in the title.
    m_telemetry.EmitSummary(Logger::Global());

    return exitCode;
}

void PreviewController::Stop() {
    m_stop.store(true);
    m_queue.Close();

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    if (m_started) {
        m_windowHandle.store(nullptr);
        m_window.Close();
        m_renderer.Shutdown();
        m_started = false;

        Logger::Global().Info("PreviewController", "Preview stopped");
    }
}

PreviewStats PreviewController::GetStats() const {
    const TelemetrySnapshot snapshot = m_telemetry.Snapshot();

    PreviewStats stats;
    stats.received       = m_queue.TotalPushed();
    stats.queueDropped   = m_queue.TotalDropped();
    stats.skipped        = m_skipped;
    stats.presented      = m_presented;
    stats.durationSec    = m_duration;
    stats.presentedFPS   = m_presentedFPS;
    stats.captureResult  = m_captureResult;
    stats.state          = snapshot.state;
    stats.captureErrors  = snapshot.captureErrors;
    stats.reconnectAttempts = snapshot.reconnectAttempts;
    stats.successfulOpens = snapshot.successfulOpens;
    return stats;
}

} // namespace frameforge
