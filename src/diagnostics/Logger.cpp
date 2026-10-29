#include "diagnostics/Logger.h"

#include <chrono>
#include <filesystem>
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

    const std::string line = std::format("[{}] [{}] [{}] {}\n",
        FormatTimestamp(),
        LevelToString(level),
        source,
        message);

    m_output << line;
    m_output.flush();

    if (m_file.is_open()) {
        if (m_maxFileBytes > 0 && m_fileBytes > 0 &&
            m_fileBytes + line.size() > m_maxFileBytes) {
            RotateFileLocked();
        }

        if (m_file.is_open()) {
            m_file.write(line.data(), static_cast<std::streamsize>(line.size()));
            m_file.flush();
            m_fileBytes += line.size();
        }
    }
}

bool Logger::SetFileOutput(const std::string& path,
                           std::uintmax_t maxBytes,
                           int maxFiles) {
    std::lock_guard lock(m_mutex);

    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }

    m_filePath     = path;
    m_maxFileBytes = maxBytes;
    m_maxFiles     = maxFiles < 0 ? 0 : maxFiles;
    m_fileBytes    = 0;

    if (path.empty()) {
        m_filePath.clear();
        return false;
    }

    m_file.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!m_file.is_open()) {
        m_filePath.clear();
        return false;
    }

    return true;
}

void Logger::CloseFileOutput() {
    std::lock_guard lock(m_mutex);
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
    m_filePath.clear();
    m_fileBytes = 0;
}

bool Logger::HasFileOutput() const {
    std::lock_guard lock(m_mutex);
    return m_file.is_open();
}

std::string Logger::FilePath() const {
    std::lock_guard lock(m_mutex);
    return m_filePath;
}

void Logger::RotateFileLocked() {
    if (!m_file.is_open() && m_filePath.empty()) {
        return;
    }

    m_file.flush();
    m_file.close();

    namespace fs = std::filesystem;
    std::error_code ec;

    if (m_maxFiles <= 0) {
        m_file.open(m_filePath,
                    std::ios::out | std::ios::trunc | std::ios::binary);
        m_fileBytes = 0;
        return;
    }

    // Drop the oldest archive, then shift .N-1 .. .1 up by one.
    fs::remove(fs::path(m_filePath + "." + std::to_string(m_maxFiles)), ec);

    for (int i = m_maxFiles - 1; i >= 1; --i) {
        const fs::path from = m_filePath + "." + std::to_string(i);
        const fs::path to   = m_filePath + "." + std::to_string(i + 1);
        fs::remove(to, ec);
        if (fs::exists(from, ec)) {
            fs::rename(from, to, ec);
        }
    }

    const fs::path first = m_filePath + ".1";
    fs::remove(first, ec);
    fs::rename(fs::path(m_filePath), first, ec);

    m_file.open(m_filePath,
                std::ios::out | std::ios::trunc | std::ios::binary);
    m_fileBytes = 0;
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
