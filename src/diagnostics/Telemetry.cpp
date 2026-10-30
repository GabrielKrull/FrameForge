#include "diagnostics/Telemetry.h"

#include "diagnostics/Logger.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace frameforge {

namespace {

int64_t QueryFrequency() {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    return freq.QuadPart > 0 ? freq.QuadPart : 1;
}

int64_t QueryNow() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// Minimum JSON string escaping needed for log/device names.
std::string JsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                out += std::format("\\u{:04X}", static_cast<unsigned>(c));
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

const char* FormatName(FrameFormat format) {
    return format == FrameFormat::NV12 ? "NV12" : "BGRA";
}

} // anonymous namespace

Telemetry::Telemetry(int64_t qpcFrequency, std::size_t maxEvents)
    : m_qpcFrequency(qpcFrequency > 0 ? qpcFrequency : QueryFrequency()),
      m_startQpc(QueryNow()),
      m_maxEvents(maxEvents > 0 ? maxEvents : 1),
      m_lastSummaryQpc(QueryNow()) {
}

int64_t Telemetry::NowQpc() const {
    return QueryNow();
}

double Telemetry::SecondsSince(int64_t qpc) const {
    const int64_t delta = QueryNow() - qpc;
    if (delta <= 0) {
        return 0.0;
    }
    return static_cast<double>(delta) / static_cast<double>(m_qpcFrequency);
}

void Telemetry::OnFrameReceived() {
    m_framesReceived.fetch_add(1, std::memory_order_relaxed);
}

void Telemetry::OnFramesDropped(uint64_t count) {
    if (count > 0) {
        m_framesDropped.fetch_add(count, std::memory_order_relaxed);
    }
}

void Telemetry::OnFrameSkipped() {
    m_framesSkipped.fetch_add(1, std::memory_order_relaxed);
}

void Telemetry::OnFramePresented() {
    m_framesPresented.fetch_add(1, std::memory_order_relaxed);
}

void Telemetry::OnFrameRejected() {
    m_framesRejected.fetch_add(1, std::memory_order_relaxed);
}

void Telemetry::SetAudioInfo(const std::string& deviceName, const std::string& formatDesc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasAudio.store(true, std::memory_order_release);
    m_audioDeviceName = deviceName;
    m_audioFormatDesc = formatDesc;
}

void Telemetry::OnAudioPacket(uint32_t frames, bool discontinuity, bool silent) {
    m_audioPackets.fetch_add(1, std::memory_order_relaxed);
    m_audioSampleFrames.fetch_add(frames, std::memory_order_relaxed);
    if (discontinuity) {
        m_audioDiscontinuities.fetch_add(1, std::memory_order_relaxed);
    }
    if (silent) {
        m_audioSilentPackets.fetch_add(1, std::memory_order_relaxed);
    }
}

void Telemetry::SetAVSync(double offsetMs, double driftPpm, bool inSync) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_avOffsetMs = offsetMs;
    m_avDriftPpm = driftPpm;
    m_avInSync = inSync;
}

void Telemetry::SetState(CaptureState state) {
    const CaptureState previous =
        m_state.exchange(state, std::memory_order_acq_rel);
    if (previous == state) {
        return;
    }

    AddEvent("state", std::format("{} -> {}",
        CaptureStateName(previous), CaptureStateName(state)));
}

void Telemetry::SetSessionInfo(TelemetrySessionInfo info) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_session = std::move(info);
}

void Telemetry::OnCaptureError(const std::string& detail) {
    m_captureErrors.fetch_add(1, std::memory_order_relaxed);
    AddEvent("error", detail);
}

void Telemetry::OnSessionOpened() {
    m_successfulOpens.fetch_add(1, std::memory_order_relaxed);
    AddEvent("session-open", "capture session opened");
}

void Telemetry::OnReconnectAttempt(int attempt, int delayMs) {
    m_reconnectAttempts.fetch_add(1, std::memory_order_relaxed);
    AddEvent("reconnect", std::format("attempt {} after {} ms",
        attempt, delayMs));
}

void Telemetry::OnReconnectSucceeded() {
    AddEvent("reconnect-ok", "capture re-established");
}

void Telemetry::OnReconnectExhausted() {
    m_reconnectFailures.fetch_add(1, std::memory_order_relaxed);
    AddEvent("reconnect-exhausted", "retry budget exhausted");
}

void Telemetry::Note(const std::string& message) {
    AddEvent("note", message);
}

void Telemetry::AddEvent(const std::string& kind, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AddEventLocked(kind, message);
}

void Telemetry::AddEventLocked(const std::string& kind,
                               const std::string& message) {
    TelemetryEvent event;
    event.qpc = QueryNow();
    event.kind = kind;
    event.message = message;

    if (m_events.size() >= m_maxEvents) {
        m_events.pop_front();
    }
    m_events.push_back(std::move(event));
}

