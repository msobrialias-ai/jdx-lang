
#include "runtime/SystemRuntime.hpp"

#include "interpreter/Environment.hpp"
#include "interpreter/Interpreter.hpp"
#include "modules/ModuleManager.hpp"
#include "utils/ErrorHandler.hpp"
#include "utils/Logger.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <ctime>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace jdx::runtime {
namespace {

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string toUpperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string getEnvOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : fallback;
}

std::string platformString() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#elif defined(__unix__)
    return "Unix";
#else
    return "Unknown";
#endif
}

std::string archString() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__)
    return "ARM64";
#elif defined(__arm__) || defined(_M_ARM)
    return "ARM";
#elif defined(__loongarch64) || defined(__loongarch_lp64)
    return "LoongArch64";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "RISC-V64";
#elif defined(__riscv) && (__riscv_xlen == 32)
    return "RISC-V32";
#elif defined(__powerpc64__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return "PowerPC64LE";
    #else
        return "PowerPC64";
    #endif
#else
    return "Unknown";
#endif
}

std::string compilerString() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "Unknown";
#endif
}

std::string endianString() {
    const std::uint16_t value = 1U;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    return (bytes[0] == 1U) ? "Little Endian" : "Big Endian";
}

std::string formatTimePoint(const std::chrono::system_clock::time_point& point) {
    const std::time_t raw = std::chrono::system_clock::to_time_t(point);
    std::tm tm{};
    gmtime_r(&raw, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
    return out.str();
}

std::string homeDirectory() {
    return getEnvOr("HOME", ".");
}

std::string rootDirectory() {
    return "/";
}

std::string tempDirectory() {
    return fs::temp_directory_path().string();
}

std::string cwdString() {
    return fs::current_path().string();
}

runtime::Value makeErrorResult(const std::string& message) {
    auto result = std::make_shared<runtime::Object>("System.SafeExec.Result");
    result->properties.emplace("ok", false);
    result->properties.emplace("error", message);
    result->properties.emplace("value", runtime::makeNull());
    return runtime::Value(result);
}

runtime::Value makeOkResult(const runtime::Value& value) {
    auto result = std::make_shared<runtime::Object>("System.SafeExec.Result");
    result->properties.emplace("ok", true);
    result->properties.emplace("error", runtime::makeNull());
    result->properties.emplace("value", value);
    return runtime::Value(result);
}

struct JGexNode {
    enum class Kind {
        Empty,
        Literal,
        Any,
        CharClass,
        Sequence,
        Alternate,
        Repeat,
        StartAnchor,
        EndAnchor
    };

    struct CharRange {
        char first {0};
        char last {0};
    };

    Kind kind {Kind::Empty};
    char literal {0};
    bool negated {false};
    std::vector<CharRange> ranges;
    std::vector<char> singles;
    std::vector<std::shared_ptr<JGexNode>> children;
    std::shared_ptr<JGexNode> child;
    std::size_t minRepeat {0};
    std::size_t maxRepeat {0};
};

class JGexPattern final {
public:
    explicit JGexPattern(std::string pattern) : pattern_(std::move(pattern)) {
        position_ = 0U;
        root_ = parseExpression();
        if (position_ != pattern_.size()) {
            throw std::runtime_error("Invalid JGex pattern near position " + std::to_string(position_ + 1U) + ".");
        }
    }

    [[nodiscard]] const std::string& pattern() const {
        return pattern_;
    }

    [[nodiscard]] bool fullMatch(const std::string& input) const {
        Memo memo;
        const auto ends = matchNode(root_, input, 0U, memo);
        for (const std::size_t end : ends) {
            if (end == input.size()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool prefixMatch(const std::string& input) const {
        Memo memo;
        const auto ends = matchNode(root_, input, 0U, memo);
        return !ends.empty();
    }

    struct SearchResult {
        std::size_t index {0};
        std::size_t length {0};
        std::string match;
    };

    [[nodiscard]] std::optional<SearchResult> search(const std::string& input) const {
        std::string effectivePattern = pattern_;
        if (!effectivePattern.empty() && effectivePattern.front() == '^') {
            effectivePattern.erase(effectivePattern.begin());
        }
        if (!effectivePattern.empty() && effectivePattern.back() == '$') {
            effectivePattern.pop_back();
        }
        const JGexPattern matcher(effectivePattern);
        for (std::size_t start = 0U; start <= input.size(); ++start) {
            Memo memo;
            const auto ends = matcher.matchNode(matcher.root_, input, start, memo);
            std::size_t bestEnd = 0U;
            bool found = false;
            for (const std::size_t end : ends) {
                if (end >= start) {
                    if (!found || end > bestEnd) {
                        bestEnd = end;
                        found = true;
                    }
                }
            }
            if (found) {
                return SearchResult{start, bestEnd - start, input.substr(start, bestEnd - start)};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string replaceAll(const std::string& input, const std::string& replacement) const {
        std::string effectivePattern = pattern_;
        if (!effectivePattern.empty() && effectivePattern.front() == '^') {
            effectivePattern.erase(effectivePattern.begin());
        }
        if (!effectivePattern.empty() && effectivePattern.back() == '$') {
            effectivePattern.pop_back();
        }
        const JGexPattern matcher(effectivePattern);
        std::string output;
        std::size_t index = 0U;
        while (index < input.size()) {
            Memo memo;
            const auto ends = matcher.matchNode(matcher.root_, input, index, memo);
            std::size_t bestEnd = 0U;
            bool found = false;
            for (const std::size_t end : ends) {
                if (end >= index) {
                    if (!found || end > bestEnd) {
                        bestEnd = end;
                        found = true;
                    }
                }
            }
            if (!found || bestEnd == index) {
                output.push_back(input[index]);
                ++index;
                continue;
            }
            output += replacement;
            index = bestEnd;
        }
        return output;
    }

    [[nodiscard]] std::vector<std::string> split(const std::string& input) const {
        std::string effectivePattern = pattern_;
        if (!effectivePattern.empty() && effectivePattern.front() == '^') {
            effectivePattern.erase(effectivePattern.begin());
        }
        if (!effectivePattern.empty() && effectivePattern.back() == '$') {
            effectivePattern.pop_back();
        }
        const JGexPattern matcher(effectivePattern);
        std::vector<std::string> parts;
        std::size_t start = 0U;
        std::size_t index = 0U;
        while (index < input.size()) {
            Memo memo;
            const auto ends = matcher.matchNode(matcher.root_, input, index, memo);
            std::size_t bestEnd = 0U;
            bool found = false;
            for (const std::size_t end : ends) {
                if (end >= index) {
                    if (!found || end > bestEnd) {
                        bestEnd = end;
                        found = true;
                    }
                }
            }
            if (!found || bestEnd == index) {
                ++index;
                continue;
            }
            parts.push_back(input.substr(start, index - start));
            start = bestEnd;
            index = bestEnd;
        }
        parts.push_back(input.substr(start));
        return parts;
    }

private:
    using MemoKey = std::pair<const JGexNode*, std::size_t>;

    struct MemoHash {
        std::size_t operator()(const MemoKey& key) const noexcept {
            const auto p = reinterpret_cast<std::uintptr_t>(key.first);
            return std::hash<std::uintptr_t>{}(p) ^ (std::hash<std::size_t>{}(key.second) << 1U);
        }
    };

    struct MemoEqual {
        bool operator()(const MemoKey& lhs, const MemoKey& rhs) const noexcept {
            return lhs.first == rhs.first && lhs.second == rhs.second;
        }
    };

    using Memo = std::unordered_map<MemoKey, std::vector<std::size_t>, MemoHash, MemoEqual>;
    std::string pattern_;
    std::size_t position_ {0U};
    std::shared_ptr<JGexNode> root_;

    [[nodiscard]] char peek() const {
        return position_ < pattern_.size() ? pattern_[position_] : '\0';
    }

    [[nodiscard]] char peekNext() const {
        return (position_ + 1U < pattern_.size()) ? pattern_[position_ + 1U] : '\0';
    }

    char advance() {
        return pattern_[position_++];
    }

    bool match(char expected) {
        if (peek() != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool atEnd() const {
        return position_ >= pattern_.size();
    }

    std::shared_ptr<JGexNode> makeNode(JGexNode::Kind kind) {
        auto node = std::make_shared<JGexNode>();
        node->kind = kind;
        return node;
    }

    std::shared_ptr<JGexNode> parseExpression() {
        auto left = parseSequence();
        std::vector<std::shared_ptr<JGexNode>> alternates;
        alternates.push_back(left);
        while (match('|')) {
            alternates.push_back(parseSequence());
        }
        if (alternates.size() == 1U) {
            return left;
        }
        auto node = makeNode(JGexNode::Kind::Alternate);
        node->children = std::move(alternates);
        return node;
    }

    std::shared_ptr<JGexNode> parseSequence() {
        std::vector<std::shared_ptr<JGexNode>> parts;
        while (!atEnd() && peek() != ')' && peek() != '|') {
            parts.push_back(parseRepeat());
        }
        if (parts.empty()) {
            return makeNode(JGexNode::Kind::Empty);
        }
        if (parts.size() == 1U) {
            return parts.front();
        }
        auto node = makeNode(JGexNode::Kind::Sequence);
        node->children = std::move(parts);
        return node;
    }

    std::shared_ptr<JGexNode> parseRepeat() {
        auto atom = parseAtom();
        if (atEnd()) {
            return atom;
        }

        if (peek() == '*') {
            advance();
            auto node = makeNode(JGexNode::Kind::Repeat);
            node->child = atom;
            node->minRepeat = 0U;
            node->maxRepeat = std::numeric_limits<std::size_t>::max();
            return node;
        }
        if (peek() == '+') {
            advance();
            auto node = makeNode(JGexNode::Kind::Repeat);
            node->child = atom;
            node->minRepeat = 1U;
            node->maxRepeat = std::numeric_limits<std::size_t>::max();
            return node;
        }
        if (peek() == '?') {
            advance();
            auto node = makeNode(JGexNode::Kind::Repeat);
            node->child = atom;
            node->minRepeat = 0U;
            node->maxRepeat = 1U;
            return node;
        }
        return atom;
    }

    std::shared_ptr<JGexNode> parseCharClass() {
        auto node = makeNode(JGexNode::Kind::CharClass);
        if (match('^')) {
            node->negated = true;
        }

        while (!atEnd() && peek() != ']') {
            char first = advance();
            if (first == '\\' && !atEnd()) {
                first = translateEscape(advance());
                node->singles.push_back(first);
                continue;
            }
            if (peek() == '-' && peekNext() != ']' && peekNext() != '\0') {
                advance();
                char last = advance();
                if (last == '\\' && !atEnd()) {
                    last = translateEscape(advance());
                }
                if (last < first) {
                    std::swap(first, last);
                }
                node->ranges.push_back({first, last});
            } else {
                node->singles.push_back(first);
            }
        }

        if (!match(']')) {
            throw std::runtime_error("Unterminated JGex character class.");
        }
        return node;
    }

    std::shared_ptr<JGexNode> parseAtom() {
        if (atEnd()) {
            return makeNode(JGexNode::Kind::Empty);
        }

        const char ch = advance();
        switch (ch) {
            case '(':
                {
                    auto node = parseExpression();
                    if (!match(')')) {
                        throw std::runtime_error("Unterminated JGex group.");
                    }
                    return node;
                }
            case '.': return makeNode(JGexNode::Kind::Any);
            case '^': return makeNode(JGexNode::Kind::StartAnchor);
            case '$': return makeNode(JGexNode::Kind::EndAnchor);
            case '[': return parseCharClass();
            case ':': return parseNamedClass();
            case '\\':
                if (atEnd()) {
                    throw std::runtime_error("Invalid JGex escape at end of pattern.");
                }
                return literalNode(translateEscape(advance()));
            default:
                return literalNode(ch);
        }
    }

    std::shared_ptr<JGexNode> literalNode(const char ch) {
        auto node = makeNode(JGexNode::Kind::Literal);
        node->literal = ch;
        return node;
    }

    std::shared_ptr<JGexNode> parseNamedClass() {
        const std::string name = parseName();
        auto node = makeNode(JGexNode::Kind::CharClass);
        if (name == "digit") {
            node->ranges.push_back({'0', '9'});
        } else if (name == "alpha") {
            node->ranges.push_back({'a', 'z'});
            node->ranges.push_back({'A', 'Z'});
        } else if (name == "alnum") {
            node->ranges.push_back({'0', '9'});
            node->ranges.push_back({'a', 'z'});
            node->ranges.push_back({'A', 'Z'});
        } else if (name == "space") {
            node->singles = {' ', '\t', '\n', '\r', '\f', '\v'};
        } else if (name == "word") {
            node->ranges.push_back({'0', '9'});
            node->ranges.push_back({'a', 'z'});
            node->ranges.push_back({'A', 'Z'});
            node->singles.push_back('_');
        } else {
            throw std::runtime_error("Unknown JGex named class ':" + name + "'.");
        }
        return node;
    }

    [[nodiscard]] std::string parseName() {
        std::string name;
        while (!atEnd() && (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_')) {
            name.push_back(advance());
        }
        if (name.empty()) {
            throw std::runtime_error("Expected a JGex named class after ':'.");
        }
        return name;
    }

    static char translateEscape(const char ch) {
        switch (ch) {
            case 'n': return '\n';
            case 'r': return '\r';
            case 't': return '\t';
            case 'd': return 'd';
            case 's': return 's';
            case 'w': return 'w';
            case '\\': return '\\';
            case '[': return '[';
            case ']': return ']';
            case '(': return '(';
            case ')': return ')';
            case '{': return '{';
            case '}': return '}';
            case '.': return '.';
            case '*': return '*';
            case '+': return '+';
            case '?': return '?';
            case '|': return '|';
            case '^': return '^';
            case '$': return '$';
            default: return ch;
        }
    }

    static bool matchesCharClass(const JGexNode& node, const char ch) {
        bool matched = false;
        for (const char candidate : node.singles) {
            if (candidate == ch) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            for (const auto& range : node.ranges) {
                if (ch >= range.first && ch <= range.last) {
                    matched = true;
                    break;
                }
            }
        }
        return node.negated ? !matched : matched;
    }

    std::vector<std::size_t> matchNode(const std::shared_ptr<JGexNode>& node,
                                       const std::string& input,
                                       std::size_t start,
                                       std::unordered_map<MemoKey, std::vector<std::size_t>, MemoHash, MemoEqual>& memo) const {
        const MemoKey key{node.get(), start};
        const auto found = memo.find(key);
        if (found != memo.end()) {
            return found->second;
        }

        std::vector<std::size_t> result;
        switch (node->kind) {
            case JGexNode::Kind::Empty:
                result.push_back(start);
                break;
            case JGexNode::Kind::Literal:
                if (start < input.size() && input[start] == node->literal) {
                    result.push_back(start + 1U);
                }
                break;
            case JGexNode::Kind::Any:
                if (start < input.size()) {
                    result.push_back(start + 1U);
                }
                break;
            case JGexNode::Kind::CharClass:
                if (start < input.size() && matchesCharClass(*node, input[start])) {
                    result.push_back(start + 1U);
                }
                break;
            case JGexNode::Kind::StartAnchor:
                if (start == 0U) {
                    result.push_back(start);
                }
                break;
            case JGexNode::Kind::EndAnchor:
                if (start == input.size()) {
                    result.push_back(start);
                }
                break;
            case JGexNode::Kind::Sequence:
                {
                    std::vector<std::size_t> positions{start};
                    for (const auto& child : node->children) {
                        std::vector<std::size_t> next;
                        for (const std::size_t pos : positions) {
                            const auto matched = matchNode(child, input, pos, memo);
                            for (const std::size_t end : matched) {
                                if (end >= pos) {
                                    next.push_back(end);
                                }
                            }
                        }
                        std::sort(next.begin(), next.end());
                        next.erase(std::unique(next.begin(), next.end()), next.end());
                        positions = std::move(next);
                        if (positions.empty()) {
                            break;
                        }
                    }
                    result = std::move(positions);
                }
                break;
            case JGexNode::Kind::Alternate:
                {
                    for (const auto& child : node->children) {
                        const auto matched = matchNode(child, input, start, memo);
                        result.insert(result.end(), matched.begin(), matched.end());
                    }
                    std::sort(result.begin(), result.end());
                    result.erase(std::unique(result.begin(), result.end()), result.end());
                }
                break;
            case JGexNode::Kind::Repeat:
                {
                    std::vector<std::size_t> frontier{start};
                    if (node->minRepeat == 0U) {
                        result.push_back(start);
                    }
                    for (std::size_t count = 1U; count <= node->maxRepeat; ++count) {
                        std::vector<std::size_t> next;
                        for (const std::size_t pos : frontier) {
                            const auto matched = matchNode(node->child, input, pos, memo);
                            for (const std::size_t end : matched) {
                                if (end > pos) {
                                    next.push_back(end);
                                }
                            }
                        }
                        std::sort(next.begin(), next.end());
                        next.erase(std::unique(next.begin(), next.end()), next.end());
                        if (next.empty()) {
                            break;
                        }
                        if (count >= node->minRepeat) {
                            result.insert(result.end(), next.begin(), next.end());
                        }
                        frontier = std::move(next);
                    }
                    std::sort(result.begin(), result.end());
                    result.erase(std::unique(result.begin(), result.end()), result.end());
                }
                break;
        }

        memo.emplace(key, result);
        return result;
    }
};

Value makeJGexObject(const std::shared_ptr<JGexPattern>& pattern) {
    auto object = std::make_shared<Object>("System.JGex");
    object->properties.emplace("pattern", Value(pattern->pattern()));

    object->properties.emplace("test", makeNative("JGex.test",
        [pattern](interpreter::Interpreter&, const std::vector<Value>& args) -> Value {
            if (args.size() != 1U || !args[0].isString()) {
                throw std::runtime_error("JGex.test expects exactly one string argument.");
            }
            return Value(pattern->fullMatch(args[0].asString()));
        }));

    object->properties.emplace("match", makeNative("JGex.match",
        [pattern](interpreter::Interpreter&, const std::vector<Value>& args) -> Value {
            if (args.size() != 1U || !args[0].isString()) {
                throw std::runtime_error("JGex.match expects exactly one string argument.");
            }
            return Value(pattern->prefixMatch(args[0].asString()));
        }));

    object->properties.emplace("search", makeNative("JGex.search",
        [pattern](interpreter::Interpreter&, const std::vector<Value>& args) -> Value {
            if (args.size() != 1U || !args[0].isString()) {
                throw std::runtime_error("JGex.search expects exactly one string argument.");
            }
            const auto found = pattern->search(args[0].asString());
            if (!found.has_value()) {
                return makeNull();
            }
            auto result = std::make_shared<Object>("JGex.SearchResult");
            result->properties.emplace("index", static_cast<std::int64_t>(found->index));
            result->properties.emplace("length", static_cast<std::int64_t>(found->length));
            result->properties.emplace("match", found->match);
            return Value(result);
        }));

    object->properties.emplace("replace", makeNative("JGex.replace",
        [pattern](interpreter::Interpreter&, const std::vector<Value>& args) -> Value {
            if (args.size() != 2U || !args[0].isString() || !args[1].isString()) {
                throw std::runtime_error("JGex.replace expects input and replacement strings.");
            }
            return Value(pattern->replaceAll(args[0].asString(), args[1].asString()));
        }));

    object->properties.emplace("split", makeNative("JGex.split",
        [pattern](interpreter::Interpreter&, const std::vector<Value>& args) -> Value {
            if (args.size() != 1U || !args[0].isString()) {
                throw std::runtime_error("JGex.split expects exactly one string argument.");
            }
            const auto pieces = pattern->split(args[0].asString());
            auto array = std::make_shared<Value::Array>();
            array->reserve(pieces.size());
            for (const auto& piece : pieces) {
                array->emplace_back(piece);
            }
            return Value(array);
        }));

    return Value(object);
}

Value makeFileSystemObject() {
    auto object = std::make_shared<Object>("System.FileSystem");
    object->properties.emplace("cwd", cwdString());
    object->properties.emplace("home", homeDirectory());
    object->properties.emplace("temp", tempDirectory());
    object->properties.emplace("root", rootDirectory());
    return Value(object);
}

Value makeSystemInfoObject() {
    auto object = std::make_shared<Object>("System.Info");
    object->properties.emplace("platform", platformString());
    object->properties.emplace("architecture", archString());
    object->properties.emplace("compiler", compilerString());
    object->properties.emplace("endianness", endianString());
    return Value(object);
}

} // namespace

std::string escapeJsonString(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"':  oss << "\\\""; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    oss << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c)
                        << std::dec << std::setfill(' ');
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

std::string jsonStringify(const Value& v);

static std::string jsonStringifyValue(const Value& v) {
    if (v.isString()) {
        return "\"" + escapeJsonString(v.asString()) + "\"";
    }

    if (v.isArray()) {
        std::ostringstream oss;
        oss << '[';
        const auto arr = v.asArray();
        for (std::size_t i = 0; i < arr->size(); ++i) {
            if (i != 0U) oss << ',';
            oss << jsonStringify((*arr)[i]);
        }
        oss << ']';
        return oss.str();
    }

    if (v.isObject()) {
        std::ostringstream oss;
        oss << '{';
        const auto obj = v.asObject();
        bool first = true;
        for (const auto& [k, val] : obj->properties) {
            if (!first) oss << ',';
            first = false;
            oss << "\"" << escapeJsonString(k) << "\":" << jsonStringify(val);
        }
        oss << '}';
        return oss.str();
    }

    const std::string t = toLowerCopy(v.typeName());
    if (t == "null") {
        return "null";
    }
    if (t == "bool" || t == "boolean") {
        const std::string s = toLowerCopy(v.toString());
        if (s == "true" || s == "false") return s;
    }

    return v.toString();
}

std::string jsonStringify(const Value& v) {
    return jsonStringifyValue(v);
}

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        if (!ok_ || pos_ != text_.size()) {
            return Value();
        }
        return v;
    }

private:
    std::string text_;
    std::size_t pos_ {0};
    bool ok_ {true};

    void skipWs() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char c) {
        skipWs();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        ok_ = false;
        return false;
    }

    char get() {
        if (pos_ >= text_.size()) {
            ok_ = false;
            return '\0';
        }
        return text_[pos_++];
    }

    Value parseValue() {
        skipWs();
        if (pos_ >= text_.size()) {
            ok_ = false;
            return Value();
        }

        const char c = text_[pos_];
        if (c == '"') return Value(parseString());
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't') return parseTrue();
        if (c == 'f') return parseFalse();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

        ok_ = false;
        return Value();
    }

    Value parseNull() {
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Value();
        }
        ok_ = false;
        return Value();
    }

    Value parseTrue() {
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Value(true);
        }
        ok_ = false;
        return Value();
    }

    Value parseFalse() {
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Value(false);
        }
        ok_ = false;
        return Value();
    }

    Value parseNumber() {
        const std::size_t start = pos_;

        if (text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;

        bool isFloat = false;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            isFloat = true;
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }

        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            isFloat = true;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }

        try {
            const std::string num = text_.substr(start, pos_ - start);
            if (isFloat) {
                return Value(std::stod(num));
            }
            return Value(static_cast<std::int64_t>(std::stoll(num)));
        } catch (...) {
            ok_ = false;
            return Value();
        }
    }

    std::string parseString() {
        if (!consume('"')) return {};
        std::string out;

        while (pos_ < text_.size()) {
            char c = get();
            if (!ok_) return {};
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    ok_ = false;
                    return {};
                }
                char esc = get();
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        if (pos_ + 4 > text_.size()) {
                            ok_ = false;
                            return {};
                        }
                        pos_ += 4;
                        out.push_back('?');
                        break;
                    default:
                        ok_ = false;
                        return {};
                }
            } else {
                out.push_back(c);
            }
        }

        ok_ = false;
        return {};
    }

    Value parseArray() {
        if (!consume('[')) return Value();

        auto arr = std::make_shared<Value::Array>();
        skipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return Value(arr);
        }

        while (true) {
            Value item = parseValue();
            if (!ok_) return Value();
            arr->push_back(item);

            skipWs();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                break;
            }
            if (!consume(',')) return Value();
        }

        return Value(arr);
    }

    Value parseObject() {
        if (!consume('{')) return Value();

        auto obj = std::make_shared<Object>("System.Server.Json");
        skipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return Value(obj);
        }

        while (true) {
            skipWs();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                ok_ = false;
                return Value();
            }

            std::string key = parseString();
            if (!ok_) return Value();

            if (!consume(':')) return Value();

            Value val = parseValue();
            if (!ok_) return Value();

            obj->properties.emplace(std::move(key), std::move(val));

            skipWs();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                break;
            }
            if (!consume(',')) return Value();
        }

        return Value(obj);
    }
};

