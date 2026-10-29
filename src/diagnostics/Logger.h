#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <fstream>
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

    // Optional file sink. Lines are still written to the console stream.
    //
    // maxBytes > 0 enables size-based rotation; maxFiles is the number of
    // rotated archives kept (path.1 .. path.N). maxFiles <= 0 disables
    // archives and simply truncates on rotation.
    //
    // Returns false when the file cannot be opened for writing.
    bool SetFileOutput(const std::string& path,
                       std::uintmax_t maxBytes = 4u * 1024u * 1024u,
                       int maxFiles = 3);
    void CloseFileOutput();
    bool HasFileOutput() const;
    std::string FilePath() const;

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
    void RotateFileLocked();

    mutable std::mutex m_mutex;
    std::ostream& m_output;
    LogLevel m_level = LogLevel::Info;

    std::ofstream m_file;
    std::string   m_filePath;
    std::uintmax_t m_maxFileBytes = 4u * 1024u * 1024u;
    std::uintmax_t m_fileBytes = 0;
    int m_maxFiles = 3;
};

} // namespace frameforge
