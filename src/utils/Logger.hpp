#pragma once
#include <iostream>
#include <string>

namespace jdx::utils {

class Logger {
public:
    static void info(const std::string& msg) { std::clog << "[info] " << msg << '\n'; }
    static void warn(const std::string& msg) { std::clog << "[warn] " << msg << '\n'; }
    static void error(const std::string& msg) { std::cerr << "[error] " << msg << '\n'; }
};

} // namespace jdx::utils
