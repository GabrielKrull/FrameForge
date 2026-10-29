#include "diagnostics/Logger.h"

#include <chrono>
#include <format>

namespace frameforge {

Logger::Logger(std::ostream& output)
    : m_output(output) {
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard lock(m_mutex);
    m_level = level;
}

LogLevel Logger::GetLevel() const {
    std::lock_guard lock(m_mutex);
    return m_level;
}

void Logger::Log(LogLevel level, std::string_view source, std::string_view message) {
    std::lock_guard lock(m_mutex);
    if (level < m_level) return;

    m_output << std::format("[{}] [{}] [{}] {}\n",
        FormatTimestamp(),
        LevelToString(level),
        source,
        message);
    m_output.flush();
}

void Logger::Debug(std::string_view source, std::string_view message) {
    Log(LogLevel::Debug, source, message);
}

void Logger::Info(std::string_view source, std::string_view message) {
    Log(LogLevel::Info, source, message);
}

void Logger::Warn(std::string_view source, std::string_view message) {
    Log(LogLevel::Warn, source, message);
}

void Logger::Error(std::string_view source, std::string_view message) {
    Log(LogLevel::Error, source, message);
}

Logger& Logger::Global() {
    static Logger instance(std::cerr);
    return instance;
}

std::string Logger::FormatTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
    localtime_s(&tm_buf, &time_t_now);

    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
        static_cast<int>(ms.count()));
}

std::string_view Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
    }
}

} // namespace frameforge
