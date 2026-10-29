#include "diagnostics/Metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace frameforge {

Metrics::Metrics(int64_t qpcFrequency)
    : m_qpcFrequency(qpcFrequency > 0 ? qpcFrequency : 1) {
}

void Metrics::RecordFrame(int64_t qpcTimestamp) {
    std::lock_guard lock(m_mutex);
    m_timestamps.push_back(qpcTimestamp);
}

void Metrics::SetExpectedFPS(double fps) {
    std::lock_guard lock(m_mutex);
    m_expectedFPS = fps;
}

FrameStats Metrics::ComputeStats() const {
    std::lock_guard lock(m_mutex);

    FrameStats stats;
    stats.totalFrames = m_timestamps.size();

    if (m_timestamps.size() < 2) {
        return stats;
    }

    const double ticksToMs = 1000.0 / static_cast<double>(m_qpcFrequency);

    // Duration: last timestamp - first timestamp
    int64_t durationTicks = m_timestamps.back() - m_timestamps.front();
    stats.durationSec = static_cast<double>(durationTicks)
                        / static_cast<double>(m_qpcFrequency);

    // Effective FPS
    if (stats.durationSec > 0.0) {
        stats.effectiveFPS =
            static_cast<double>(m_timestamps.size() - 1) / stats.durationSec;
    }

    // Collect intervals between consecutive frames
    std::vector<double> intervalsMs;
    intervalsMs.reserve(m_timestamps.size() - 1);
    for (size_t i = 1; i < m_timestamps.size(); ++i) {
        double intervalMs =
            static_cast<double>(m_timestamps[i] - m_timestamps[i - 1]) * ticksToMs;
        intervalsMs.push_back(intervalMs);
    }

    // Min / Max / Avg
    auto [minIt, maxIt] = std::minmax_element(
        intervalsMs.begin(), intervalsMs.end());
    stats.intervalMinMs = *minIt;
    stats.intervalMaxMs = *maxIt;
    stats.intervalAvgMs =
        std::accumulate(intervalsMs.begin(), intervalsMs.end(), 0.0)
        / static_cast<double>(intervalsMs.size());

    // StdDev (population)
    double sumSqDiff = 0.0;
    for (double v : intervalsMs) {
        double diff = v - stats.intervalAvgMs;
        sumSqDiff += diff * diff;
    }
    stats.intervalStdDev =
        std::sqrt(sumSqDiff / static_cast<double>(intervalsMs.size()));

    // Estimated drops: any interval > 1.75x the expected frame time
    if (m_expectedFPS > 0.0) {
        double expectedIntervalMs = 1000.0 / m_expectedFPS;
        for (double intervalMs : intervalsMs) {
            if (intervalMs > expectedIntervalMs * 1.75) {
                stats.estimatedDrops +=
                    static_cast<uint64_t>(intervalMs / expectedIntervalMs) - 1;
            }
        }
    }

    return stats;
}

void Metrics::Reset() {
    std::lock_guard lock(m_mutex);
    m_timestamps.clear();
}

uint64_t Metrics::GetFrameCount() const {
    std::lock_guard lock(m_mutex);
    return m_timestamps.size();
}

} // namespace frameforge