struct SocketState {
    int fd {-1};
    bool connected {false};
    bool listening {false};

    ~SocketState() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
};

struct ListenerState {
    int fd {-1};
    bool listening {false};

    ~ListenerState() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
};

static int parsePort(const Value& v) {
    int port = static_cast<int>(v.asDouble());
    if (port < 1 || port > 65535) return -1;
    return port;
}

static bool setSocketTimeout(int fd, int ms) {
    if (fd < 0 || ms < 0) return false;

    timeval tv {};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    const bool r1 = (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0);
    const bool r2 = (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0);
    return r1 && r2;
}

static std::string socketErrorString() {
    return std::string(std::strerror(errno));
}

static bool connectSocketState(const std::shared_ptr<SocketState>& state, const std::string& host, int port) {
    if (!state || host.empty() || port < 1 || port > 65535) return false;

    addrinfo hints {};
    addrinfo* res = nullptr;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string portStr = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) {
        return false;
    }

    int connectedFd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        const int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            connectedFd = fd;
            break;
        }

        ::close(fd);
    }

    ::freeaddrinfo(res);

    if (connectedFd < 0) {
        return false;
    }

    if (state->fd >= 0) {
        ::close(state->fd);
    }

    state->fd = connectedFd;
    state->connected = true;
    state->listening = false;
    return true;
}

