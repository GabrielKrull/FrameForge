#pragma once

#include "capture/CaptureSession.h"
#include "capture/ReconnectPolicy.h"
#include "diagnostics/CaptureState.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Telemetry.h"

#include <atomic>
#include <functional>
#include <memory>

namespace frameforge {

struct CaptureRunConfig {
    double durationSec = 5.0; // <= 0 = until stopFlag
    double expectedFPS = 0.0;
};

// ============================================================
// Owns the capture lifecycle and the recovery loop.
//
// A fresh session is created for every attempt. Failures move the
// state machine through RecoverableError -> Reconnecting and are
// retried with capped exponential backoff. The wait is sliced and
// checks the stop flag, so cancellation is bounded (the only
// non-cancellable step is a blocking device Open()).
// ============================================================
class CaptureRunner {
public:
    CaptureRunner(CaptureSessionFactory factory,
                  ReconnectPolicy policy,
                  Telemetry& telemetry);

    CaptureRunner(const CaptureRunner&) = delete;
    CaptureRunner& operator=(const CaptureRunner&) = delete;

    // durationSec > 0 : overall deadline spanning reconnections.
    // durationSec <= 0: run until stopFlag is set (required).
    //
    // Returns S_OK when the deadline elapsed or the caller stopped the
    // run. Returns the last error when the retry budget is exhausted.
    HRESULT Run(const CaptureRunConfig& config,
                Metrics& metrics,
                const FrameSink& onFrame,
                const std::atomic_bool* stopFlag);

    CaptureState CurrentState() const { return m_states.Current(); }
    const ReconnectPolicy& Policy() const { return m_policy; }

private:
    enum class BackoffResult {
        Completed,
        Cancelled,
        DeadlineReached,
    };

    bool TransitionTo(CaptureState state);
    BackoffResult WaitForBackoff(int delayMs,
                                 double maxWaitSec,
                                 const std::atomic_bool* stopFlag);

    CaptureSessionFactory m_factory;
    ReconnectPolicy m_policy;
    Telemetry& m_telemetry;
    CaptureStateMachine m_states;
};

} // namespace frameforge
