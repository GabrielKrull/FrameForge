#pragma once

#include <atomic>
#include <mutex>
#include <string_view>

namespace frameforge {

// ============================================================
// Explicit capture lifecycle.
//
// The state machine is intentionally small and dependency-free
// so it can be unit tested and reused by the UI, the runner and
// the telemetry layer.
//
//   Disconnected -> Available -> Initializing -> Active
//                       ^                         |
//                       |                         v
//                       +---- Reconnecting <-- RecoverableError
//
// ShuttingDown is reachable from every state; Disconnected is the
// only state reachable from ShuttingDown.
// ============================================================
enum class CaptureState {
    Disconnected = 0,
    Available,
    Initializing,
    Active,
    RecoverableError,
    Reconnecting,
    ShuttingDown,
};

inline std::string_view CaptureStateName(CaptureState state) {
    switch (state) {
    case CaptureState::Disconnected:     return "DISCONNECTED";
    case CaptureState::Available:        return "AVAILABLE";
    case CaptureState::Initializing:     return "INITIALIZING";
    case CaptureState::Active:           return "ACTIVE";
    case CaptureState::RecoverableError: return "RECOVERABLE_ERROR";
    case CaptureState::Reconnecting:     return "RECONNECTING";
    case CaptureState::ShuttingDown:     return "SHUTTING_DOWN";
    }
    return "UNKNOWN";
}

inline bool IsValidCaptureTransition(CaptureState from, CaptureState to) {
    if (from == to) {
        return true;
    }

    switch (from) {
    case CaptureState::Disconnected:
        return to == CaptureState::Available ||
               to == CaptureState::ShuttingDown;

    case CaptureState::Available:
        return to == CaptureState::Initializing ||
               to == CaptureState::Disconnected ||
               to == CaptureState::ShuttingDown;

    case CaptureState::Initializing:
        return to == CaptureState::Active ||
               to == CaptureState::RecoverableError ||
               to == CaptureState::Disconnected ||
               to == CaptureState::ShuttingDown;

    case CaptureState::Active:
        return to == CaptureState::RecoverableError ||
               to == CaptureState::Disconnected ||
               to == CaptureState::ShuttingDown;

    case CaptureState::RecoverableError:
        return to == CaptureState::Reconnecting ||
               to == CaptureState::Disconnected ||
               to == CaptureState::ShuttingDown;

    case CaptureState::Reconnecting:
        return to == CaptureState::Available ||
               to == CaptureState::Initializing ||
               to == CaptureState::Disconnected ||
               to == CaptureState::ShuttingDown;

    case CaptureState::ShuttingDown:
        return to == CaptureState::Disconnected;
    }

    return false;
}

// Thread-safe holder for the current capture state. Transition
// validation and the store happen atomically with respect to other
// transitions; readers only pay for an atomic load.
class CaptureStateMachine {
public:
    CaptureStateMachine() = default;
    explicit CaptureStateMachine(CaptureState initial)
        : m_state(initial) {}

    CaptureState Current() const {
        return m_state.load(std::memory_order_acquire);
    }

    bool Is(CaptureState state) const {
        return Current() == state;
    }

    // Returns true when the transition is valid (or when the state
    // is already the requested one). The state is not changed when
    // the transition is rejected.
    bool TryTransition(CaptureState next) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const CaptureState current = m_state.load(std::memory_order_relaxed);
        if (current == next) {
            return true;
        }
        if (!IsValidCaptureTransition(current, next)) {
            return false;
        }
        m_state.store(next, std::memory_order_release);
        return true;
    }

    // Escape hatch for tests and for recovery paths that must not
    // fail because of an unexpected previous state.
    void Force(CaptureState next) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.store(next, std::memory_order_release);
    }

private:
    mutable std::mutex m_mutex;
    std::atomic<CaptureState> m_state{ CaptureState::Disconnected };
};

} // namespace frameforge
