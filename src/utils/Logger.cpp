
#include "utils/Logger.hpp"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace jdx::utils {

namespace {
#if defined(_WIN32)
class ConsoleColorGuard final {
public:
    explicit ConsoleColorGuard(const LogLevel level) : handle_(GetStdHandle(STD_OUTPUT_HANDLE)) {
        if (handle_ == INVALID_HANDLE_VALUE) {
            return;
        }

        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(handle_, &info) == 0) {
            return;
        }

        original_ = info.wAttributes;
        const WORD color = (level == LogLevel::Log)
            ? FOREGROUND_GREEN | FOREGROUND_INTENSITY
            : (level == LogLevel::Warn)
                ? FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY
                : FOREGROUND_RED | FOREGROUND_INTENSITY;
        SetConsoleTextAttribute(handle_, color);
        active_ = true;
    }

    ~ConsoleColorGuard() {
        if (active_) {
            SetConsoleTextAttribute(handle_, original_);
        }
    }

    ConsoleColorGuard(const ConsoleColorGuard&) = delete;
    ConsoleColorGuard& operator=(const ConsoleColorGuard&) = delete;

private:
    HANDLE handle_ {INVALID_HANDLE_VALUE};
    WORD original_ {0};
    bool active_ {false};
};
#endif

std::string ansiCode(const LogLevel level) {
    switch (level) {
        case LogLevel::Log: return "\x1b[32m";
        case LogLevel::Warn: return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
    }
    return "\x1b[0m";
}

const char* prefix(const LogLevel level) {
    switch (level) {
        case LogLevel::Log: return "[log]";
        case LogLevel::Warn: return "[warn]";
        case LogLevel::Error: return "[error]";
    }
    return "[log]";
}
} // namespace

std::string Logger::decorate(const LogLevel level, std::string_view message) {
#if defined(_WIN32)
    (void)level;
    return std::string(prefix(level)) + " " + std::string(message);
#else
    return ansiCode(level) + std::string(prefix(level)) + " " + std::string(message) + "\x1b[0m";
#endif
}

void Logger::write(const LogLevel level, std::string_view message, std::ostream& stream) {
#if defined(_WIN32)
    ConsoleColorGuard guard(level);
    stream << prefix(level) << ' ' << message << '\n';
#else
    stream << decorate(level, message) << '\n';
#endif
}

void Logger::log(std::string_view message) {
    write(LogLevel::Log, message, std::clog);
}

void Logger::warn(std::string_view message) {
    write(LogLevel::Warn, message, std::clog);
}

void Logger::error(std::string_view message) {
    write(LogLevel::Error, message, std::cerr);
}

} // namespace jdx::utils