TelemetrySnapshot Telemetry::Snapshot() const {
    TelemetrySnapshot snapshot;

    snapshot.state = m_state.load(std::memory_order_acquire);

    snapshot.framesReceived = m_framesReceived.load(std::memory_order_relaxed);
    snapshot.framesDropped  = m_framesDropped.load(std::memory_order_relaxed);
    snapshot.framesSkipped  = m_framesSkipped.load(std::memory_order_relaxed);
    snapshot.framesPresented = m_framesPresented.load(std::memory_order_relaxed);
    snapshot.framesRejected = m_framesRejected.load(std::memory_order_relaxed);

    snapshot.captureErrors = m_captureErrors.load(std::memory_order_relaxed);
    snapshot.reconnectAttempts =
        m_reconnectAttempts.load(std::memory_order_relaxed);
    snapshot.reconnectFailures =
        m_reconnectFailures.load(std::memory_order_relaxed);
    snapshot.successfulOpens =
        m_successfulOpens.load(std::memory_order_relaxed);

    const int64_t start = m_startQpc.load(std::memory_order_relaxed);
    const int64_t now = QueryNow();
    const int64_t delta = now > start ? now - start : 0;
    snapshot.uptimeSec = static_cast<double>(delta) /
                         static_cast<double>(m_qpcFrequency);

    if (snapshot.uptimeSec > 0.0) {
        snapshot.effectiveInputFPS =
            static_cast<double>(snapshot.framesReceived) / snapshot.uptimeSec;
        snapshot.effectivePresentedFPS =
            static_cast<double>(snapshot.framesPresented) / snapshot.uptimeSec;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot.session = m_session;
        snapshot.hasAudio = m_hasAudio.load(std::memory_order_relaxed);
        snapshot.audioPackets = m_audioPackets.load(std::memory_order_relaxed);
        snapshot.audioSampleFrames = m_audioSampleFrames.load(std::memory_order_relaxed);
        snapshot.audioDiscontinuities = m_audioDiscontinuities.load(std::memory_order_relaxed);
        snapshot.audioSilentPackets = m_audioSilentPackets.load(std::memory_order_relaxed);
        snapshot.audioDeviceName = m_audioDeviceName;
        snapshot.audioFormat = m_audioFormatDesc;
        snapshot.avOffsetMs = m_avOffsetMs;
        snapshot.avDriftPpm = m_avDriftPpm;
        snapshot.avInSync = m_avInSync;
    }

    return snapshot;
}

std::vector<TelemetryEvent> Telemetry::Events() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<TelemetryEvent>(m_events.begin(), m_events.end());
}

void Telemetry::EmitSummary(Logger& logger) {
    const TelemetrySnapshot snapshot = Snapshot();
    const std::vector<TelemetryEvent> events = Events();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastSummaryQpc = QueryNow();
        m_lastSummaryReceived = snapshot.framesReceived;
        m_lastSummaryPresented = snapshot.framesPresented;
    }

    logger.Info("Telemetry", std::format(
        "state={} device=\"{}\" mode={}x{}@{:.2f} ({}) "
        "received={} dropped={} skipped={} presented={} rejected={} "
        "errors={} reconnects={}/{} opens={} uptime={:.2f}s "
        "input={:.2f}fps presented={:.2f}fps events={}",
        CaptureStateName(snapshot.state),
        snapshot.session.deviceName,
        snapshot.session.width,
        snapshot.session.height,
        snapshot.session.fps,
        FormatName(snapshot.session.format),
        snapshot.framesReceived,
        snapshot.framesDropped,
        snapshot.framesSkipped,
        snapshot.framesPresented,
        snapshot.framesRejected,
        snapshot.captureErrors,
        snapshot.reconnectAttempts,
        snapshot.reconnectFailures,
        snapshot.successfulOpens,
        snapshot.uptimeSec,
        snapshot.effectiveInputFPS,
        snapshot.effectivePresentedFPS,
        events.size()));

    if (snapshot.hasAudio) {
        logger.Info("Telemetry", std::format(
            "audio=\"{}\" format=[{}] packets={} frames={} disc={} silent={} av_offset={:.2f}ms drift={:.1f}ppm in_sync={}",
            snapshot.audioDeviceName, snapshot.audioFormat,
            snapshot.audioPackets, snapshot.audioSampleFrames,
            snapshot.audioDiscontinuities, snapshot.audioSilentPackets,
            snapshot.avOffsetMs, snapshot.avDriftPpm,
            snapshot.avInSync ? "yes" : "no"));
    }
}

bool Telemetry::MaybeEmitSummary(Logger& logger, double intervalSec) {
    if (intervalSec <= 0.0) {
        EmitSummary(logger);
        return true;
    }

    int64_t last = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        last = m_lastSummaryQpc;
    }

    const int64_t now = QueryNow();
    const double elapsed = static_cast<double>(now - last) /
                           static_cast<double>(m_qpcFrequency);
    if (elapsed < intervalSec) {
        return false;
    }

    EmitSummary(logger);
    return true;
}

