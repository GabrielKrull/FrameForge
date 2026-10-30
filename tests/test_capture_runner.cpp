#include "capture/CaptureRunner.h"
#include "capture/CaptureSession.h"
#include "diagnostics/Logger.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Telemetry.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>
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
// Scripted capture session used to exercise the recovery loop
// without touching hardware.
// ============================================================

struct FakeDevice {
    std::vector<HRESULT> openResults; // last entry repeats
    std::vector<HRESULT> runResults;  // last entry repeats
    int framesPerRun = 0;

    int opens = 0;
    int runs  = 0;
    int closes = 0;

    HRESULT OpenResultFor(int call) const {
        if (openResults.empty()) return S_OK;
        const std::size_t index = static_cast<std::size_t>(call) <
                                  openResults.size()
            ? static_cast<std::size_t>(call)
            : openResults.size() - 1;
        return openResults[index];
    }

    HRESULT RunResultFor(int call) const {
        if (runResults.empty()) return S_OK;
        const std::size_t index = static_cast<std::size_t>(call) <
                                  runResults.size()
            ? static_cast<std::size_t>(call)
            : runResults.size() - 1;
        return runResults[index];
    }
};

class FakeSession final : public ICaptureSession {
public:
    explicit FakeSession(std::shared_ptr<FakeDevice> device)
        : m_device(std::move(device)) {
        m_info.deviceName   = "Fake Capture Device";
        m_info.symbolicLink = "fake://device";
        m_info.width        = 1280;
        m_info.height       = 720;
        m_info.stride       = 1280;
        m_info.fps          = 60.0;
        m_info.format       = FrameFormat::NV12;
        m_info.mediaTypeIndex = 0;
    }

    HRESULT Open() override {
        const HRESULT hr = m_device->OpenResultFor(m_device->opens);
        ++m_device->opens;
        m_open = SUCCEEDED(hr);
        return hr;
    }

    HRESULT Run(double,
                Metrics&,
                double,
                const FrameSink& onFrame,
                const std::atomic_bool*) override {
        const HRESULT hr = m_device->RunResultFor(m_device->runs);
        ++m_device->runs;

        if (!m_open) {
            return E_NOT_VALID_STATE;
        }

        for (int i = 0; i < m_device->framesPerRun; ++i) {
            if (onFrame) {
                Frame frame;
                frame.format = FrameFormat::NV12;
                frame.width = m_info.width;
                frame.height = m_info.height;
                frame.stride = m_info.stride;
                frame.timestamp = i;
                onFrame(std::move(frame));
            }
        }

        return hr;
    }

    void Close() override {
        if (m_open) {
            ++m_device->closes;
            m_open = false;
        }
    }

    const CaptureSessionInfo& Info() const override { return m_info; }
    bool IsOpen() const override { return m_open; }

private:
    std::shared_ptr<FakeDevice> m_device;
    CaptureSessionInfo m_info;
    bool m_open = false;
};

namespace {

CaptureSessionFactory MakeFactory(std::shared_ptr<FakeDevice> device) {
    return [device]() -> std::unique_ptr<ICaptureSession> {
        return std::make_unique<FakeSession>(device);
    };
}

ReconnectPolicy MakePolicy(int maxAttempts, int baseDelayMs,
                           int maxDelayMs = 2, double multiplier = 1.0) {
    ReconnectPolicy policy;
    policy.maxAttempts = maxAttempts;
    policy.baseDelayMs = baseDelayMs;
    policy.maxDelayMs  = maxDelayMs;
    policy.multiplier  = multiplier;
    return policy;
}

double ElapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

} // anonymous namespace

// ============================================================
// Tests
// ============================================================

