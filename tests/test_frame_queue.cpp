#include "core/BoundedQueue.h"
#include "preview/FrameQueue.h"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace frameforge;

static int g_total = 0;
static int g_passed = 0;

#define TEST_ASSERT(expr) do { \
    ++g_total; \
    if (expr) { ++g_passed; } \
    else { std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

// ============================================================
// Bounded queue behaviour
// ============================================================

void TestFifoOrder() {
    std::printf("TestFifoOrder\n");
    BoundedQueue<int> queue(4);

    TEST_ASSERT(queue.Push(1) == BoundedQueue<int>::PushResult::Pushed);
    TEST_ASSERT(queue.Push(2) == BoundedQueue<int>::PushResult::Pushed);
    TEST_ASSERT(queue.Push(3) == BoundedQueue<int>::PushResult::Pushed);
    TEST_ASSERT(queue.Size() == 3);

    int value = 0;
    TEST_ASSERT(queue.TryPop(value) && value == 1);
    TEST_ASSERT(queue.TryPop(value) && value == 2);
    TEST_ASSERT(queue.TryPop(value) && value == 3);
    TEST_ASSERT(!queue.TryPop(value));
    TEST_ASSERT(queue.TotalPopped() == 3);
    TEST_ASSERT(queue.TotalDropped() == 0);
}

void TestDropOldestWhenFull() {
    std::printf("TestDropOldestWhenFull\n");
    BoundedQueue<int> queue(2);

    TEST_ASSERT(queue.Push(1) == BoundedQueue<int>::PushResult::Pushed);
    TEST_ASSERT(queue.Push(2) == BoundedQueue<int>::PushResult::Pushed);
    TEST_ASSERT(queue.Push(3) ==
        BoundedQueue<int>::PushResult::ReplacedOldest);
    TEST_ASSERT(queue.Push(4) ==
        BoundedQueue<int>::PushResult::ReplacedOldest);

    TEST_ASSERT(queue.Size() == 2);
    TEST_ASSERT(queue.TotalPushed() == 4);
    TEST_ASSERT(queue.TotalDropped() == 2);

    int value = 0;
    TEST_ASSERT(queue.TryPop(value) && value == 3);
    TEST_ASSERT(queue.TryPop(value) && value == 4);
    TEST_ASSERT(!queue.TryPop(value));
}

void TestWaitPopTimeout() {
    std::printf("TestWaitPopTimeout\n");
    BoundedQueue<int> queue(2);

    int value = 0;
    const auto begin = std::chrono::steady_clock::now();
    const bool popped = queue.WaitPopFor(value, std::chrono::milliseconds(20));
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    TEST_ASSERT(!popped);
    TEST_ASSERT(elapsed >= std::chrono::milliseconds(15));
}

void TestWaitPopWakesProducer() {
    std::printf("TestWaitPopWakesProducer\n");
    BoundedQueue<int> queue(2);

    std::thread producer([&queue] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        queue.Push(42);
    });

    int value = 0;
    const bool popped = queue.WaitPopFor(value, std::chrono::milliseconds(500));
    producer.join();

    TEST_ASSERT(popped);
    TEST_ASSERT(value == 42);
}

void TestCloseRejectsAndDrains() {
    std::printf("TestCloseRejectsAndDrains\n");
    BoundedQueue<int> queue(4);
    queue.Push(7);

    queue.Close();
    TEST_ASSERT(queue.IsClosed());
    TEST_ASSERT(queue.Push(8) == BoundedQueue<int>::PushResult::Rejected);
    TEST_ASSERT(queue.TotalRejected() == 1);

    int value = 0;
    TEST_ASSERT(queue.WaitPopFor(value, std::chrono::milliseconds(10)));
    TEST_ASSERT(value == 7);
    TEST_ASSERT(!queue.WaitPopFor(value, std::chrono::milliseconds(10)));
}

void TestClearCountsDrops() {
    std::printf("TestClearCountsDrops\n");
    BoundedQueue<int> queue(4);
    queue.Push(1);
    queue.Push(2);
    queue.Push(3);

    queue.Clear();
    TEST_ASSERT(queue.Size() == 0);
    TEST_ASSERT(queue.TotalDropped() == 3);
}

// ============================================================
// Frame queue accounting under concurrent load
// ============================================================

void TestFrameQueueAccounting() {
    std::printf("TestFrameQueueAccounting\n");
    FrameQueue queue(3);

    constexpr int kFrames = 2000;
    std::thread producer([&queue] {
        for (int i = 0; i < kFrames; ++i) {
            Frame frame;
            frame.format = FrameFormat::NV12;
            frame.width  = 16;
            frame.height = 16;
            frame.stride = 16;
            frame.timestamp = i;
            frame.data.resize(16 * 16 * 3 / 2);
            queue.Push(std::move(frame));
        }
    });

    uint64_t consumed = 0;
    Frame frame;
    for (;;) {
        if (queue.WaitPopFor(frame, std::chrono::milliseconds(50))) {
            ++consumed;
            continue;
        }
        // Timeout: producer is done once all pushes are accounted for.
        if (queue.TotalPushed() == kFrames && queue.Size() == 0) {
            break;
        }
    }
    producer.join();

    TEST_ASSERT(queue.TotalPushed() == kFrames);
    TEST_ASSERT(consumed + queue.TotalDropped() + queue.Size() == kFrames);
    TEST_ASSERT(queue.TotalPopped() == consumed);
}

int main() {
    std::printf("=== Frame queue tests ===\n");

    TestFifoOrder();
    TestDropOldestWhenFull();
    TestWaitPopTimeout();
    TestWaitPopWakesProducer();
    TestCloseRejectsAndDrains();
    TestClearCountsDrops();
    TestFrameQueueAccounting();

    std::printf("\n%d/%d assertions passed\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
