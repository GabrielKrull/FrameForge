#include "capture/CaptureRunner.h"

#include "diagnostics/Logger.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <thread>

namespace frameforge {

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedSeconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

bool StopRequested(const std::atomic_bool* stopFlag) {
    return stopFlag && stopFlag->load(std::memory_order_relaxed);
}

TelemetrySessionInfo ToTelemetryInfo(const CaptureSessionInfo& info) {
    TelemetrySessionInfo telemetry;
    telemetry.deviceName     = info.deviceName;
    telemetry.symbolicLink   = info.symbolicLink;
    telemetry.width          = info.width;
    telemetry.height         = info.height;
    telemetry.stride         = info.stride;
    telemetry.fps            = info.fps;
    telemetry.format         = info.format;
    telemetry.mediaTypeIndex = info.mediaTypeIndex;
    return telemetry;
}

} // anonymous namespace

CaptureRunner::CaptureRunner(CaptureSessionFactory factory,
                             ReconnectPolicy policy,
                             Telemetry& telemetry)
    : m_factory(std::move(factory)),
      m_policy(policy),
      m_telemetry(telemetry) {
    m_telemetry.SetState(m_states.Current());
}

bool CaptureRunner::TransitionTo(CaptureState state) {
    if (!m_states.TryTransition(state)) {
        Logger::Global().Warn("CaptureRunner", std::format(
            "Invalid capture state transition {} -> {}; forcing",
            CaptureStateName(m_states.Current()), CaptureStateName(state)));
        m_states.Force(state);
    }
    m_telemetry.SetState(state);
    return true;
}