bool Telemetry::ExportJson(const std::string& path) const {
    if (path.empty()) {
        return false;
    }

    const TelemetrySnapshot snapshot = Snapshot();
    const std::vector<TelemetryEvent> events = Events();
    const int64_t start = m_startQpc.load(std::memory_order_relaxed);

    std::ostringstream out;
    out << "{\n";
    out << "  \"schema\": \"frameforge.telemetry.v1\",\n";
    out << std::format("  \"state\": \"{}\",\n",
        CaptureStateName(snapshot.state));
    out << std::format("  \"uptime_sec\": {:.3f},\n", snapshot.uptimeSec);
    out << "  \"session\": {\n";
    out << std::format("    \"device\": \"{}\",\n",
        JsonEscape(snapshot.session.deviceName));
    out << std::format("    \"symbolic_link\": \"{}\",\n",
        JsonEscape(snapshot.session.symbolicLink));
    out << std::format("    \"media_type_index\": {},\n",
        snapshot.session.mediaTypeIndex);
    out << std::format("    \"width\": {},\n", snapshot.session.width);
    out << std::format("    \"height\": {},\n", snapshot.session.height);
    out << std::format("    \"stride\": {},\n", snapshot.session.stride);
    out << std::format("    \"fps\": {:.3f},\n", snapshot.session.fps);
    out << std::format("    \"format\": \"{}\"\n",
        FormatName(snapshot.session.format));
    out << "  },\n";
    out << "  \"counters\": {\n";
    out << std::format("    \"frames_received\": {},\n",
        snapshot.framesReceived);
    out << std::format("    \"frames_dropped\": {},\n",
        snapshot.framesDropped);
    out << std::format("    \"frames_skipped\": {},\n",
        snapshot.framesSkipped);
    out << std::format("    \"frames_presented\": {},\n",
        snapshot.framesPresented);
    out << std::format("    \"frames_rejected\": {},\n",
        snapshot.framesRejected);
    out << std::format("    \"capture_errors\": {},\n",
        snapshot.captureErrors);
    out << std::format("    \"reconnect_attempts\": {},\n",
        snapshot.reconnectAttempts);
    out << std::format("    \"reconnect_failures\": {},\n",
        snapshot.reconnectFailures);
    out << std::format("    \"successful_opens\": {},\n",
        snapshot.successfulOpens);
    out << std::format("    \"effective_input_fps\": {:.3f},\n",
        snapshot.effectiveInputFPS);
    out << std::format("    \"effective_presented_fps\": {:.3f}\n",
        snapshot.effectivePresentedFPS);
    out << "  },\n";
    out << "  \"audio\": {\n";
    out << std::format("    \"enabled\": {},\n", snapshot.hasAudio ? "true" : "false");
    out << std::format("    \"device\": \"{}\",\n", JsonEscape(snapshot.audioDeviceName));
    out << std::format("    \"format\": \"{}\",\n", JsonEscape(snapshot.audioFormat));
    out << std::format("    \"packets\": {},\n", snapshot.audioPackets);
    out << std::format("    \"sample_frames\": {},\n", snapshot.audioSampleFrames);
    out << std::format("    \"discontinuities\": {},\n", snapshot.audioDiscontinuities);
    out << std::format("    \"silent_packets\": {},\n", snapshot.audioSilentPackets);
    out << std::format("    \"av_offset_ms\": {:.3f},\n", snapshot.avOffsetMs);
    out << std::format("    \"av_drift_ppm\": {:.2f},\n", snapshot.avDriftPpm);
    out << std::format("    \"av_in_sync\": {}\n", snapshot.avInSync ? "true" : "false");
    out << "  },\n";
    out << "  \"events\": [\n";

    for (std::size_t i = 0; i < events.size(); ++i) {
        const double t = static_cast<double>(events[i].qpc - start) /
                         static_cast<double>(m_qpcFrequency);
        out << std::format(
            "    {{\"t_sec\": {:.3f}, \"kind\": \"{}\", \"message\": \"{}\"}}",
            t,
            JsonEscape(events[i].kind),
            JsonEscape(events[i].message));
        if (i + 1 < events.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    const std::string text = out.str();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

void Telemetry::Reset() {
    m_state.store(CaptureState::Disconnected, std::memory_order_release);
    m_framesReceived.store(0);
    m_framesDropped.store(0);
    m_framesSkipped.store(0);
    m_framesPresented.store(0);
    m_framesRejected.store(0);
    m_captureErrors.store(0);
    m_reconnectAttempts.store(0);
    m_reconnectFailures.store(0);
    m_successfulOpens.store(0);
    m_hasAudio.store(false);
    m_audioPackets.store(0);
    m_audioSampleFrames.store(0);
    m_audioDiscontinuities.store(0);
    m_audioSilentPackets.store(0);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.clear();
    m_startQpc.store(QueryNow());
    m_lastSummaryQpc = QueryNow();
    m_lastSummaryReceived = 0;
    m_lastSummaryPresented = 0;
    m_audioDeviceName.clear();
    m_audioFormatDesc.clear();
    m_avOffsetMs = 0.0;
    m_avDriftPpm = 0.0;
    m_avInSync = true;
}

} // namespace frameforge
