#pragma once

#include "audio/AudioTypes.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace frameforge {

struct AudioStats {
    uint64_t totalPackets           = 0;
    uint64_t totalSampleFrames       = 0;
    uint64_t totalBytes             = 0;
    uint64_t discontinuities        = 0;
    uint64_t silentPackets          = 0;

    double   durationSec            = 0.0;
    double   effectiveSampleRate    = 0.0;

    double   intervalMinMs          = 0.0;
    double   intervalMaxMs          = 0.0;
    double   intervalAvgMs          = 0.0;
    double   intervalStdDevMs       = 0.0;
};

class AudioMetrics {
public:
    explicit AudioMetrics(int64_t qpcFrequency = 0);

    void Reset();

    // Record an incoming audio packet.
    void RecordPacket(const AudioPacket& packet, uint32_t sampleRate);

    // Compute aggregated statistical summary.
    AudioStats ComputeStats() const;

    int64_t QpcFrequency() const { return m_qpcFreq; }

private:
    int64_t m_qpcFreq = 0;
    mutable std::mutex m_mutex;

    uint64_t m_totalPackets       = 0;
    uint64_t m_totalSampleFrames   = 0;
    uint64_t m_totalBytes         = 0;
    uint64_t m_discontinuities    = 0;
    uint64_t m_silentPackets      = 0;

    int64_t  m_firstQpc           = 0;
    int64_t  m_lastQpc            = 0;
    std::vector<int64_t> m_packetArrivals;
};

} // namespace frameforge

