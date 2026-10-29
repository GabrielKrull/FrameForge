#include "diagnostics/Logger.h"

#include <cassert>
#include <cstdio>
#include <format>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace frameforge;

static int g_total = 0;
static int g_passed = 0;

#define TEST_ASSERT(expr) do { \
    ++g_total; \
    if (expr) { ++g_passed; } \
    else { std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

// ============================================================
// Basic logging
// ============================================================

void TestBasicLogging() {
    std::printf("TestBasicLogging\n");
    std::ostringstream oss;
    Logger logger(oss);

    logger.Info("Test", "Hello World");

    std::string output = oss.str();
    TEST_ASSERT(output.find("Hello World") != std::string::npos);
    TEST_ASSERT(output.find("[INFO ]") != std::string::npos);
    TEST_ASSERT(output.find("[Test]") != std::string::npos);
}

void TestLogLevels() {
    std::printf("TestLogLevels\n");
    std::ostringstream oss;
    Logger logger(oss);

    // Default level is Info; Debug messages should be filtered out
    logger.Debug("Test", "debug message");
    logger.Info("Test", "info message");
    logger.Warn("Test", "warn message");
    logger.Error("Test", "error message");

    std::string output = oss.str();
    TEST_ASSERT(output.find("debug message") == std::string::npos);
    TEST_ASSERT(output.find("info message")  != std::string::npos);
    TEST_ASSERT(output.find("warn message")  != std::string::npos);
    TEST_ASSERT(output.find("error message") != std::string::npos);
}

void TestSetLevel() {
    std::printf("TestSetLevel\n");
    std::ostringstream oss;
    Logger logger(oss);

    // Set to Warn; Info should be filtered out
    logger.SetLevel(LogLevel::Warn);
    logger.Info("Test", "info message");
    logger.Warn("Test", "warn message");
    logger.Error("Test", "error message");

    std::string output = oss.str();
    TEST_ASSERT(output.find("info message") == std::string::npos);
    TEST_ASSERT(output.find("warn message") != std::string::npos);
    TEST_ASSERT(output.find("error message") != std::string::npos);
}

void TestSetLevelDebug() {
    std::printf("TestSetLevelDebug\n");
    std::ostringstream oss;
    Logger logger(oss);

    logger.SetLevel(LogLevel::Debug);
    logger.Debug("Test", "debug message");
    logger.Info("Test", "info message");

    std::string output = oss.str();
    TEST_ASSERT(output.find("debug message") != std::string::npos);
    TEST_ASSERT(output.find("info message")  != std::string::npos);
}

void TestLevelLabels() {
    std::printf("TestLevelLabels\n");
    std::ostringstream oss;
    Logger logger(oss);

    logger.SetLevel(LogLevel::Debug);
    logger.Debug("Test", "msg");
    logger.Info("Test", "msg");
    logger.Warn("Test", "msg");
    logger.Error("Test", "msg");

    std::string output = oss.str();
    TEST_ASSERT(output.find("[DEBUG]") != std::string::npos);
    TEST_ASSERT(output.find("[INFO ]") != std::string::npos);
    TEST_ASSERT(output.find("[WARN ]") != std::string::npos);
    TEST_ASSERT(output.find("[ERROR]") != std::string::npos);
}

// ============================================================
// Timestamp format
// ============================================================

void TestTimestampFormat() {
    std::printf("TestTimestampFormat\n");
    std::ostringstream oss;
    Logger logger(oss);

    logger.Info("Test", "msg");
    std::string output = oss.str();

    // Timestamp format: [YYYY-MM-DD HH:MM:SS.mmm]
    // First char after '[' should be '2' (year starts with 2)
    TEST_ASSERT(output.size() > 2);
    TEST_ASSERT(output[1] == '2'); // year 20xx or 21xx
}

// ============================================================
// Thread safety
// ============================================================

void TestThreadSafety() {
    std::printf("TestThreadSafety\n");
    std::ostringstream oss;
    Logger logger(oss);

    const int numThreads = 4;
    const int msgsPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&logger, t, msgsPerThread]() {
            for (int i = 0; i < msgsPerThread; ++i) {
                logger.Info("Thread",
                    std::format("thread {} msg {}", t, i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Verify no data corruption: each message should be on its own line
    std::string output = oss.str();
    // Count newlines
    int newlineCount = 0;
    for (char c : output) {
        if (c == '\n') ++newlineCount;
    }

    // Each message should end with exactly one newline
    TEST_ASSERT(newlineCount == numThreads * msgsPerThread);

    // Spot check: verify "thread 0 msg 0" is present
    TEST_ASSERT(output.find("thread 0 msg 0") != std::string::npos);
    TEST_ASSERT(output.find(
        std::format("thread {} msg {}",
            numThreads - 1, msgsPerThread - 1)) != std::string::npos);
}

// ============================================================
// Empty message
// ============================================================

void TestEmptyMessage() {
    std::printf("TestEmptyMessage\n");
    std::ostringstream oss;
    Logger logger(oss);

    logger.Info("Test", "");

    std::string output = oss.str();
    // Should still produce a line with the source name
    TEST_ASSERT(output.find("[Test]") != std::string::npos);
}

// ============================================================
// GetLevel
// ============================================================

void TestGetLevel() {
    std::printf("TestGetLevel\n");
    std::ostringstream oss;
    Logger logger(oss);

    TEST_ASSERT(logger.GetLevel() == LogLevel::Info);

    logger.SetLevel(LogLevel::Error);
    TEST_ASSERT(logger.GetLevel() == LogLevel::Error);

    logger.SetLevel(LogLevel::Debug);
    TEST_ASSERT(logger.GetLevel() == LogLevel::Debug);
}

// ============================================================
// Global instance
// ============================================================

void TestGlobalInstance() {
    std::printf("TestGlobalInstance\n");
    // Global() should return the same object every time
    Logger& a = Logger::Global();
    Logger& b = Logger::Global();
    TEST_ASSERT(&a == &b);
}

// ============================================================
// Main
// ============================================================

int main() {
    std::printf("=== Logger Tests ===\n\n");

    TestBasicLogging();
    TestLogLevels();
    TestSetLevel();
    TestSetLevelDebug();
    TestLevelLabels();
    TestTimestampFormat();
    TestThreadSafety();
    TestEmptyMessage();
    TestGetLevel();
    TestGlobalInstance();

    std::printf("\n%d/%d assertions passed\n", g_passed, g_total);
    if (g_passed == g_total) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        std::printf("SOME TESTS FAILED\n");
        return 1;
    }
}
