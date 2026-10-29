#pragma once

#include <algorithm>
#include <cstdint>

namespace frameforge {

// ============================================================
// Reconnection policy with capped exponential backoff.
//
// maxAttempts is the total number of consecutive reconnect
// attempts allowed after a failure. 0 means "retry until the
// caller cancels".
//
// The delay before attempt N (1-based) is:
//   min(baseDelayMs * multiplier^(N-1), maxDelayMs)
//
// The policy is a plain value type so the runner can be tested
// without any timing dependency.
// ============================================================
struct ReconnectPolicy {
    int   maxAttempts  = 5;      // 0 = unlimited
    int   baseDelayMs  = 250;
    int   maxDelayMs   = 5000;
    double multiplier  = 2.0;

    // True when another attempt is allowed after 'failedAttempts'
    // consecutive failures (passed value already includes the
    // failure that just happened).
    bool ShouldRetry(int failedAttempts) const {
        if (failedAttempts <= 0) {
            return true;
        }
        return maxAttempts == 0 || failedAttempts <= maxAttempts;
    }

    // Delay before a given 1-based reconnect attempt.
    int DelayMsForAttempt(int attempt) const {
        if (attempt < 1) {
            attempt = 1;
        }

        const int base = std::max(0, baseDelayMs);
        const int cap  = std::max(base, maxDelayMs);

        double delay = static_cast<double>(base);
        const double factor = multiplier > 1.0 ? multiplier : 1.0;
        for (int i = 1; i < attempt && delay < static_cast<double>(cap); ++i) {
            delay *= factor;
        }

        if (delay >= static_cast<double>(cap)) {
            return cap;
        }
        return static_cast<int>(delay);
    }
};

} // namespace frameforge