static std::int64_t socketSendState(const std::shared_ptr<SocketState>& state, const std::string& data) {
    if (!state || state->fd < 0) return -1;

    std::size_t sentTotal = 0;
    while (sentTotal < data.size()) {
        const ssize_t n = ::send(
            state->fd,
            data.data() + sentTotal,
            data.size() - sentTotal,
            0
        );

        if (n <= 0) {
            return -1;
        }

        sentTotal += static_cast<std::size_t>(n);
    }

    return static_cast<std::int64_t>(sentTotal);
}

static std::string socketRecvState(const std::shared_ptr<SocketState>& state, std::size_t size) {
    if (!state || state->fd < 0 || size == 0) return {};

    std::string buf;
    buf.resize(size);

    const ssize_t n = ::recv(state->fd, buf.data(), size, 0);
    if (n <= 0) {
        return {};
    }

    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

static bool socketCloseState(const std::shared_ptr<SocketState>& state) {
    if (!state) return false;
    if (state->fd >= 0) {
        ::close(state->fd);
        state->fd = -1;
    }
    state->connected = false;
    state->listening = false;
    return true;
}

static bool listenerCloseState(const std::shared_ptr<ListenerState>& state) {
    if (!state) return false;
    if (state->fd >= 0) {
        ::close(state->fd);
        state->fd = -1;
    }
    state->listening = false;
    return true;
}

static Value makeSocketObject(std::shared_ptr<SocketState>& outState) {
    outState = std::make_shared<SocketState>();
    auto obj = std::make_shared<Object>("System.Server.Socket");

    obj->properties.emplace("connect", makeNative("System.Server.Socket.connect",
        [outState](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.size() < 2U) return Value(false);
            const std::string host = a[0].toString();
            const int port = parsePort(a[1]);
            return Value(connectSocketState(outState, host, port));
        }));

    obj->properties.emplace("send", makeNative("System.Server.Socket.send",
        [outState](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty()) return Value(static_cast<std::int64_t>(0));
            const std::string data = a[0].toString();
            return Value(socketSendState(outState, data));
        }));

    obj->properties.emplace("recv", makeNative("System.Server.Socket.recv",
        [outState](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            std::size_t size = 4096U;
            if (!a.empty()) {
                const int n = static_cast<int>(a[0].asDouble());
                if (n > 0) size = static_cast<std::size_t>(n);
            }
            return Value(socketRecvState(outState, size));
        }));

    obj->properties.emplace("close", makeNative("System.Server.Socket.close",
        [outState](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            return Value(socketCloseState(outState));
        }));

    obj->properties.emplace("setTimeout", makeNative("System.Server.Socket.setTimeout",
        [outState](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty() || outState->fd < 0) return Value(false);
            const int ms = static_cast<int>(a[0].asDouble());
            return Value(setSocketTimeout(outState->fd, ms));
        }));

    obj->properties.emplace("info", makeNative("System.Server.Socket.info",
        [outState](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            auto info = std::make_shared<Object>("System.Server.Socket.Info");
            info->properties.emplace("fd", static_cast<std::int64_t>(outState->fd));
            info->properties.emplace("connected", outState->connected);
            info->properties.emplace("listening", outState->listening);
            return Value(info);
        }));

    return Value(obj);
}

