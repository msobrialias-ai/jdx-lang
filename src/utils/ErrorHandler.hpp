#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <sstream>

namespace jdx::lexer { struct Token; }

namespace jdx::utils {

struct SourceView {
    std::string filename;
    std::vector<std::string> lines;
};

class SourceRegistry {
public:
    static SourceRegistry& instance();
    void registerSource(const std::string& filename, const std::string& source);
    const std::vector<std::string>* getLines(const std::string& filename) const;
private:
    std::unordered_map<std::string, std::vector<std::string>> sources_;
};

class DiagnosticError : public std::runtime_error {
public:
    explicit DiagnosticError(const std::string& message) : std::runtime_error(message) {}
};

void pushStackFrame(const std::string& frame);
void popStackFrame();
std::vector<std::string> currentStackTrace();

std::string makeCaretDiagnostic(const std::string& kind,
                                const std::string& filename,
                                std::size_t line,
                                std::size_t column,
                                const std::string& offendingLine,
                                const std::string& message);

void raiseSyntaxError(const std::string& filename,
                      std::size_t line,
                      std::size_t column,
                      const std::string& message);

void raiseRuntimeError(const std::string& filename,
                       std::size_t line,
                       std::size_t column,
                       const std::string& message,
                       const std::vector<std::string>& stackTrace = {});

} // namespace jdx::utils
