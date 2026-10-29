#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace frameforge {

// ============================================================
// Snapshot of computed frame statistics
// ============================================================
struct FrameStats {
    uint64_t totalFrames    = 0;
    double   durationSec    = 0.0;
    double   effectiveFPS   = 0.0;
    double   intervalMinMs  = 0.0;
    double   intervalMaxMs  = 0.0;
    double   intervalAvgMs  = 0.0;
    double   intervalStdDev = 0.0;
    uint64_t estimatedDrops = 0;
};

// ============================================================
// Thread-safe metrics collector.
// Feed QPC timestamps as frames arrive; call ComputeStats() to
// get a snapshot of timing statistics.
// ============================================================
class Metrics {
public:
    // qpcFrequency: QueryPerformanceFrequency value (ticks per second)
    explicit Metrics(int64_t qpcFrequency);
    ~Metrics() = default;

    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;

    // Record the arrival time of one frame (QPC ticks)
    void RecordFrame(int64_t qpcTimestamp);

    // Expected FPS used to estimate dropped frames.
    // Pass 0.0 to disable drop estimation.
    void SetExpectedFPS(double fps);

    // Compute and return current statistics
    FrameStats ComputeStats() const;

    // Clear all recorded frames and stats
    void Reset();

    // Current frame count (thread-safe)
    uint64_t GetFrameCount() const;

private:
    int64_t m_qpcFrequency;
    double  m_expectedFPS = 0.0;

    mutable std::mutex     m_mutex;
    std::vector<int64_t>   m_timestamps;
};

} // namespace frameforge
