#include "audio/AVSyncTracker.h"

#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace frameforge {

AVSyncTracker::AVSyncTracker(int64_t qpcFrequency)
    : m_qpcFreq(qpcFrequency) {
    if (m_qpcFreq <= 0) {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        m_qpcFreq = freq.QuadPart;
    }
}

void AVSyncTracker::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_videoCount = 0;
    m_audioPacketCount = 0;
    m_audioTotalFrames = 0;
    m_firstVideoQpc = 0;
    m_lastVideoQpc = 0;
    m_firstAudioQpc = 0;
    m_lastAudioQpc = 0;
    m_lastOffsetMs = 0.0;
    m_minOffsetMs = 0.0;
    m_maxOffsetMs = 0.0;
    m_sumOffsetMs = 0.0;
    m_offsetSamples = 0;
}

void AVSyncTracker::SetNominalRates(double expectedVideoFps, uint32_t expectedAudioSampleRate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (expectedVideoFps > 0.0) m_expectedFps = expectedVideoFps;
    if (expectedAudioSampleRate > 0) m_expectedSampleRate = expectedAudioSampleRate;
}

void AVSyncTracker::RecordVideoFrame(int64_t qpcTimestamp, int64_t /*mediaTime100ns*/) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_videoCount;
    if (m_firstVideoQpc == 0) {
        m_firstVideoQpc = qpcTimestamp;
    }
    m_lastVideoQpc = qpcTimestamp;
}

void AVSyncTracker::RecordAudioPacket(int64_t qpcTimestamp, uint32_t frames, uint64_t /*devicePosition*/) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_audioPacketCount;
    m_audioTotalFrames += frames;

    if (m_firstAudioQpc == 0) {
        m_firstAudioQpc = qpcTimestamp;
    }
    m_lastAudioQpc = qpcTimestamp;

    if (m_lastVideoQpc > 0 && m_qpcFreq > 0) {
        const double msPerTick = 1000.0 / static_cast<double>(m_qpcFreq);
        const double offsetMs = static_cast<double>(qpcTimestamp - m_lastVideoQpc) * msPerTick;

        m_lastOffsetMs = offsetMs;
        if (m_offsetSamples == 0) {
            m_minOffsetMs = offsetMs;
            m_maxOffsetMs = offsetMs;
        } else {
            if (offsetMs < m_minOffsetMs) m_minOffsetMs = offsetMs;
            if (offsetMs > m_maxOffsetMs) m_maxOffsetMs = offsetMs;
        }
        m_sumOffsetMs += offsetMs;
        ++m_offsetSamples;
    }
}

AVSyncStats AVSyncTracker::ComputeStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    AVSyncStats stats;
    stats.videoFramesSeen  = m_videoCount;
    stats.audioPacketsSeen = m_audioPacketCount;

    if (m_offsetSamples > 0) {
        stats.currentOffsetMs = m_lastOffsetMs;
        stats.minOffsetMs     = m_minOffsetMs;
        stats.maxOffsetMs     = m_maxOffsetMs;
        stats.avgOffsetMs     = m_sumOffsetMs / static_cast<double>(m_offsetSamples);
        stats.inSync50ms      = std::abs(stats.avgOffsetMs) <= 50.0;
    }

    // Compute clock drift between audio stream time and video stream time,
    // accounting for any startup offset between the devices.
    if (m_videoCount > 1 && m_audioTotalFrames > 0 &&
        m_expectedFps > 0.0 && m_expectedSampleRate > 0 && m_qpcFreq > 0) {

        const double videoStreamTimeSec = static_cast<double>(m_videoCount) / m_expectedFps;
        const double audioStreamTimeSec = static_cast<double>(m_audioTotalFrames) / static_cast<double>(m_expectedSampleRate);

        double startDeltaSec = 0.0;
        if (m_firstAudioQpc > 0 && m_firstVideoQpc > 0) {
            startDeltaSec = static_cast<double>(m_firstAudioQpc - m_firstVideoQpc) / static_cast<double>(m_qpcFreq);
        }

        const double adjustedAudioSec = audioStreamTimeSec + startDeltaSec;
        const double elapsedSec = (videoStreamTimeSec + adjustedAudioSec) * 0.5;

        if (elapsedSec > 0.5) {
            const double diffSec = adjustedAudioSec - videoStreamTimeSec;
            stats.driftPpm = (diffSec / elapsedSec) * 1'000'000.0;
            stats.driftMsPerMin = (diffSec / elapsedSec) * 60'000.0;
        }
    }

    return stats;
}

} // namespace frameforge