HRESULT CaptureRunner::Run(const CaptureRunConfig& config,
                           Metrics& metrics,
                           const FrameSink& onFrame,
                           const std::atomic_bool* stopFlag) {
    if (!m_factory) {
        Logger::Global().Error("CaptureRunner",
            "Run called without a session factory");
        return E_INVALIDARG;
    }

    const bool useDuration = config.durationSec > 0.0;
    if (!useDuration && !stopFlag) {
        Logger::Global().Error("CaptureRunner",
            "Run with no duration requires a stop flag");
        return E_INVALIDARG;
    }

    const Clock::time_point runStart = Clock::now();

    HRESULT finalResult = S_OK;
    HRESULT lastError = S_OK;
    int failedAttempts = 0;
    bool retryPending = false;
    bool completed = false;

    auto handleFailure = [&](HRESULT error,
                             const char* what) -> std::optional<bool> {
        // Returns nullopt when the run must stop (finalResult was set).
        lastError = error;
        m_telemetry.OnCaptureError(
            std::format("{}: {}", what, HResultToString(error)));
        TransitionTo(CaptureState::RecoverableError);

        if (StopRequested(stopFlag)) {
            finalResult = S_OK;
            return std::nullopt;
        }

        ++failedAttempts;
        if (!m_policy.ShouldRetry(failedAttempts)) {
            Logger::Global().Error("CaptureRunner", std::format(
                "Retry budget exhausted after {} consecutive failure(s)",
                failedAttempts));
            m_telemetry.OnReconnectExhausted();
            finalResult = lastError;
            return std::nullopt;
        }

        TransitionTo(CaptureState::Reconnecting);
        const int delayMs = m_policy.DelayMsForAttempt(failedAttempts);
        m_telemetry.OnReconnectAttempt(failedAttempts, delayMs);

        double maxWaitSec = -1.0;
        if (useDuration) {
            maxWaitSec = config.durationSec - ElapsedSeconds(runStart);
            if (maxWaitSec <= 0.0) {
                // The overall deadline elapsed during recovery. Per the
                // Run() contract this is a normal completion, not a
                // capture failure.
                finalResult = S_OK;
                return std::nullopt;
            }
        }

        Logger::Global().Warn("CaptureRunner", std::format(
            "Recovering in {} ms (attempt {}/{})",
            delayMs, failedAttempts,
            m_policy.maxAttempts == 0
                ? std::string("unlimited")
                : std::to_string(m_policy.maxAttempts)));

        retryPending = true;
        const BackoffResult backoff =
            WaitForBackoff(delayMs, maxWaitSec, stopFlag);

        if (backoff == BackoffResult::Cancelled) {
            finalResult = S_OK;
            return std::nullopt;
        }
        if (backoff == BackoffResult::DeadlineReached) {
            // See above: the run deadline is a normal completion.
            finalResult = S_OK;
            return std::nullopt;
        }

        return true;
    };

    while (!completed) {
        if (StopRequested(stopFlag)) {
            finalResult = S_OK;
            break;
        }
        if (useDuration &&
            ElapsedSeconds(runStart) >= config.durationSec) {
            finalResult = S_OK;
            break;
        }

        // Reconnecting -> Available before every acquisition attempt.
        if (m_states.Is(CaptureState::Disconnected) ||
            m_states.Is(CaptureState::Reconnecting)) {
            TransitionTo(CaptureState::Available);
        }

        std::unique_ptr<ICaptureSession> session;
        try {
            session = m_factory();
        } catch (...) {
            session = nullptr;
        }

        if (!session) {
            const auto action =
                handleFailure(E_POINTER, "session factory failed");
            if (!action) break;
            continue;
        }

        TransitionTo(CaptureState::Initializing);

        HRESULT hr = session->Open();
        if (FAILED(hr)) {
            session->Close();
            const auto action = handleFailure(hr, "session open failed");
            if (!action) break;
            continue;
        }

        if (retryPending) {
            m_telemetry.OnReconnectSucceeded();
            retryPending = false;
        }

        failedAttempts = 0;
        m_telemetry.OnSessionOpened();
        m_telemetry.SetSessionInfo(ToTelemetryInfo(session->Info()));
        TransitionTo(CaptureState::Active);

        double remaining = 0.0;
        if (useDuration) {
            remaining = config.durationSec - ElapsedSeconds(runStart);
            if (remaining <= 0.0) {
                session->Close();
                finalResult = S_OK;
                break;
            }
        }

        hr = session->Run(remaining, metrics, config.expectedFPS,
                          onFrame, stopFlag);
        session->Close();

        if (FAILED(hr)) {
            const auto action = handleFailure(hr, "capture failed");
            if (!action) break;
            continue;
        }

        // S_OK means the deadline elapsed or the caller requested a
        // stop; there is nothing left to recover.
        finalResult = S_OK;
        completed = true;
    }

    if (m_states.Is(CaptureState::Disconnected)) {
        m_telemetry.SetState(CaptureState::Disconnected);
    } else {
        if (!m_states.Is(CaptureState::ShuttingDown)) {
            TransitionTo(CaptureState::ShuttingDown);
        }
        TransitionTo(CaptureState::Disconnected);
    }

    Logger::Global().Info("CaptureRunner", std::format(
        "Runner stopped: result={} elapsed={:.2f}s failures={}{}",
        HResultToString(finalResult), ElapsedSeconds(runStart),
        failedAttempts,
        retryPending ? " (a retry was pending)" : ""));

    return finalResult;
}

CaptureRunner::BackoffResult CaptureRunner::WaitForBackoff(
    int delayMs, double maxWaitSec, const std::atomic_bool* stopFlag) {

    double waitMs = static_cast<double>(std::max(0, delayMs));
    if (maxWaitSec >= 0.0) {
        const double maxWaitMs = maxWaitSec * 1000.0;
        if (maxWaitMs <= 0.0) {
            return BackoffResult::DeadlineReached;
        }
        waitMs = std::min(waitMs, maxWaitMs);
    }

    const auto start = Clock::now();
    const double totalMs = waitMs;

    while (true) {
        if (StopRequested(stopFlag)) {
            return BackoffResult::Cancelled;
        }

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(
                Clock::now() - start).count();

        if (elapsedMs >= totalMs) {
            break;
        }

        const double remainingMs = totalMs - elapsedMs;
        const auto slice = std::chrono::milliseconds(
            static_cast<int>(std::max(1.0, std::min(50.0, remainingMs))));
        std::this_thread::sleep_for(slice);
    }

    if (StopRequested(stopFlag)) {
        return BackoffResult::Cancelled;
    }

    // Signal a deadline only when the cap intentionally shortened the
    // requested backoff.
    if (maxWaitSec >= 0.0 &&
        totalMs + 0.5 < static_cast<double>(std::max(0, delayMs))) {
        return BackoffResult::DeadlineReached;
    }

    return BackoffResult::Completed;
}

} // namespace frameforge
