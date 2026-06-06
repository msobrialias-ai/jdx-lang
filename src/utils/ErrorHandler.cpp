#include "utils/ErrorHandler.hpp"
#include <algorithm>
#include <sstream>

namespace jdx::utils {

SourceRegistry& SourceRegistry::instance() {
    static SourceRegistry registry;
    return registry;
}

void SourceRegistry::registerSource(const std::string& filename, const std::string& source) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : source) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (ch != '\r') {
            current.push_back(ch);
        }
    }
    lines.push_back(current);
    sources_[filename] = std::move(lines);
}

const std::vector<std::string>* SourceRegistry::getLines(const std::string& filename) const {
    auto it = sources_.find(filename);
    if (it == sources_.end()) return nullptr;
    return &it->second;
}

namespace {
thread_local std::vector<std::string> g_stackTrace;
std::string spaces(std::size_t n) { return std::string(n, ' '); }
}

void pushStackFrame(const std::string& frame) { g_stackTrace.push_back(frame); }
void popStackFrame() { if (!g_stackTrace.empty()) g_stackTrace.pop_back(); }
std::vector<std::string> currentStackTrace() { return g_stackTrace; }

std::string makeCaretDiagnostic(const std::string& kind,
                                const std::string& filename,
                                std::size_t line,
                                std::size_t column,
                                const std::string& offendingLine,
                                const std::string& message) {
    std::ostringstream out;
    out << "  [" << kind << "] In file '" << filename << "' at line " << line
        << ", column " << column << ":\n";
    out << "  " << offendingLine << '\n';
    out << "  " << spaces(column > 0 ? column - 1 : 0) << "^\n";
    out << "  Message: " << message << '\n';
    return out.str();
}

static std::string getLineText(const std::string& filename, std::size_t line) {
    const auto* lines = SourceRegistry::instance().getLines(filename);
    if (!lines || line == 0 || line > lines->size()) return {};
    return (*lines)[line - 1];
}

void raiseSyntaxError(const std::string& filename,
                      std::size_t line,
                      std::size_t column,
                      const std::string& message) {
    throw DiagnosticError(makeCaretDiagnostic("Syntax Error", filename, line, column, getLineText(filename, line), message));
}

void raiseRuntimeError(const std::string& filename,
                       std::size_t line,
                       std::size_t column,
                       const std::string& message,
                       const std::vector<std::string>& stackTrace) {
    const auto trace = stackTrace.empty() ? currentStackTrace() : stackTrace;
    std::ostringstream out;
    out << "  ======================================================================\n";
    out << "  RUNTIME ERROR: " << message << '\n';
    out << "  ======================================================================\n";
    if (!trace.empty()) {
        out << "  Stack Trace (most recent call first):\n";
        for (auto it = trace.rbegin(); it != trace.rend(); ++it) out << "    " << *it << '\n';
    }
    const std::string offending = getLineText(filename, line);
    out << makeCaretDiagnostic("Runtime Error", filename, line, column, offending, message);
    out << "  ======================================================================\n";
    throw DiagnosticError(out.str());
}

} // namespace jdx::utils
