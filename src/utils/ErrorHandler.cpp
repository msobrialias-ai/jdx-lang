
#include "utils/ErrorHandler.hpp"

#include <algorithm>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <utility>

namespace jdx::utils {

SourceRegistry& SourceRegistry::instance() {
    static SourceRegistry registry;
    return registry;
}

void SourceRegistry::registerSource(const std::string& filename, const std::string& source) {
    std::vector<std::string> lines;
    std::string current;

    for (const char ch : source) {
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
    const auto it = sources_.find(filename);
    if (it == sources_.end()) {
        return nullptr;
    }
    return &it->second;
}

ErrorSettings& ErrorSettings::instance() {
    static ErrorSettings settings;
    return settings;
}

void ErrorSettings::setStacktraceLevel(std::size_t level) {
    stacktraceLevel_ = level;
}

std::size_t ErrorSettings::stacktraceLevel() const {
    return stacktraceLevel_;
}

void ErrorSettings::setStacktraceType(std::string type) {
    stacktraceType_ = std::move(type);
}

const std::string& ErrorSettings::stacktraceType() const {
    return stacktraceType_;
}

namespace {
thread_local std::vector<std::string> g_stackTrace;
std::string spaces(const std::size_t count) {
    return std::string(count, ' ');
}

std::string lineText(const std::string& filename, const std::size_t line) {
    const auto* lines = SourceRegistry::instance().getLines(filename);
    if (lines == nullptr || line == 0U || line > lines->size()) {
        return {};
    }
    return (*lines)[line - 1U];
}

std::vector<std::string> applyStackLimit(const std::vector<std::string>& trace) {
    const std::size_t limit = ErrorSettings::instance().stacktraceLevel();
    if (limit == 0U || trace.size() <= limit) {
        return trace;
    }
    return std::vector<std::string>(trace.end() - static_cast<std::ptrdiff_t>(limit), trace.end());
}
} // namespace

void pushStackFrame(const std::string& frame) {
    g_stackTrace.push_back(frame);
}

void popStackFrame() {
    if (!g_stackTrace.empty()) {
        g_stackTrace.pop_back();
    }
}

std::vector<std::string> currentStackTrace() {
    return g_stackTrace;
}

std::string makeCaretDiagnostic(const std::string& kind,
                                const std::string& filename,
                                const std::size_t line,
                                const std::size_t column,
                                const std::string& offendingLine,
                                const std::string& message) {
    std::ostringstream out;
    out << "  [" << kind << "] In file '" << filename << "' at line " << line
        << ", column " << column << ":\n";
    out << "  " << offendingLine << '\n';
    out << "  " << spaces(column > 0U ? column - 1U : 0U) << "^\n";
    out << "  Message: " << message << '\n';
    return out.str();
}

[[noreturn]] void raiseSyntaxError(const std::string& filename,
                                   const std::size_t line,
                                   const std::size_t column,
                                   const std::string& message) {
    throw DiagnosticError(makeCaretDiagnostic("Syntax Error",
                                              filename,
                                              line,
                                              column,
                                              lineText(filename, line),
                                              message));
}

[[noreturn]] void raiseRuntimeError(const std::string& filename,
                                    const std::size_t line,
                                    const std::size_t column,
                                    const std::string& message,
                                    const std::vector<std::string>& stackTrace) {
    std::ostringstream out;
    out << "  ======================================================================\n";
    out << "  RUNTIME ERROR: " << message << '\n';
    out << "  ======================================================================\n";

    const std::vector<std::string> frames = stackTrace.empty() ? currentStackTrace() : stackTrace;
    const std::vector<std::string> limited = applyStackLimit(frames);

    if (!limited.empty()) {
        out << "  Stack Trace (" << ErrorSettings::instance().stacktraceType() << "):\n";
        for (std::size_t i = 0; i < limited.size(); ++i) {
            out << "    " << (i + 1U) << ". " << limited[i] << '\n';
        }
    }

    out << makeCaretDiagnostic("Runtime Error",
                               filename,
                               line,
                               column,
                               lineText(filename, line),
                               message);
    out << "  ======================================================================\n";
    throw DiagnosticError(out.str());
}

} // namespace jdx::utils