static Value makeConnectedSocketObject(std::shared_ptr<SocketState>& outState, const std::string& host, int port) {
    Value sock = makeSocketObject(outState);
    if (!connectSocketState(outState, host, port)) {
        return Value(false);
    }
    return sock;
}

static bool startListenerState(const std::shared_ptr<ListenerState>& state, int port, int backlog) {
    if (!state || port < 1 || port > 65535) return false;
    if (backlog <= 0) backlog = 16;

    addrinfo hints {};
    addrinfo* res = nullptr;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    const std::string portStr = std::to_string(port);
    if (::getaddrinfo(nullptr, portStr.c_str(), &hints, &res) != 0) {
        return false;
    }

    int listenerFd = -1;

    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        const int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

#ifdef SO_REUSEPORT
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

        if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0 && ::listen(fd, backlog) == 0) {
            listenerFd = fd;
            break;
        }

        ::close(fd);
    }

    ::freeaddrinfo(res);

    if (listenerFd < 0) {
        return false;
    }

    if (state->fd >= 0) {
        ::close(state->fd);
    }

    state->fd = listenerFd;
    state->listening = true;
    return true;
}

static Value makeListenerObject(std::shared_ptr<ListenerState>& outState) {
    outState = std::make_shared<ListenerState>();
    auto obj = std::make_shared<Object>("System.Server.Listen");

    obj->properties.emplace("accept", makeNative("System.Server.Listen.accept",
        [outState](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            if (!outState || outState->fd < 0) return Value(false);

            sockaddr_storage addr {};
            socklen_t len = sizeof(addr);
            const int cfd = ::accept(outState->fd, reinterpret_cast<sockaddr*>(&addr), &len);
            if (cfd < 0) {
                return Value(false);
            }

            std::shared_ptr<SocketState> socketState;
            Value sock = makeSocketObject(socketState);
            socketState->fd = cfd;
            socketState->connected = true;
            socketState->listening = false;
            return sock;
        }));

    obj->properties.emplace("close", makeNative("System.Server.Listen.close",
        [outState](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            return Value(listenerCloseState(outState));
        }));

    obj->properties.emplace("info", makeNative("System.Server.Listen.info",
        [outState](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            auto info = std::make_shared<Object>("System.Server.Listen.Info");
            info->properties.emplace("fd", static_cast<std::int64_t>(outState->fd));
            info->properties.emplace("listening", outState->listening);
            return Value(info);
        }));

    return Value(obj);
}

