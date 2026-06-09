
#pragma once

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace jdx::lexer {
struct Token;
}

namespace jdx::utils {

struct SourceView {
    std::string filename;
    std::vector<std::string> lines;
};

class SourceRegistry {
public:
    static SourceRegistry& instance();

    void registerSource(const std::string& filename, const std::string& source);
    [[nodiscard]] const std::vector<std::string>* getLines(const std::string& filename) const;

private:
    std::unordered_map<std::string, std::vector<std::string>> sources_;
};

class DiagnosticError final : public std::runtime_error {
public:
    explicit DiagnosticError(const std::string& message) : std::runtime_error(message) {}
};

class ErrorSettings final {
public:
    static ErrorSettings& instance();

    void setStacktraceLevel(std::size_t level);
    [[nodiscard]] std::size_t stacktraceLevel() const;

    void setStacktraceType(std::string type);
    [[nodiscard]] const std::string& stacktraceType() const;

private:
    std::size_t stacktraceLevel_ {8};
    std::string stacktraceType_ {"compact"};
};

void pushStackFrame(const std::string& frame);
void popStackFrame();
[[nodiscard]] std::vector<std::string> currentStackTrace();

[[nodiscard]] std::string makeCaretDiagnostic(const std::string& kind,
                                               const std::string& filename,
                                               std::size_t line,
                                               std::size_t column,
                                               const std::string& offendingLine,
                                               const std::string& message);

[[noreturn]] void raiseSyntaxError(const std::string& filename,
                                  std::size_t line,
                                  std::size_t column,
                                  const std::string& message);

[[noreturn]] void raiseRuntimeError(const std::string& filename,
                                   std::size_t line,
                                   std::size_t column,
                                   const std::string& message,
                                   const std::vector<std::string>& stackTrace = {});

} // namespace jdx::utils
