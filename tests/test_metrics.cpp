#include "diagnostics/Metrics.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace frameforge;

static int g_total = 0;
static int g_passed = 0;

#define TEST_ASSERT(expr) do { \
    ++g_total; \
    if (expr) { ++g_passed; } \
    else { std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

#define TEST_ASSERT_NEAR(a, b, tol) do { \
    ++g_total; \
    if (std::fabs((a) - (b)) <= (tol)) { ++g_passed; } \
    else { \
        std::printf("  FAIL: %s ~= %s (line %d)  got=%.6f expected=%.6f\n", \
            #a, #b, __LINE__, (double)(a), (double)(b)); \
    } \
} while(0)

// ============================================================
// Synthetic QPC tests
// ============================================================

void TestEmptyMetrics() {
    std::printf("TestEmptyMetrics\n");
    Metrics m(1'000'000); // 1 MHz QPC
    auto s = m.ComputeStats();
    TEST_ASSERT(s.totalFrames == 0);
    TEST_ASSERT_NEAR(s.effectiveFPS, 0.0, 1e-9);
    TEST_ASSERT_NEAR(s.durationSec, 0.0, 1e-9);
}

void TestSingleFrame() {
    std::printf("TestSingleFrame\n");
    Metrics m(1'000'000);
    m.RecordFrame(100);
    auto s = m.ComputeStats();
    TEST_ASSERT(s.totalFrames == 1);
    TEST_ASSERT_NEAR(s.effectiveFPS, 0.0, 1e-9);
}

void TestSteady30FPS() {
    std::printf("TestSteady30FPS\n");
    // 1 MHz QPC -> 1 tick = 1 us. 30 FPS -> interval = 33333.33 us.
    const int64_t freq = 1'000'000;
    const int64_t interval = freq / 30; // 33333 ticks

    Metrics m(freq);
    const int frames = 30;
    for (int i = 0; i < frames; ++i) {
        m.RecordFrame(i * interval);
    }

    auto s = m.ComputeStats();
    TEST_ASSERT(s.totalFrames == static_cast<uint64_t>(frames));
    TEST_ASSERT_NEAR(s.effectiveFPS, 30.0, 0.01);
    TEST_ASSERT_NEAR(s.intervalAvgMs, 33.333, 0.01);
    TEST_ASSERT_NEAR(s.intervalMinMs, 33.333, 0.01);
    TEST_ASSERT_NEAR(s.intervalMaxMs, 33.333, 0.01);
    TEST_ASSERT_NEAR(s.intervalStdDev, 0.0, 0.01);
    TEST_ASSERT(s.estimatedDrops == 0);
}

void TestSteady60FPS() {
    std::printf("TestSteady60FPS\n");
    const int64_t freq = 1'000'000;
    // Use 1000000/60 which is not integer; use rounding
    const int64_t interval = 1'000'000 / 60; // 16666 ticks

    Metrics m(freq);
    const int frames = 60;
    for (int i = 0; i < frames; ++i) {
        m.RecordFrame(i * interval);
    }

    auto s = m.ComputeStats();
    TEST_ASSERT(s.totalFrames == static_cast<uint64_t>(frames));
    // Effective FPS: 59 intervals over duration of 59*16666 ticks
    // = 59 / (59*16666/1e6) = 1e6/16666 ~ 60.0024
    TEST_ASSERT_NEAR(s.effectiveFPS, 60.0, 0.1);
    TEST_ASSERT(s.estimatedDrops == 0);
}

void TestDroppedFrames() {
    std::printf("TestDroppedFrames\n");
    const int64_t freq = 1'000'000;
    const int64_t interval = freq / 30; // 33333

    Metrics m(freq);
    m.SetExpectedFPS(30.0);

    // 10 frames, but with a gap of 5 intervals after frame 5
    // Frames 0-4: normal, then skip 5 intervals (drop ~4 frames), then frames 5-9
    int64_t ts = 0;
    for (int i = 0; i < 10; ++i) {
        m.RecordFrame(ts);
        if (i == 4) {
            // Simulate 5 missing frames (gap of 5 intervals)
            ts += interval * 6; // skip ahead
        } else {
            ts += interval;
        }
    }

    auto s = m.ComputeStats();
    TEST_ASSERT(s.totalFrames == 10);
    // One big gap of 6 intervals -> estimated drops = 6-1 = 5
    TEST_ASSERT(s.estimatedDrops >= 3);
    TEST_ASSERT(s.estimatedDrops <= 6);
    // Interval max should be ~6*33.33 = 200ms
    TEST_ASSERT_NEAR(s.intervalMaxMs, 200.0, 5.0);
}

void TestJitteryIntervals() {
    std::printf("TestJitteryIntervals\n");
    const int64_t freq = 1'000'000;
    const int64_t base = freq / 30; // 33333

    Metrics m(freq);
    // Alternate between +20% and -20% jitter
    int64_t ts = 0;
    for (int i = 0; i < 20; ++i) {
        m.RecordFrame(ts);
        int64_t jitter = (i % 2 == 0) ? (base * 12 / 10) : (base * 8 / 10);
        ts += jitter;
    }

    auto s = m.ComputeStats();
    TEST_ASSERT(s.totalFrames == 20);
    // StdDev should be > 0 (jittery)
    TEST_ASSERT(s.intervalStdDev > 1.0);
    // Min < Max
    TEST_ASSERT(s.intervalMinMs < s.intervalMaxMs);
    // Avg around 33.33ms
    TEST_ASSERT_NEAR(s.intervalAvgMs, 33.33, 2.0);
}

void TestReset() {
    std::printf("TestReset\n");
    Metrics m(1'000'000);
    m.RecordFrame(0);
    m.RecordFrame(33333);
    TEST_ASSERT(m.GetFrameCount() == 2);

    m.Reset();
    TEST_ASSERT(m.GetFrameCount() == 0);
    auto s = m.ComputeStats();
    TEST_ASSERT(s.totalFrames == 0);
}

void TestGetFrameCount() {
    std::printf("TestGetFrameCount\n");
    Metrics m(1'000'000);
    for (int i = 0; i < 100; ++i) {
        m.RecordFrame(i * 33333);
    }
    TEST_ASSERT(m.GetFrameCount() == 100);
}

// ============================================================
// Main
// ============================================================

int main() {
    std::printf("=== Metrics Tests ===\n\n");

    TestEmptyMetrics();
    TestSingleFrame();
    TestSteady30FPS();
    TestSteady60FPS();
    TestDroppedFrames();
    TestJitteryIntervals();
    TestReset();
    TestGetFrameCount();

    std::printf("\n%d/%d assertions passed\n", g_passed, g_total);
    if (g_passed == g_total) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        std::printf("SOME TESTS FAILED\n");
        return 1;
    }
}