static Value makeListeningObject(std::shared_ptr<ListenerState>& outState, int port, int backlog) {
    Value listener = makeListenerObject(outState);
    if (!startListenerState(outState, port, backlog)) {
        return Value(false);
    }
    return listener;
}

static std::string statusText(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 415: return "Unsupported Media Type";
        case 418: return "I'm a teapot";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "OK";
    }
}

static std::string buildResponseHeader(int statusCode,
                                      const std::string& contentType,
                                      const std::string& body,
                                      const Object* extraHeaders) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << ' ' << statusText(statusCode) << "\r\n";
    oss << "Server: JDX\r\n";
    oss << "Connection: close\r\n";

    if (!contentType.empty()) {
        oss << "Content-Type: " << contentType << "\r\n";
    }

    if (!body.empty()) {
        oss << "Content-Length: " << body.size() << "\r\n";
    }

    if (extraHeaders) {
        for (const auto& [k, v] : extraHeaders->properties) {
            const std::string lk = toLowerCopy(k);
            if (lk == "content-type" || lk == "content-length" || lk == "connection" || lk == "server") {
                continue;
            }
            oss << k << ": " << v.toString() << "\r\n";
        }
    }

    oss << "\r\n";
    return oss.str();
}

static Value makeServerNamespace() {
    auto server = std::make_shared<Object>("System.Server");

    server->properties.emplace("Socket", makeNative("System.Server.Socket",
        [](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            std::shared_ptr<SocketState> state;
            return makeSocketObject(state);
        }));

    server->properties.emplace("Resolver", makeNative("System.Server.Resolver",
        [](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty()) return Value(std::string{});

            const std::string host = a[0].toString();
            if (host.empty()) return Value(std::string{});

            addrinfo hints {};
            addrinfo* res = nullptr;

            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0) {
                return Value(std::string{});
            }

            auto arr = std::make_shared<Value::Array>();
            std::set<std::string> seen;

            for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
                char buf[INET6_ADDRSTRLEN] {};
                void* src = nullptr;

                if (p->ai_family == AF_INET) {
                    src = &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr;
                } else if (p->ai_family == AF_INET6) {
                    src = &reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr;
                } else {
                    continue;
                }

                if (::inet_ntop(p->ai_family, src, buf, sizeof(buf)) == nullptr) {
                    continue;
                }

                std::string ip = buf;
                if (seen.insert(ip).second) {
                    arr->emplace_back(ip);
                }
            }

            ::freeaddrinfo(res);
            return Value(arr);
        }));

    server->properties.emplace("Connect", makeNative("System.Server.Connect",
        [](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.size() < 2U) return Value(false);

            const std::string host = a[0].toString();
            const int port = parsePort(a[1]);
            if (port < 0) return Value(false);

            std::shared_ptr<SocketState> state;
            return makeConnectedSocketObject(state, host, port);
        }));

    server->properties.emplace("Listen", makeNative("System.Server.Listen",
        [](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty()) return Value(false);

            const int port = parsePort(a[0]);
            if (port < 0) return Value(false);

            int backlog = 16;
            if (a.size() >= 2U) {
                backlog = static_cast<int>(a[1].asDouble());
                if (backlog <= 0) backlog = 16;
            }

            std::shared_ptr<ListenerState> state;
            return makeListeningObject(state, port, backlog);
        }));

    server->properties.emplace("JsonParse", makeNative("System.Server.JsonParse",
        [](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty()) return Value();
            JsonParser parser(a[0].toString());
            return parser.parse();
        }));

    server->properties.emplace("JsonStringfy", makeNative("System.Server.JsonStringfy",
        [](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty()) return Value(std::string("null"));
            return Value(jsonStringify(a[0]));
        }));

    server->properties.emplace("JsonStringify", makeNative("System.Server.JsonStringify",
        [](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty()) return Value(std::string("null"));
            return Value(jsonStringify(a[0]));
        }));

    server->properties.emplace("ResponseHeader", makeNative("System.Server.ResponseHeader",
        [](interpreter::Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty()) {
                return Value(std::string("HTTP/1.1 200 OK\r\nServer: JDX\r\nConnection: close\r\nX-JDX-Version: 0.1.0\r\n\r\n"));
            }

            const int statusCode = static_cast<int>(a[0].asDouble());
            const std::string contentType = (a.size() >= 2U) ? a[1].toString() : std::string("text/plain; charset=utf-8");

            std::string body;
            const Object* extraHeaders = nullptr;

            if (a.size() >= 3U) {
                if (a[2].isObject()) {
                    extraHeaders = a[2].asObject().get();
                } else {
                    body = a[2].toString();
                }
            }

            if (a.size() >= 4U && a[3].isObject()) {
                extraHeaders = a[3].asObject().get();
            }

            return Value(buildResponseHeader(statusCode, contentType, body, extraHeaders));
        }));

    return Value(server);
}

