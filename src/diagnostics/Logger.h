#pragma once

#include <string>
#include <string_view>
#include <mutex>
#include <ostream>
#include <iostream>

namespace frameforge {

enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

class Logger {
public:
    // Construct with a specific output stream (default: stderr)
    explicit Logger(std::ostream& output = std::cerr);
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Set the minimum log level (messages below this level are discarded)
    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;

    // Log a message with explicit level and source component
    void Log(LogLevel level, std::string_view source, std::string_view message);

    // Convenience methods
    void Debug(std::string_view source, std::string_view message);
    void Info(std::string_view source, std::string_view message);
    void Warn(std::string_view source, std::string_view message);
    void Error(std::string_view source, std::string_view message);

    // Global logger instance (writes to stderr)
    static Logger& Global();

private:
    std::string FormatTimestamp() const;
    static std::string_view LevelToString(LogLevel level);

    mutable std::mutex m_mutex;
    std::ostream& m_output;
    LogLevel m_level = LogLevel::Info;
};

} // namespace frameforge
