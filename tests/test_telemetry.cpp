#include "diagnostics/Logger.h"
#include "diagnostics/Telemetry.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace frameforge;

static int g_total = 0;
static int g_passed = 0;

#define TEST_ASSERT(expr) do { \
    ++g_total; \
    if (expr) { ++g_passed; } \
    else { std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

// ============================================================
// Counters
// ============================================================

void TestCounters() {
    std::printf("TestCounters\n");
    Telemetry telemetry(1'000'000);

    telemetry.OnFrameReceived();
    telemetry.OnFrameReceived();
    telemetry.OnFrameReceived();
    telemetry.OnFramesDropped(2);
    telemetry.OnFramesDropped(0); // ignored
    telemetry.OnFrameSkipped();
    telemetry.OnFramePresented();
    telemetry.OnFrameRejected();
    telemetry.OnSessionOpened();
    telemetry.OnReconnectAttempt(1, 250);
    telemetry.OnReconnectSucceeded();
    telemetry.OnCaptureError("boom");

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.framesReceived == 3);
    TEST_ASSERT(snapshot.framesDropped == 2);
    TEST_ASSERT(snapshot.framesSkipped == 1);
    TEST_ASSERT(snapshot.framesPresented == 1);
    TEST_ASSERT(snapshot.framesRejected == 1);
    TEST_ASSERT(snapshot.successfulOpens == 1);
    TEST_ASSERT(snapshot.reconnectAttempts == 1);
    TEST_ASSERT(snapshot.captureErrors == 1);
    TEST_ASSERT(snapshot.reconnectFailures == 0);
}

void TestThreadSafeCounters() {
    std::printf("TestThreadSafeCounters\n");
    Telemetry telemetry(1'000'000);

    constexpr int kThreads = 4;
    constexpr int kFramesPerThread = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&telemetry] {
            for (int i = 0; i < kFramesPerThread; ++i) {
                telemetry.OnFrameReceived();
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    TEST_ASSERT(telemetry.Snapshot().framesReceived ==
                static_cast<uint64_t>(kThreads * kFramesPerThread));
}

// ============================================================
// Events
// ============================================================

void TestStateEventsOnlyOnChange() {
    std::printf("TestStateEventsOnlyOnChange\n");
    Telemetry telemetry(1'000'000);

    TEST_ASSERT(telemetry.Events().empty());

    telemetry.SetState(CaptureState::Disconnected); // same as initial
    TEST_ASSERT(telemetry.Events().empty());

    telemetry.SetState(CaptureState::Available);
    telemetry.SetState(CaptureState::Available);

    const auto events = telemetry.Events();
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].kind == "state");
    TEST_ASSERT(events[0].message.find("DISCONNECTED -> AVAILABLE") !=
                std::string::npos);
}

void TestBoundedEventLog() {
    std::printf("TestBoundedEventLog\n");
    Telemetry telemetry(1'000'000, 3);

    for (int i = 0; i < 5; ++i) {
        telemetry.AddEvent("test", std::to_string(i));
    }

    const auto events = telemetry.Events();
    TEST_ASSERT(events.size() == 3);
    TEST_ASSERT(events.front().message == "2");
    TEST_ASSERT(events.back().message == "4");
}

void TestReconnectAccounting() {
    std::printf("TestReconnectAccounting\n");
    Telemetry telemetry(1'000'000);

    telemetry.OnReconnectAttempt(1, 100);
    telemetry.OnReconnectAttempt(2, 200);
    telemetry.OnReconnectSucceeded();
    telemetry.OnReconnectExhausted();

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.reconnectAttempts == 2);
    TEST_ASSERT(snapshot.reconnectFailures == 1);

    bool sawSuccess = false;
    bool sawExhausted = false;
    for (const auto& event : telemetry.Events()) {
        if (event.kind == "reconnect-ok") sawSuccess = true;
        if (event.kind == "reconnect-exhausted") sawExhausted = true;
    }
    TEST_ASSERT(sawSuccess);
    TEST_ASSERT(sawExhausted);
}

// ============================================================
// Periodic summary
// ============================================================

void TestSummaryEmission() {
    std::printf("TestSummaryEmission\n");
    Telemetry telemetry(1'000'000);
    telemetry.OnFrameReceived();
    telemetry.SetState(CaptureState::Active);

    std::ostringstream stream;
    Logger logger(stream);

    TEST_ASSERT(telemetry.MaybeEmitSummary(logger, 0.0));
    const std::string output = stream.str();
    TEST_ASSERT(output.find("state=ACTIVE") != std::string::npos);
    TEST_ASSERT(output.find("received=1") != std::string::npos);

    // The same call with a long interval must not emit immediately.
    TEST_ASSERT(!telemetry.MaybeEmitSummary(logger, 3600.0));
}

