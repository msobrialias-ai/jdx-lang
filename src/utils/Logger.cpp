#include "utils/Logger.hpp"
#include <iostream>

namespace jdx::utils {

namespace {
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
    return ansiCode(level) + std::string(prefix(level)) + " " + std::string(message) + "\x1b[0m";
}

void Logger::write(const LogLevel level, std::string_view message, std::ostream& stream) {
    stream << decorate(level, message) << '\n';
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