void TestHappyPath() {
    std::printf("TestHappyPath\n");
    auto device = std::make_shared<FakeDevice>();
    device->openResults = {S_OK};
    device->runResults  = {S_OK};
    device->framesPerRun = 2;

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(MakeFactory(device), MakePolicy(3, 1), telemetry);

    int frames = 0;
    const HRESULT hr = runner.Run(CaptureRunConfig{5.0, 0.0}, metrics,
        [&frames](Frame&&) { ++frames; }, nullptr);

    TEST_ASSERT(hr == S_OK);
    TEST_ASSERT(frames == 2);
    TEST_ASSERT(device->opens == 1);
    TEST_ASSERT(device->runs == 1);
    TEST_ASSERT(device->closes == 1);

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.successfulOpens == 1);
    TEST_ASSERT(snapshot.captureErrors == 0);
    TEST_ASSERT(snapshot.reconnectAttempts == 0);
    TEST_ASSERT(snapshot.state == CaptureState::Disconnected);
}

void TestRetryThenSuccess() {
    std::printf("TestRetryThenSuccess\n");
    auto device = std::make_shared<FakeDevice>();
    device->openResults = {E_FAIL, S_OK};
    device->runResults  = {S_OK};

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(MakeFactory(device), MakePolicy(3, 1), telemetry);

    const HRESULT hr = runner.Run(CaptureRunConfig{5.0, 0.0}, metrics,
                                  {}, nullptr);

    TEST_ASSERT(hr == S_OK);
    TEST_ASSERT(device->opens == 2);
    TEST_ASSERT(device->runs == 1);
    TEST_ASSERT(device->closes == 1);

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.captureErrors == 1);
    TEST_ASSERT(snapshot.reconnectAttempts == 1);
    TEST_ASSERT(snapshot.reconnectFailures == 0);
    TEST_ASSERT(snapshot.successfulOpens == 1);

    bool sawError = false;
    bool sawReconnect = false;
    bool sawSuccess = false;
    for (const auto& event : telemetry.Events()) {
        if (event.kind == "error") sawError = true;
        if (event.kind == "reconnect") sawReconnect = true;
        if (event.kind == "reconnect-ok") sawSuccess = true;
    }
    TEST_ASSERT(sawError);
    TEST_ASSERT(sawReconnect);
    TEST_ASSERT(sawSuccess);
}

void TestRetryBudgetExhausted() {
    std::printf("TestRetryBudgetExhausted\n");
    auto device = std::make_shared<FakeDevice>();
    device->openResults = {E_FAIL};

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(MakeFactory(device), MakePolicy(2, 1), telemetry);

    const HRESULT hr = runner.Run(CaptureRunConfig{5.0, 0.0}, metrics,
                                  {}, nullptr);

    TEST_ASSERT(hr == E_FAIL);
    TEST_ASSERT(device->opens == 3); // initial attempt + 2 retries
    TEST_ASSERT(device->runs == 0);

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.captureErrors == 3);
    TEST_ASSERT(snapshot.reconnectAttempts == 2);
    TEST_ASSERT(snapshot.reconnectFailures == 1);
    TEST_ASSERT(snapshot.successfulOpens == 0);
    TEST_ASSERT(snapshot.state == CaptureState::Disconnected);
}

void TestRunFailureRecovers() {
    std::printf("TestRunFailureRecovers\n");
    auto device = std::make_shared<FakeDevice>();
    device->openResults = {S_OK, S_OK};
    device->runResults  = {E_FAIL, S_OK};

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(MakeFactory(device), MakePolicy(3, 1), telemetry);

    const HRESULT hr = runner.Run(CaptureRunConfig{5.0, 0.0}, metrics,
                                  {}, nullptr);

    TEST_ASSERT(hr == S_OK);
    TEST_ASSERT(device->opens == 2);
    TEST_ASSERT(device->runs == 2);
    TEST_ASSERT(device->closes == 2);

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.captureErrors == 1);
    TEST_ASSERT(snapshot.reconnectAttempts == 1);
    TEST_ASSERT(snapshot.successfulOpens == 2);
}

