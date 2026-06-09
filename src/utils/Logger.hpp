
#pragma once

#include <ostream>
#include <string>
#include <string_view>

namespace jdx::utils {

enum class LogLevel {
    Log,
    Warn,
    Error
};

class Logger final {
public:
    static std::string decorate(LogLevel level, std::string_view message);
    static void write(LogLevel level, std::string_view message, std::ostream& stream);

    static void log(std::string_view message);
    static void warn(std::string_view message);
    static void error(std::string_view message);
};

} // namespace jdx::utils
