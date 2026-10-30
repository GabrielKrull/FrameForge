#include "diagnostics/CaptureState.h"

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
// Capture state machine
// ============================================================

void TestDefaultState() {
    std::printf("TestDefaultState\n");
    CaptureStateMachine machine;
    TEST_ASSERT(machine.Current() == CaptureState::Disconnected);
    TEST_ASSERT(machine.Is(CaptureState::Disconnected));
}

void TestHappyPathTransitions() {
    std::printf("TestHappyPathTransitions\n");
    CaptureStateMachine machine;

    TEST_ASSERT(machine.TryTransition(CaptureState::Available));
    TEST_ASSERT(machine.TryTransition(CaptureState::Initializing));
    TEST_ASSERT(machine.TryTransition(CaptureState::Active));
    TEST_ASSERT(machine.Current() == CaptureState::Active);

    TEST_ASSERT(machine.TryTransition(CaptureState::RecoverableError));
    TEST_ASSERT(machine.TryTransition(CaptureState::Reconnecting));
    TEST_ASSERT(machine.TryTransition(CaptureState::Available));
    TEST_ASSERT(machine.TryTransition(CaptureState::Initializing));
    TEST_ASSERT(machine.TryTransition(CaptureState::Active));
}

void TestInvalidTransitionsAreRejected() {
    std::printf("TestInvalidTransitionsAreRejected\n");
    CaptureStateMachine machine;

    // Disconnected cannot jump straight to Active.
    TEST_ASSERT(!machine.TryTransition(CaptureState::Active));
    TEST_ASSERT(machine.Current() == CaptureState::Disconnected);

    // Disconnected cannot enter the recovery states either.
    TEST_ASSERT(!machine.TryTransition(CaptureState::RecoverableError));
    TEST_ASSERT(!machine.TryTransition(CaptureState::Reconnecting));

    TEST_ASSERT(machine.TryTransition(CaptureState::Available));
    // Available cannot skip Initializing.
    TEST_ASSERT(!machine.TryTransition(CaptureState::Active));
    TEST_ASSERT(machine.Current() == CaptureState::Available);
}

void TestSameStateIsAccepted() {
    std::printf("TestSameStateIsAccepted\n");
    CaptureStateMachine machine(CaptureState::Active);
    TEST_ASSERT(machine.TryTransition(CaptureState::Active));
    TEST_ASSERT(machine.Current() == CaptureState::Active);
}

void TestShutdownTransition() {
    std::printf("TestShutdownTransition\n");
    CaptureStateMachine machine(CaptureState::Active);

    TEST_ASSERT(machine.TryTransition(CaptureState::ShuttingDown));
    // ShuttingDown is only allowed to go back to Disconnected.
    TEST_ASSERT(!machine.TryTransition(CaptureState::Available));
    TEST_ASSERT(!machine.TryTransition(CaptureState::Active));
    TEST_ASSERT(machine.TryTransition(CaptureState::Disconnected));
    TEST_ASSERT(machine.Current() == CaptureState::Disconnected);
}

void TestForceBypassesValidation() {
    std::printf("TestForceBypassesValidation\n");
    CaptureStateMachine machine;
    machine.Force(CaptureState::Active);
    TEST_ASSERT(machine.Current() == CaptureState::Active);
}

void TestStateNames() {
    std::printf("TestStateNames\n");
    TEST_ASSERT(CaptureStateName(CaptureState::Disconnected) == "DISCONNECTED");
    TEST_ASSERT(CaptureStateName(CaptureState::Available) == "AVAILABLE");
    TEST_ASSERT(CaptureStateName(CaptureState::Initializing) == "INITIALIZING");
    TEST_ASSERT(CaptureStateName(CaptureState::Active) == "ACTIVE");
    TEST_ASSERT(CaptureStateName(CaptureState::RecoverableError) ==
                "RECOVERABLE_ERROR");
    TEST_ASSERT(CaptureStateName(CaptureState::Reconnecting) ==
                "RECONNECTING");
    TEST_ASSERT(CaptureStateName(CaptureState::ShuttingDown) ==
                "SHUTTING_DOWN");
}

int main() {
    std::printf("=== Capture state tests ===\n");

    TestDefaultState();
    TestHappyPathTransitions();
    TestInvalidTransitionsAreRejected();
    TestSameStateIsAccepted();
    TestShutdownTransition();
    TestForceBypassesValidation();
    TestStateNames();

    std::printf("\n%d/%d assertions passed\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