void TestFactoryFailure() {
    std::printf("TestFactoryFailure\n");
    CaptureSessionFactory factory = []() -> std::unique_ptr<ICaptureSession> {
        return nullptr;
    };

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(std::move(factory), MakePolicy(1, 1), telemetry);

    const HRESULT hr = runner.Run(CaptureRunConfig{5.0, 0.0}, metrics,
                                  {}, nullptr);

    TEST_ASSERT(hr == E_POINTER);
    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.captureErrors == 2);
    TEST_ASSERT(snapshot.reconnectAttempts == 1);
    TEST_ASSERT(snapshot.reconnectFailures == 1);
}

void TestStopBeforeStart() {
    std::printf("TestStopBeforeStart\n");
    auto device = std::make_shared<FakeDevice>();
    std::atomic_bool stop{true};

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(MakeFactory(device), MakePolicy(3, 1), telemetry);

    const HRESULT hr = runner.Run(CaptureRunConfig{0.0, 0.0}, metrics,
                                  {}, &stop);

    TEST_ASSERT(hr == S_OK);
    TEST_ASSERT(device->opens == 0);
    TEST_ASSERT(device->runs == 0);
}

void TestCancelDuringBackoff() {
    std::printf("TestCancelDuringBackoff\n");
    auto device = std::make_shared<FakeDevice>();
    device->openResults = {E_FAIL};

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(MakeFactory(device), MakePolicy(0, 5000, 5000),
                         telemetry);

    std::atomic_bool stop{false};
    std::thread stopper([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        stop.store(true);
    });

    const auto start = std::chrono::steady_clock::now();
    const HRESULT hr = runner.Run(CaptureRunConfig{0.0, 0.0}, metrics,
                                  {}, &stop);
    const double elapsedMs = ElapsedMs(start);
    stopper.join();

    TEST_ASSERT(hr == S_OK);
    TEST_ASSERT(elapsedMs < 2000.0);
    TEST_ASSERT(device->opens == 1);

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.captureErrors == 1);
    TEST_ASSERT(snapshot.reconnectAttempts == 1);
    TEST_ASSERT(snapshot.reconnectFailures == 0);
    TEST_ASSERT(snapshot.state == CaptureState::Disconnected);
}

void TestDeadlineDuringBackoffIsSuccess() {
    std::printf("TestDeadlineDuringBackoffIsSuccess\n");
    auto device = std::make_shared<FakeDevice>();
    device->openResults = {E_FAIL};

    Telemetry telemetry(1'000'000);
    Metrics metrics(1'000'000);
    CaptureRunner runner(MakeFactory(device), MakePolicy(0, 5000, 5000),
                         telemetry);

    const auto start = std::chrono::steady_clock::now();
    const HRESULT hr = runner.Run(CaptureRunConfig{0.05, 0.0}, metrics,
                                  {}, nullptr);
    const double elapsedMs = ElapsedMs(start);

    TEST_ASSERT(hr == S_OK);
    TEST_ASSERT(elapsedMs < 2000.0);
    TEST_ASSERT(device->opens == 1);

    const TelemetrySnapshot snapshot = telemetry.Snapshot();
    TEST_ASSERT(snapshot.captureErrors == 1);
    TEST_ASSERT(snapshot.reconnectAttempts == 1);
    TEST_ASSERT(snapshot.reconnectFailures == 0);
}

int main() {
    std::printf("=== Capture runner tests ===\n");

    // Tests intentionally exercise failure paths; keep the log quiet.
    Logger::Global().SetLevel(LogLevel::Error);

    TestHappyPath();
    TestRetryThenSuccess();
    TestRetryBudgetExhausted();
    TestRunFailureRecovers();
    TestFactoryFailure();
    TestStopBeforeStart();
    TestCancelDuringBackoff();
    TestDeadlineDuringBackoffIsSuccess();

    std::printf("\n%d/%d assertions passed\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