static void beepTone(int /*freq*/, int durationMs) {
    std::cout << '\a' << std::flush;

    if (durationMs > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(durationMs)
        );
    }
}

Value makeSystemObject(const std::vector<std::string>& systemArgs) {
    auto system = std::make_shared<Object>("System");

    system->properties.emplace("Args", [&systemArgs]() {
        auto array = std::make_shared<Value::Array>();
        array->reserve(systemArgs.size());
        for (const auto& arg : systemArgs) {
            array->emplace_back(arg);
        }
        return Value(array);
    }());

    system->properties.emplace("Output", makeNative("System.Output",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            for (std::size_t i = 0U; i < values.size(); ++i) {
                if (i != 0U) {
                    std::cout << ' ';
                }
                std::cout << values[i].toString();
            }
            std::cout << '\n';
            return makeNull();
        }));

    system->properties.emplace("Log", makeNative("System.Log",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            const std::string message = values.empty() ? std::string{} : values.front().toString();
            utils::Logger::log(message);
            return makeNull();
        }));

    system->properties.emplace("Warn", makeNative("System.Warn",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            const std::string message = values.empty() ? std::string{} : values.front().toString();
            utils::Logger::warn(message);
            return makeNull();
        }));

    system->properties.emplace("Error", makeNative("System.Error",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            const std::string message = values.empty() ? std::string{} : values.front().toString();
            utils::Logger::error(message);
            return makeNull();
        }));

    system->properties.emplace("ReadFile", makeNative("System.ReadFile",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U || !values[0].isString()) {
                throw std::runtime_error("System.ReadFile expects one string argument.");
            }
            std::ifstream in(values[0].asString(), std::ios::binary);
            if (!in) {
                throw std::runtime_error("Unable to read file '" + values[0].asString() + "'.");
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            return Value(ss.str());
        }));

    system->properties.emplace("WriteFile", makeNative("System.WriteFile",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 2U || !values[0].isString() || !values[1].isString()) {
                throw std::runtime_error("System.WriteFile expects two string arguments.");
            }
            std::ofstream out(values[0].asString(), std::ios::binary | std::ios::trunc);
            if (!out) {
                throw std::runtime_error("Unable to write file '" + values[0].asString() + "'.");
            }
            out << values[1].asString();
            if (!out) {
                throw std::runtime_error("Failed while writing file '" + values[0].asString() + "'.");
            }
            return makeNull();
        }));

    system->properties.emplace("Exists", makeNative("System.Exists",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U || !values[0].isString()) {
                throw std::runtime_error("System.Exists expects one string argument.");
            }
            return Value(fs::exists(fs::path(values[0].asString())));
        }));

    system->properties.emplace("Clock", makeNative("System.Clock",
        [](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            return Value(formatTimePoint(std::chrono::system_clock::now()));
        }));

    system->properties.emplace("Time", makeNative("System.Time",
        [](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return Value(static_cast<std::int64_t>(ms));
        }));

    system->properties.emplace("Sleep", makeNative("System.Sleep",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U || !values[0].isNumber()) {
                throw std::runtime_error("System.Sleep expects one numeric argument.");
            }
            const auto ms = static_cast<std::uint64_t>(values[0].asDouble());
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return makeNull();
        }));

    system->properties.emplace("Random", makeNative("System.Random",
        [](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            static std::mutex rngMutex;
            static std::mt19937_64 rng{std::random_device{}()};
            std::lock_guard<std::mutex> lock(rngMutex);
            return Value(static_cast<std::int64_t>(rng()));
        }));

    system->properties.emplace("Type", makeNative("System.Type",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U) {
                throw std::runtime_error("System.Type expects one argument.");
            }
            return Value(values[0].typeName());
        }));

    system->properties.emplace("Len", makeNative("System.Len",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U) {
                throw std::runtime_error("System.Len expects one argument.");
            }
            if (values[0].isString()) {
                return Value(static_cast<std::int64_t>(values[0].asString().size()));
            }
            if (values[0].isArray()) {
                return Value(static_cast<std::int64_t>(values[0].asArray()->size()));
            }
            if (values[0].isObject()) {
                return Value(static_cast<std::int64_t>(values[0].asObject()->properties.size()));
            }
            throw std::runtime_error("System.Len expects a string, array, or object.");
        }));

    system->properties.emplace("Lower", makeNative("System.Lower",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U || !values[0].isString()) {
                throw std::runtime_error("System.Lower expects one string argument.");
            }
            return Value(toLowerCopy(values[0].asString()));
        }));

    system->properties.emplace("Upper", makeNative("System.Upper",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U || !values[0].isString()) {
                throw std::runtime_error("System.Upper expects one string argument.");
            }
            return Value(toUpperCopy(values[0].asString()));
        }));

    system->properties.emplace("SocketError", makeNative("System.SocketError",
        [](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            return Value(socketErrorString());
        }));

    system->properties.emplace("FileSystem", makeNative("System.FileSystem",
        [](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            return makeFileSystemObject();
        }));

    system->properties.emplace("ShowSystemInfo", makeNative("System.ShowSystemInfo",
        [](interpreter::Interpreter&, const std::vector<Value>&) -> Value {
            return makeSystemInfoObject();
        }));

    system->properties.emplace("JGex", makeNative("System.JGex",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() != 1U || !values[0].isString()) {
                throw std::runtime_error("System.JGex expects one string pattern.");
            }
            return makeJGexObject(std::make_shared<JGexPattern>(values[0].asString()));
        }));

    system->properties.emplace("Regex", system->properties.at("JGex"));

    system->properties.emplace("Server", makeServerNamespace());

    system->properties.emplace("SafeExec", makeNative("System.SafeExec",
        [](interpreter::Interpreter& interpreter, const std::vector<Value>& values) -> Value {
            if (values.empty()) {
                return makeErrorResult("System.SafeExec expects a callable as the first argument.");
            }

            const Value& callee = values.front();
            if (!callee.isCallable()) {
                return makeErrorResult("System.SafeExec expects a callable as the first argument.");
            }

            std::vector<Value> callArgs;
            callArgs.reserve(values.size() - 1U);
            for (std::size_t i = 1U; i < values.size(); ++i) {
                callArgs.push_back(values[i]);
            }

            try {
                return makeOkResult(interpreter.callValue(callee, callArgs));
            } catch (const utils::DiagnosticError& error) {
                return makeErrorResult(error.what());
            } catch (const std::exception& error) {
                return makeErrorResult(error.what());
            }
        }));
    
    system->properties.emplace("Beep", makeNative("System.Beep", [](interpreter::Interpreter&,
           const std::vector<Value>& args) -> Value {

            int freq = 750;
            int duration = 300;

            if (!args.empty()) {
                freq = static_cast<int>(args[0].asDouble());
            }

            if (args.size() >= 2) {
                duration = static_cast<int>(args[1].asDouble());
            }

            if (freq < 37) {
                freq = 37;
            }

            if (freq > 32767) {
                freq = 32767;
            }

            if (duration < 1) {
                duration = 1;
            }

            beepTone(freq, duration);

            return makeNull();
        }
    ));

    return Value(system);
}

