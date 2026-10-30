#include "audio/AVSyncTracker.h"

#include <cmath>
#include <cstdio>

using namespace frameforge;

static int g_total = 0;
static int g_passed = 0;

#define TEST_ASSERT(expr) do { \
    ++g_total; \
    if (expr) { ++g_passed; } \
    else { std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

int main() {
    std::printf("Running test_av_sync...\n");

    const int64_t qpcFreq = 10'000'000; // 10 MHz (1 tick = 100 ns)
    const int64_t ticksPerMs = qpcFreq / 1000;

    // 1. Initial state
    {
        AVSyncTracker tracker(qpcFreq);
        AVSyncStats stats = tracker.ComputeStats();
        TEST_ASSERT(stats.videoFramesSeen == 0);
        TEST_ASSERT(stats.audioPacketsSeen == 0);
        TEST_ASSERT(stats.currentOffsetMs == 0.0);
        TEST_ASSERT(stats.inSync50ms == true);
    }

    // 2. Synchronized video (60 fps) and audio (48000 Hz, 10ms buffers)
    {
        AVSyncTracker tracker(qpcFreq);
        tracker.SetNominalRates(60.0, 48000);

        int64_t baseQpc = 1'000'000'000;
        // Interleave 60 video frames and 100 audio packets (approx 1 sec)
        // Video interval: ~16.666 ms
        // Audio interval: 10 ms (480 frames)
        for (int i = 0; i < 60; ++i) {
            const int64_t vQpc = baseQpc + static_cast<int64_t>(i * 16.6666 * ticksPerMs);
            tracker.RecordVideoFrame(vQpc);

            // Record audio packet shortly after (e.g. +5 ms)
            const int64_t aQpc = vQpc + 5 * ticksPerMs;
            tracker.RecordAudioPacket(aQpc, 480);
        }

        AVSyncStats stats = tracker.ComputeStats();
        TEST_ASSERT(stats.videoFramesSeen == 60);
        TEST_ASSERT(stats.audioPacketsSeen == 60);
        TEST_ASSERT(std::abs(stats.currentOffsetMs - 5.0) < 0.01);
        TEST_ASSERT(std::abs(stats.avgOffsetMs - 5.0) < 0.01);
        TEST_ASSERT(stats.inSync50ms == true); // Within +-50ms acceptance criterion
    }

    // 3. Audio lagging significantly beyond 50ms tolerance
    {
        AVSyncTracker tracker(qpcFreq);
        tracker.SetNominalRates(60.0, 48000);

        const int64_t vQpc = 100'000'000;
        tracker.RecordVideoFrame(vQpc);

        // Audio arrives 80ms later
        const int64_t aQpc = vQpc + 80 * ticksPerMs;
        tracker.RecordAudioPacket(aQpc, 480);

        AVSyncStats stats = tracker.ComputeStats();
        TEST_ASSERT(std::abs(stats.currentOffsetMs - 80.0) < 0.01);
        TEST_ASSERT(stats.inSync50ms == false); // Out of tolerance
    }

    // 4. Clock drift calculation
    {
        AVSyncTracker tracker(qpcFreq);
        tracker.SetNominalRates(60.0, 48000);

        // 10 seconds of video = 600 frames
        for (int i = 0; i < 600; ++i) {
            tracker.RecordVideoFrame(1000 + i * 166666);
        }

        // 10 seconds of audio + 100ms drift = 480000 + 4800 samples = 484800 samples
        // That's 10.10s audio vs 10.00s video -> 0.10s / 10.05s ~ 9950 PPM (~600 ms/min)
        for (int i = 0; i < 1010; ++i) {
            tracker.RecordAudioPacket(2000 + i * 100000, 480);
        }

        AVSyncStats stats = tracker.ComputeStats();
        TEST_ASSERT(stats.videoFramesSeen == 600);
        TEST_ASSERT(stats.audioPacketsSeen == 1010);
        TEST_ASSERT(stats.driftPpm > 5000.0);
        TEST_ASSERT(stats.driftMsPerMin > 300.0);
    }

    // 5. Reset
    {
        AVSyncTracker tracker(qpcFreq);
        tracker.RecordVideoFrame(100);
        tracker.RecordAudioPacket(200, 480);
        tracker.Reset();

        AVSyncStats stats = tracker.ComputeStats();
        TEST_ASSERT(stats.videoFramesSeen == 0);
        TEST_ASSERT(stats.audioPacketsSeen == 0);
        TEST_ASSERT(stats.currentOffsetMs == 0.0);
    }

    std::printf("test_av_sync: %d/%d passed\n", g_passed, g_total);
    return (g_passed == g_total) ? 0 : 1;
}

