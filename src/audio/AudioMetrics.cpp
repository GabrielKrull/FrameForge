#include "audio/AudioMetrics.h"

#include <cmath>
#include <numeric>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace frameforge {

AudioMetrics::AudioMetrics(int64_t qpcFrequency)
    : m_qpcFreq(qpcFrequency) {
    if (m_qpcFreq <= 0) {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        m_qpcFreq = freq.QuadPart;
    }
}

void AudioMetrics::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_totalPackets = 0;
    m_totalSampleFrames = 0;
    m_totalBytes = 0;
    m_discontinuities = 0;
    m_silentPackets = 0;
    m_firstQpc = 0;
    m_lastQpc = 0;
    m_packetArrivals.clear();
}

void AudioMetrics::RecordPacket(const AudioPacket& packet, uint32_t /*sampleRate*/) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ++m_totalPackets;
    m_totalSampleFrames += packet.frames;
    m_totalBytes += packet.data.size();

    if (packet.IsDiscontinuity()) {
        ++m_discontinuities;
    }
    if (packet.IsSilent()) {
        ++m_silentPackets;
    }

    const int64_t qpc = packet.qpcTimestamp;
    if (m_firstQpc == 0) {
        m_firstQpc = qpc;
    }
    m_lastQpc = qpc;

    if (m_packetArrivals.size() < 100000) {
        m_packetArrivals.push_back(qpc);
    }
}

AudioStats AudioMetrics::ComputeStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    AudioStats stats;
    stats.totalPackets     = m_totalPackets;
    stats.totalSampleFrames = m_totalSampleFrames;
    stats.totalBytes       = m_totalBytes;
    stats.discontinuities  = m_discontinuities;
    stats.silentPackets    = m_silentPackets;

    if (m_firstQpc == 0 || m_lastQpc <= m_firstQpc || m_qpcFreq <= 0) {
        return stats;
    }

    const double ticksPerSec = static_cast<double>(m_qpcFreq);
    stats.durationSec = static_cast<double>(m_lastQpc - m_firstQpc) / ticksPerSec;

    if (stats.durationSec > 0.0) {
        stats.effectiveSampleRate = static_cast<double>(stats.totalSampleFrames) / stats.durationSec;
    }

    if (m_packetArrivals.size() < 2) {
        return stats;
    }

    const double msPerTick = 1000.0 / ticksPerSec;
    std::vector<double> intervals;
    intervals.reserve(m_packetArrivals.size() - 1);

    for (std::size_t i = 1; i < m_packetArrivals.size(); ++i) {
        const double diffMs = static_cast<double>(m_packetArrivals[i] - m_packetArrivals[i - 1]) * msPerTick;
        intervals.push_back(diffMs);
    }

    double minVal = intervals.front();
    double maxVal = intervals.front();
    double sum = 0.0;

    for (double val : intervals) {
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
        sum += val;
    }

    const double avg = sum / static_cast<double>(intervals.size());
    double sumSqDiff = 0.0;
    for (double val : intervals) {
        const double diff = val - avg;
        sumSqDiff += diff * diff;
    }
    const double stdDev = std::sqrt(sumSqDiff / static_cast<double>(intervals.size()));

    stats.intervalMinMs    = minVal;
    stats.intervalMaxMs    = maxVal;
    stats.intervalAvgMs    = avg;
    stats.intervalStdDevMs = stdDev;

    return stats;
}

} // namespace frameforge