Value makeDevelopmentTestObject() {
    auto test = std::make_shared<Object>("Develoment.Test");

    test->properties.emplace("Assert", makeNative("Develoment.Test.Assert",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.empty() || !values[0].truthy()) {
                const std::string message = (values.size() >= 2U && values[1].isString())
                    ? values[1].asString()
                    : std::string{"Assertion failed."};
                throw std::runtime_error(message);
            }
            return Value(true);
        }));

    test->properties.emplace("Equal", makeNative("Develoment.Test.Equal",
        [](interpreter::Interpreter&, const std::vector<Value>& values) -> Value {
            if (values.size() < 2U) {
                throw std::runtime_error("Develoment.Test.Equal expects at least two arguments.");
            }
            const bool ok = values[0].equals(values[1]);
            if (!ok) {
                const std::string message = (values.size() >= 3U && values[2].isString())
                    ? values[2].asString()
                    : std::string{"Values are not equal."};
                throw std::runtime_error(message);
            }
            return Value(true);
        }));

    test->properties.emplace("Throws", makeNative("Develoment.Test.Throws",
        [](interpreter::Interpreter& interpreter, const std::vector<Value>& values) -> Value {
            if (values.empty() || !values[0].isCallable()) {
                throw std::runtime_error("Develoment.Test.Throws expects a callable.");
            }
            std::vector<Value> args;
            args.reserve(values.size() - 1U);
            for (std::size_t i = 1U; i < values.size(); ++i) {
                args.push_back(values[i]);
            }
            try {
                static_cast<void>(interpreter.callValue(values[0], args));
            } catch (...) {
                return Value(true);
            }
            throw std::runtime_error("Expected callable to throw.");
        }));

    return Value(test);
}

Value makeDevelopmentObject() {
    auto development = std::make_shared<Object>("Develoment");
    auto stacktrace = std::make_shared<Object>("Develoment.Stacktrace");
    stacktrace->properties.emplace("Level", static_cast<std::int64_t>(8));
    stacktrace->properties.emplace("Type", std::string("compact"));
    development->properties.emplace("Stacktrace", Value(stacktrace));
    development->properties.emplace("Test", makeDevelopmentTestObject());
    return Value(development);
}

std::shared_ptr<interpreter::Environment> makeGlobalEnvironment(const std::vector<std::string>& args) {
    auto globals = std::make_shared<interpreter::Environment>();
    globals->define("System", makeSystemObject(args), true, false);
    globals->define("Develoment", makeDevelopmentObject(), true, false);
    return globals;
}

} // namespace jdx::runtime
