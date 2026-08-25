#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

enum class LogLevel : std::uint8_t
{
    Info = 0,
    Success,
    Warning,
    Error
};

struct LogEntry
{
    std::string message{};
    std::string timestamp{};
    LogLevel level{ LogLevel::Info };
};

class Logger
{
public:
    [[nodiscard]] static Logger& Instance() noexcept
    {
        static Logger s_instance{};
        return s_instance;
    }

    void Log(std::string_view message, LogLevel level = LogLevel::Info)
    {
        const auto now = std::chrono::system_clock::now();
        const auto timeT = std::chrono::system_clock::to_time_t(now);

        std::tm tmBuffer{};
        localtime_s(&tmBuffer, &timeT);

        std::ostringstream ss{};
        ss << std::put_time(&tmBuffer, "%H:%M:%S");

        m_entries.emplace_back(LogEntry{ std::string{ message }, ss.str(), level });
    }

    void Clear() noexcept { m_entries.clear(); }
    [[nodiscard]] const std::vector<LogEntry>& GetEntries() const noexcept { return m_entries; }

private:
    Logger() = default;
    std::vector<LogEntry> m_entries{};
};

// Convenience namespace helpers
namespace Log
{
    inline void Info(std::string_view msg) { Logger::Instance().Log(msg, LogLevel::Info); }
    inline void Success(std::string_view msg) { Logger::Instance().Log(msg, LogLevel::Success); }
    inline void Warn(std::string_view msg) { Logger::Instance().Log(msg, LogLevel::Warning); }
    inline void Error(std::string_view msg) { Logger::Instance().Log(msg, LogLevel::Error); }
}