// ============================================================
// JSON export
// ============================================================

void TestExportJson() {
    std::printf("TestExportJson\n");
    Telemetry telemetry(1'000'000);
    telemetry.OnFrameReceived();
    telemetry.OnFrameReceived();
    telemetry.OnFramesDropped(1);
    telemetry.OnFramePresented();
    telemetry.SetState(CaptureState::Active);

    TelemetrySessionInfo info;
    info.deviceName = "Cam \"A\"";
    info.symbolicLink = "\\\\?\\usb#vid_0000";
    info.width = 1920;
    info.height = 1080;
    info.stride = 1920;
    info.fps = 60.0;
    info.format = FrameFormat::NV12;
    info.mediaTypeIndex = 7;
    telemetry.SetSessionInfo(info);
    telemetry.AddEvent("note", "hello");

    TEST_ASSERT(!telemetry.ExportJson(""));

    const auto path = std::filesystem::temp_directory_path() /
                      "frameforge_telemetry_test.json";

    TEST_ASSERT(telemetry.ExportJson(path.string()));

    std::ifstream file(path, std::ios::in | std::ios::binary);
    TEST_ASSERT(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    file.close();

    TEST_ASSERT(text.find("\"schema\": \"frameforge.telemetry.v1\"") !=
                std::string::npos);
    TEST_ASSERT(text.find("\"state\": \"ACTIVE\"") != std::string::npos);
    TEST_ASSERT(text.find("\"device\": \"Cam \\\"A\\\"\"") !=
                std::string::npos);
    TEST_ASSERT(text.find("\"frames_received\": 2") != std::string::npos);
    TEST_ASSERT(text.find("\"frames_dropped\": 1") != std::string::npos);
    TEST_ASSERT(text.find("\"kind\": \"note\"") != std::string::npos);
    TEST_ASSERT(text.find("\"message\": \"hello\"") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ============================================================
// Reset
// ============================================================

void TestAudioTelemetry() {
    std::printf("TestAudioTelemetry\n");
    Telemetry telemetry(1'000'000);

    telemetry.SetAudioInfo("USB Audio", "2 ch, 48000 Hz, 16 (PCM 16-bit)");
    telemetry.OnAudioPacket(480, false, false);
    telemetry.OnAudioPacket(480, true, false);
    telemetry.OnAudioPacket(480, false, true);
    telemetry.SetAVSync(12.5, 45.0, true);

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.hasAudio == true);
    TEST_ASSERT(snapshot.audioDeviceName == "USB Audio");
    TEST_ASSERT(snapshot.audioFormat == "2 ch, 48000 Hz, 16 (PCM 16-bit)");
    TEST_ASSERT(snapshot.audioPackets == 3);
    TEST_ASSERT(snapshot.audioSampleFrames == 3 * 480);
    TEST_ASSERT(snapshot.audioDiscontinuities == 1);
    TEST_ASSERT(snapshot.audioSilentPackets == 1);
    TEST_ASSERT(snapshot.avOffsetMs == 12.5);
    TEST_ASSERT(snapshot.avDriftPpm == 45.0);
    TEST_ASSERT(snapshot.avInSync == true);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "frameforge_audio_telemetry_test.json";
    TEST_ASSERT(telemetry.ExportJson(path.string()));

    std::ifstream file(path, std::ios::in | std::ios::binary);
    TEST_ASSERT(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    file.close();

    TEST_ASSERT(text.find("\"audio\":") != std::string::npos);
    TEST_ASSERT(text.find("\"enabled\": true") != std::string::npos);
    TEST_ASSERT(text.find("\"packets\": 3") != std::string::npos);
    TEST_ASSERT(text.find("\"discontinuities\": 1") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(path, ec);

    telemetry.Reset();
    const TelemetrySnapshot resetSnap = telemetry.Snapshot();
    TEST_ASSERT(resetSnap.hasAudio == false);
    TEST_ASSERT(resetSnap.audioPackets == 0);
}

void TestReset() {
    std::printf("TestReset\n");
    Telemetry telemetry(1'000'000);

    telemetry.OnFrameReceived();
    telemetry.OnCaptureError("boom");
    telemetry.SetState(CaptureState::Active);

    telemetry.Reset();

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.state == CaptureState::Disconnected);
    TEST_ASSERT(snapshot.framesReceived == 0);
    TEST_ASSERT(snapshot.captureErrors == 0);
    TEST_ASSERT(telemetry.Events().empty());
}

int main() {
    std::printf("=== Telemetry tests ===\n");

    TestCounters();
    TestThreadSafeCounters();
    TestStateEventsOnlyOnChange();
    TestBoundedEventLog();
    TestReconnectAccounting();
    TestSummaryEmission();
    TestExportJson();
    TestAudioTelemetry();
    TestReset();

    std::printf("\n%d/%d assertions passed\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
