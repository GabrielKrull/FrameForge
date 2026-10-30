#pragma once

#include <cstdint>
#include <mutex>

namespace frameforge {

struct AVSyncStats {
    uint64_t videoFramesSeen = 0;
    uint64_t audioPacketsSeen = 0;

    double currentOffsetMs = 0.0; // audio arrival QPC minus most recent video arrival QPC
    double minOffsetMs     = 0.0;
    double maxOffsetMs     = 0.0;
    double avgOffsetMs     = 0.0;

    // Clock drift estimate based on audio sample count vs video frame count
    double driftPpm        = 0.0;  // Parts-per-million drift rate
    double driftMsPerMin   = 0.0;  // Drift in milliseconds per minute

    bool   inSync50ms      = true; // Acceptance criterion: |offset| <= 50ms
};

class AVSyncTracker {
public:
    explicit AVSyncTracker(int64_t qpcFrequency = 0);

    void Reset();

    void SetNominalRates(double expectedVideoFps, uint32_t expectedAudioSampleRate);

    void RecordVideoFrame(int64_t qpcTimestamp, int64_t mediaTime100ns = 0);

    void RecordAudioPacket(int64_t qpcTimestamp, uint32_t frames, uint64_t devicePosition = 0);

    AVSyncStats ComputeStats() const;

private:
    int64_t m_qpcFreq = 0;
    mutable std::mutex m_mutex;

    double   m_expectedFps = 30.0;
    uint32_t m_expectedSampleRate = 48000;

    uint64_t m_videoCount = 0;
    uint64_t m_audioPacketCount = 0;
    uint64_t m_audioTotalFrames = 0;

    int64_t  m_firstVideoQpc = 0;
    int64_t  m_lastVideoQpc  = 0;

    int64_t  m_firstAudioQpc = 0;
    int64_t  m_lastAudioQpc  = 0;

    double   m_lastOffsetMs = 0.0;
    double   m_minOffsetMs  = 0.0;
    double   m_maxOffsetMs  = 0.0;
    double   m_sumOffsetMs  = 0.0;
    uint64_t m_offsetSamples = 0;
};

} // namespace frameforge

