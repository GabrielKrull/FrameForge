#pragma once

#include "core/Frame.h"
#include "diagnostics/CaptureState.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace frameforge {

class Logger;

// ============================================================
// One diagnostic event. Events are rare (state changes, errors,
// reconnects); per-frame data is kept in counters only so the
// capture path never writes to disk.
// ============================================================
struct TelemetryEvent {
    int64_t     qpc = 0;
    std::string kind;
    std::string message;
};

// ============================================================
// Negociated session description captured at reconnect time.
// ============================================================
struct TelemetrySessionInfo {
    std::string deviceName;
    std::string symbolicLink;
    uint32_t    width          = 0;
    uint32_t    height         = 0;
    uint32_t    stride         = 0;
    double      fps            = 0.0;
    FrameFormat format         = FrameFormat::NV12;
    int         mediaTypeIndex = -1;
};

struct TelemetrySnapshot {
    CaptureState state = CaptureState::Disconnected;

    TelemetrySessionInfo session;

    uint64_t framesReceived     = 0;
    uint64_t framesDropped      = 0;
    uint64_t framesSkipped      = 0;
    uint64_t framesPresented    = 0;
    uint64_t framesRejected     = 0;

    uint64_t captureErrors      = 0;
    uint64_t reconnectAttempts  = 0;
    uint64_t reconnectFailures  = 0;
    uint64_t successfulOpens    = 0;

    double   uptimeSec          = 0.0;
    double   effectiveInputFPS  = 0.0;
    double   effectivePresentedFPS = 0.0;

    // Audio telemetry (Fase 4)
    bool        hasAudio           = false;
    std::string audioDeviceName;
    std::string audioFormat;
    uint64_t    audioPackets       = 0;
    uint64_t    audioSampleFrames  = 0;
    uint64_t    audioDiscontinuities = 0;
    uint64_t    audioSilentPackets = 0;
    double      avOffsetMs         = 0.0;
    double      avDriftPpm         = 0.0;
    bool        avInSync           = true;
};

// ============================================================
// Lock-free-on-the-hot-path counters plus a bounded event log.
//
// All timestamps use QPC (monotonic). Counters accumulate across
// reconnects; they are never reset implicitly.
// ============================================================
class Telemetry {
public:
    // qpcFrequency == 0 queries the system frequency.
    explicit Telemetry(int64_t qpcFrequency = 0,
                       std::size_t maxEvents = 128);

    Telemetry(const Telemetry&) = delete;
    Telemetry& operator=(const Telemetry&) = delete;

    int64_t QpcFrequency() const { return m_qpcFrequency; }
    int64_t NowQpc() const;
    double SecondsSince(int64_t qpc) const;

    // --- Hot-path counters ---------------------------------
    void OnFrameReceived();
    void OnFramesDropped(uint64_t count = 1);
    void OnFrameSkipped();
    void OnFramePresented();
    void OnFrameRejected();

    // --- Audio events (Fase 4) -----------------------------
    void SetAudioInfo(const std::string& deviceName, const std::string& formatDesc);
    void OnAudioPacket(uint32_t frames, bool discontinuity, bool silent);
    void SetAVSync(double offsetMs, double driftPpm, bool inSync);

    // --- Session / recovery events --------------------------
    void SetState(CaptureState state);
    void SetSessionInfo(TelemetrySessionInfo info);
    void OnCaptureError(const std::string& detail);
    void OnSessionOpened();
    void OnReconnectAttempt(int attempt, int delayMs);
    void OnReconnectSucceeded();
    void OnReconnectExhausted();
    void Note(const std::string& message);
    void AddEvent(const std::string& kind, const std::string& message);

    // --- Observation ---------------------------------------
    TelemetrySnapshot Snapshot() const;
    std::vector<TelemetryEvent> Events() const;

    // Aggregated, human-readable summary. MaybeEmitSummary()
    // is cheap enough to call from the UI loop; it only writes
    // when 'intervalSec' has elapsed.
    void EmitSummary(Logger& logger);
    bool MaybeEmitSummary(Logger& logger, double intervalSec = 5.0);

    // Export a JSON report. Intended for explicit user action or
    // shutdown, never for the per-frame path.
    bool ExportJson(const std::string& path) const;

    // Test/diagnostic helper: clears counters, events and state.
    void Reset();

private:
    void AddEventLocked(const std::string& kind, const std::string& message);

    int64_t     m_qpcFrequency;
    std::atomic<int64_t> m_startQpc;
    std::size_t m_maxEvents;

    std::atomic<CaptureState> m_state{ CaptureState::Disconnected };

    std::atomic<uint64_t> m_framesReceived{ 0 };
    std::atomic<uint64_t> m_framesDropped{ 0 };
    std::atomic<uint64_t> m_framesSkipped{ 0 };
    std::atomic<uint64_t> m_framesPresented{ 0 };
    std::atomic<uint64_t> m_framesRejected{ 0 };

    std::atomic<uint64_t> m_captureErrors{ 0 };
    std::atomic<uint64_t> m_reconnectAttempts{ 0 };
    std::atomic<uint64_t> m_reconnectFailures{ 0 };
    std::atomic<uint64_t> m_successfulOpens{ 0 };

    // Audio atomic counters (Fase 4)
    std::atomic<bool>     m_hasAudio{ false };
    std::atomic<uint64_t> m_audioPackets{ 0 };
    std::atomic<uint64_t> m_audioSampleFrames{ 0 };
    std::atomic<uint64_t> m_audioDiscontinuities{ 0 };
    std::atomic<uint64_t> m_audioSilentPackets{ 0 };


    std::string m_audioDeviceName;
    std::string m_audioFormatDesc;
    double m_avOffsetMs = 0.0;
    double m_avDriftPpm = 0.0;
    bool   m_avInSync = true;

    mutable std::mutex m_mutex;
    TelemetrySessionInfo m_session;
    std::deque<TelemetryEvent> m_events;
    int64_t m_lastSummaryQpc = 0;
    uint64_t m_lastSummaryReceived = 0;
    uint64_t m_lastSummaryPresented = 0;
};

} // namespace frameforge
