#include "audio/AudioMetrics.h"
#include "audio/AudioTypes.h"

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

int main() {
    std::printf("Running test_audio_metrics...\n");

    const int64_t qpcFreq = 10'000'000; // 10 MHz (100 ns per tick)
    const int64_t ticksPerMs = qpcFreq / 1000;

    // 1. Initial / empty state
    {
        AudioMetrics metrics(qpcFreq);
        AudioStats stats = metrics.ComputeStats();

        TEST_ASSERT(stats.totalPackets == 0);
        TEST_ASSERT(stats.totalSampleFrames == 0);
        TEST_ASSERT(stats.totalBytes == 0);
        TEST_ASSERT(stats.discontinuities == 0);
        TEST_ASSERT(stats.silentPackets == 0);
        TEST_ASSERT(stats.durationSec == 0.0);
        TEST_ASSERT(stats.effectiveSampleRate == 0.0);
    }

    // 2. Normal recording of steady packets (e.g. 10 ms at 48000 Hz = 480 frames per packet)
    {
        AudioMetrics metrics(qpcFreq);
        const uint32_t sampleRate = 48000;
        const uint32_t framesPerPacket = 480; // 10ms
        const int64_t intervalTicks = 10 * ticksPerMs;

        int64_t qpc = 1'000'000'000; // arbitrary base
        for (int i = 0; i < 100; ++i) {
            AudioPacket p;
            p.frames = framesPerPacket;
            p.data.resize(framesPerPacket * 4, 0x55); // 4 bytes/frame (stereo 16-bit)
            p.qpcTimestamp = qpc;
            p.devicePosition = i * framesPerPacket;
            p.flags = 0;

            metrics.RecordPacket(p, sampleRate);
            qpc += intervalTicks;
        }

        AudioStats stats = metrics.ComputeStats();
        TEST_ASSERT(stats.totalPackets == 100);
        TEST_ASSERT(stats.totalSampleFrames == 100 * 480);
        TEST_ASSERT(stats.totalBytes == 100 * 480 * 4);
        TEST_ASSERT(stats.discontinuities == 0);
        TEST_ASSERT(stats.silentPackets == 0);

        // 99 intervals of 10 ms = 0.99 seconds elapsed
        TEST_ASSERT(std::abs(stats.durationSec - 0.99) < 0.001);
        // Effective sample rate: (48000 frames) / 0.99s ~ 48484.8 Hz
        TEST_ASSERT(stats.effectiveSampleRate > 48000.0);

        TEST_ASSERT(std::abs(stats.intervalMinMs - 10.0) < 0.001);
        TEST_ASSERT(std::abs(stats.intervalMaxMs - 10.0) < 0.001);
        TEST_ASSERT(std::abs(stats.intervalAvgMs - 10.0) < 0.001);
        TEST_ASSERT(stats.intervalStdDevMs < 0.0001);
    }

    // 3. Discontinuities and silent packets
    {
        AudioMetrics metrics(qpcFreq);
        int64_t qpc = 500'000;

        // Normal packet
        AudioPacket p1;
        p1.frames = 480;
        p1.data.resize(480 * 4);
        p1.qpcTimestamp = qpc;
        metrics.RecordPacket(p1, 48000);

        // Silent packet
        AudioPacket p2;
        p2.frames = 480;
        p2.data.resize(480 * 4);
        p2.qpcTimestamp = qpc + 10 * ticksPerMs;
        p2.flags = AUDCLNT_BUFFERFLAGS_SILENT;
        metrics.RecordPacket(p2, 48000);

        // Discontinuity packet
        AudioPacket p3;
        p3.frames = 480;
        p3.data.resize(480 * 4);
        p3.qpcTimestamp = qpc + 30 * ticksPerMs; // gap of 20ms
        p3.flags = AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY;
        metrics.RecordPacket(p3, 48000);

        AudioStats stats = metrics.ComputeStats();
        TEST_ASSERT(stats.totalPackets == 3);
        TEST_ASSERT(stats.silentPackets == 1);
        TEST_ASSERT(stats.discontinuities == 1);
        TEST_ASSERT(stats.intervalMinMs < stats.intervalMaxMs);
    }

    // 4. Reset
    {
        AudioMetrics metrics(qpcFreq);
        AudioPacket p;
        p.frames = 480;
        p.data.resize(100);
        p.qpcTimestamp = 1000;
        metrics.RecordPacket(p, 48000);

        metrics.Reset();
        AudioStats stats = metrics.ComputeStats();
        TEST_ASSERT(stats.totalPackets == 0);
        TEST_ASSERT(stats.totalSampleFrames == 0);
        TEST_ASSERT(stats.totalBytes == 0);
        TEST_ASSERT(stats.discontinuities == 0);
    }

    std::printf("test_audio_metrics: %d/%d passed\n", g_passed, g_total);
    return (g_passed == g_total) ? 0 : 1;
}

