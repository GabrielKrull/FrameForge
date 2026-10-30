#include "capture/ReconnectPolicy.h"

#include <cstdio>

using namespace frameforge;

static int g_total = 0;
static int g_passed = 0;

#define TEST_ASSERT(expr) do { \
    ++g_total; \
    if (expr) { ++g_passed; } \
    else { std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

// ============================================================
// Reconnect policy: retry budget
// ============================================================

void TestRetryBudget() {
    std::printf("TestRetryBudget\n");
    ReconnectPolicy policy;
    policy.maxAttempts = 3;

    TEST_ASSERT(policy.ShouldRetry(0)); // no failure yet
    TEST_ASSERT(policy.ShouldRetry(1));
    TEST_ASSERT(policy.ShouldRetry(2));
    TEST_ASSERT(policy.ShouldRetry(3));
    TEST_ASSERT(!policy.ShouldRetry(4));
}

void TestUnlimitedRetries() {
    std::printf("TestUnlimitedRetries\n");
    ReconnectPolicy policy;
    policy.maxAttempts = 0;

    TEST_ASSERT(policy.ShouldRetry(1));
    TEST_ASSERT(policy.ShouldRetry(100));
    TEST_ASSERT(policy.ShouldRetry(100000));
}

void TestNonPositiveFailuresAreAllowed() {
    std::printf("TestNonPositiveFailuresAreAllowed\n");
    ReconnectPolicy policy;
    policy.maxAttempts = 1;

    TEST_ASSERT(policy.ShouldRetry(-1));
    TEST_ASSERT(policy.ShouldRetry(0));
}

// ============================================================
// Reconnect policy: backoff delays
// ============================================================

void TestExponentialBackoff() {
    std::printf("TestExponentialBackoff\n");
    ReconnectPolicy policy;
    policy.maxAttempts = 0;
    policy.baseDelayMs = 250;
    policy.maxDelayMs  = 5000;
    policy.multiplier  = 2.0;

    TEST_ASSERT(policy.DelayMsForAttempt(1) == 250);
    TEST_ASSERT(policy.DelayMsForAttempt(2) == 500);
    TEST_ASSERT(policy.DelayMsForAttempt(3) == 1000);
    TEST_ASSERT(policy.DelayMsForAttempt(4) == 2000);
    TEST_ASSERT(policy.DelayMsForAttempt(5) == 4000);
    TEST_ASSERT(policy.DelayMsForAttempt(6) == 5000);
    TEST_ASSERT(policy.DelayMsForAttempt(20) == 5000);
}

void TestInvalidAttemptIsClampedToFirst() {
    std::printf("TestInvalidAttemptIsClampedToFirst\n");
    ReconnectPolicy policy;
    policy.baseDelayMs = 100;
    policy.maxDelayMs  = 1000;
    policy.multiplier  = 2.0;

    TEST_ASSERT(policy.DelayMsForAttempt(0) == 100);
    TEST_ASSERT(policy.DelayMsForAttempt(-5) == 100);
}

void TestZeroBaseDelay() {
    std::printf("TestZeroBaseDelay\n");
    ReconnectPolicy policy;
    policy.baseDelayMs = 0;
    policy.maxDelayMs  = 5000;
    policy.multiplier  = 2.0;

    TEST_ASSERT(policy.DelayMsForAttempt(1) == 0);
    TEST_ASSERT(policy.DelayMsForAttempt(10) == 0);
}

void TestMultiplierBelowOneIsNeutralised() {
    std::printf("TestMultiplierBelowOneIsNeutralised\n");
    ReconnectPolicy policy;
    policy.baseDelayMs = 50;
    policy.maxDelayMs  = 500;
    policy.multiplier  = 0.5;

    TEST_ASSERT(policy.DelayMsForAttempt(1) == 50);
    TEST_ASSERT(policy.DelayMsForAttempt(4) == 50);
}

void TestMaxDelayBelowBaseIsRaisedToBase() {
    std::printf("TestMaxDelayBelowBaseIsRaisedToBase\n");
    ReconnectPolicy policy;
    policy.baseDelayMs = 250;
    policy.maxDelayMs  = 100;
    policy.multiplier  = 2.0;

    TEST_ASSERT(policy.DelayMsForAttempt(1) == 250);
    TEST_ASSERT(policy.DelayMsForAttempt(5) == 250);
}

int main() {
    std::printf("=== Reconnect policy tests ===\n");

    TestRetryBudget();
    TestUnlimitedRetries();
    TestNonPositiveFailuresAreAllowed();
    TestExponentialBackoff();
    TestInvalidAttemptIsClampedToFirst();
    TestZeroBaseDelay();
    TestMultiplierBelowOneIsNeutralised();
    TestMaxDelayBelowBaseIsRaisedToBase();

    std::printf("\n%d/%d assertions passed\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
