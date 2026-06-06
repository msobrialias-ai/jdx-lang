#include "runtime/SystemRuntime.hpp"
#include "utils/ErrorHandler.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <netdb.h>
#include <memory>
#include <random>
#include <regex>
#include <set>
#include <signal.h>
#include <sstream>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace jdx::runtime {

Value makeNative(const std::string& name, std::function<Value(const std::vector<Value>&)> fn) {
    auto native = std::make_shared<NativeFunction>();
    native->name = name;
    native->callable = std::move(fn);
    return Value(native);
}

static std::string trimCopy(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string toUpperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

static std::string getEnvOr(const char* name, const std::string& fallback = "N/A") {
    const char* v = std::getenv(name);
    return v ? std::string(v) : fallback;
}

static std::string platformString() {
#if defined(__ANDROID__)
    return "Android";
#elif defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown";
#endif
}

static std::string archString() {
#if defined(__aarch64__)
    return "ARM64";
#elif defined(__arm__) || defined(_M_ARM)
    return "ARMv7";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "Unknown";
#endif
}

static std::string compilerString() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "Unknown";
#endif
}

static std::string endianString() {
    const std::uint16_t n = 0x1;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&n);
    return (p[0] == 0x1) ? "Little Endian" : "Big Endian";
}

static std::string formatBytes(std::uint64_t kib) {
    const double gib = static_cast<double>(kib) / 1024.0 / 1024.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << gib << " GB";
    return oss.str();
}

static bool readMemInfo(std::uint64_t& totalKiB, std::uint64_t& availableKiB) {
    std::ifstream in("/proc/meminfo");
    if (!in.is_open()) return false;

    std::string key, unit;
    std::uint64_t value = 0;
    totalKiB = 0;
    availableKiB = 0;

    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") {
            totalKiB = value;
        } else if (key == "MemAvailable:") {
            availableKiB = value;
        }

        if (totalKiB != 0 && availableKiB != 0) {
            return true;
        }
    }

    return totalKiB != 0;
}

static std::string formatTime(std::time_t t, bool utc) {
    std::tm tm{};

#if defined(_WIN32)
    if (utc) {
        gmtime_s(&tm, &t);
    } else {
        localtime_s(&tm, &t);
    }
#else
    if (utc) {
        gmtime_r(&t, &tm);
    } else {
        localtime_r(&t, &tm);
    }
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static void printSystemInfo() {
    struct utsname u {};
    std::string sysname = "Unknown";
    std::string release = "Unknown";
    std::string version = "Unknown";
    std::string nodename = "Unknown";
    std::string machine = "Unknown";

    if (uname(&u) == 0) {
        sysname = u.sysname;
        release = u.release;
        version = u.version;
        nodename = u.nodename;
        machine = u.machine;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);

    std::uint64_t memTotalKiB = 0;
    std::uint64_t memAvailKiB = 0;
    const bool hasMem = readMemInfo(memTotalKiB, memAvailKiB);

    std::string cwd;
    try {
        cwd = fs::current_path().string();
    } catch (...) {
        cwd = "N/A";
    }

    std::string home = getEnvOr("HOME");
    std::string tempDir;
    try {
        tempDir = fs::temp_directory_path().string();
    } catch (...) {
        tempDir = "N/A";
    }

    const unsigned int cores = std::thread::hardware_concurrency();
    const std::uint64_t pid = static_cast<std::uint64_t>(getpid());
    const std::uint64_t ppid = static_cast<std::uint64_t>(getppid());
    const std::uint64_t uid = static_cast<std::uint64_t>(getuid());
    const std::uint64_t gid = static_cast<std::uint64_t>(getgid());
    const std::size_t ptrSize = sizeof(void*) * 8;

    std::cout << "==================== JDX System Info ====================\n\n";

    std::cout << "[JDX Runtime]\n";
    std::cout << std::left << std::setw(18) << "Version"      << " : " << "0.1.0" << '\n';
    std::cout << std::left << std::setw(18) << "Build Type"   << " : "
#ifdef NDEBUG
              << "Release"
#else
              << "Debug"
#endif
              << '\n';
    std::cout << std::left << std::setw(18) << "Compiler"     << " : " << compilerString() << '\n';
    std::cout << std::left << std::setw(18) << "Build Date"   << " : " << __DATE__ << " " << __TIME__ << '\n';
    std::cout << std::left << std::setw(18) << "Build Target" << " : " << archString() << '\n';
    std::cout << std::left << std::setw(18) << "Pointer Size" << " : " << ptrSize << "-bit\n\n";

    std::cout << "[Operating System]\n";
    std::cout << std::left << std::setw(18) << "Platform"      << " : " << platformString() << '\n';
    std::cout << std::left << std::setw(18) << "Kernel Name"   << " : " << sysname << '\n';
    std::cout << std::left << std::setw(18) << "Kernel Release"<< " : " << release << '\n';
    std::cout << std::left << std::setw(18) << "Kernel Version" << " : " << version << '\n';
    std::cout << std::left << std::setw(18) << "Hostname"      << " : " << nodename << '\n';
    std::cout << std::left << std::setw(18) << "Machine"       << " : " << machine << "\n\n";

    std::cout << "[CPU]\n";
    std::cout << std::left << std::setw(18) << "Architecture"  << " : " << archString() << '\n';
    std::cout << std::left << std::setw(18) << "CPU Cores"     << " : " << (cores ? std::to_string(cores) : "N/A") << '\n';
    std::cout << std::left << std::setw(18) << "Endianness"    << " : " << endianString() << "\n\n";

    std::cout << "[Memory]\n";
    if (hasMem) {
        const std::uint64_t usedKiB = (memTotalKiB > memAvailKiB) ? (memTotalKiB - memAvailKiB) : 0;
        std::cout << std::left << std::setw(18) << "RAM Total"     << " : " << formatBytes(memTotalKiB) << '\n';
        std::cout << std::left << std::setw(18) << "RAM Available" << " : " << formatBytes(memAvailKiB) << '\n';
        std::cout << std::left << std::setw(18) << "RAM Used"      << " : " << formatBytes(usedKiB) << '\n';
    } else {
        std::cout << std::left << std::setw(18) << "RAM Total"     << " : " << "N/A" << '\n';
        std::cout << std::left << std::setw(18) << "RAM Available" << " : " << "N/A" << '\n';
        std::cout << std::left << std::setw(18) << "RAM Used"      << " : " << "N/A" << '\n';
    }
    std::cout << '\n';

    std::cout << "[Process]\n";
    std::cout << std::left << std::setw(18) << "PID"            << " : " << pid << '\n';
    std::cout << std::left << std::setw(18) << "Parent PID"     << " : " << ppid << '\n';
    std::cout << std::left << std::setw(18) << "User ID"        << " : " << uid << '\n';
    std::cout << std::left << std::setw(18) << "Group ID"       << " : " << gid << "\n\n";

    std::cout << "[Filesystem]\n";
    std::cout << std::left << std::setw(18) << "Current Directory" << " : " << cwd << '\n';
    std::cout << std::left << std::setw(18) << "Home Directory"    << " : " << home << '\n';
    std::cout << std::left << std::setw(18) << "Temp Directory"    << " : " << tempDir << '\n';
    std::cout << std::left << std::setw(18) << "Root Directory"    << " : " << fs::path("/").string() << "\n\n";

    std::cout << "[Environment]\n";
    std::cout << std::left << std::setw(18) << "PATH"          << " : " << getEnvOr("PATH") << '\n';
    std::cout << std::left << std::setw(18) << "TERM"          << " : " << getEnvOr("TERM") << '\n';
    std::cout << std::left << std::setw(18) << "LANG"          << " : " << getEnvOr("LANG") << '\n';
    std::cout << std::left << std::setw(18) << "SHELL"         << " : " << getEnvOr("SHELL") << "\n\n";

    std::cout << "[Time]\n";
    std::cout << std::left << std::setw(18) << "Local Time"     << " : " << formatTime(tt, false) << '\n';
    std::cout << std::left << std::setw(18) << "UTC Time"       << " : " << formatTime(tt, true) << '\n';
    std::cout << std::left << std::setw(18) << "Timezone"       << " : " << getEnvOr("TZ", "Asia/Jakarta") << '\n';

    std::cout << "\n=========================================================\n";
}

/* ----------------------------- JSON helpers ----------------------------- */

static std::string escapeJsonString(const std::string& s) {
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

static std::string jsonStringify(const Value& v);


struct BufferState {
    std::vector<std::uint8_t> bytes;
};

static std::int64_t valueToInt64(const Value& v, std::int64_t fallback = 0) {
    try {
        const std::string s = v.toString();
        size_t idx = 0;
        long long n = std::stoll(s, &idx, 10);
        (void)idx;
        return static_cast<std::int64_t>(n);
    } catch (...) {
        return fallback;
    }
}

static std::uint8_t clampByte(std::int64_t n) {
    if (n < 0) return 0;
    if (n > 255) return 255;
    return static_cast<std::uint8_t>(n);
}

static std::vector<std::uint8_t> valueToBytes(const Value& v) {
    std::vector<std::uint8_t> out;

    if (v.isString()) {
        const std::string s = v.asString();
        out.assign(s.begin(), s.end());
        return out;
    }

    if (v.isArray()) {
        const auto arr = v.asArray();
        out.reserve(arr->size());
        for (const auto& item : *arr) {
            out.push_back(clampByte(valueToInt64(item)));
        }
        return out;
    }

    if (v.isObject()) {
        const auto obj = v.asObject();
        auto itKind = obj->find("kind");
        if (itKind != obj->end() && itKind->second.isString() && toLowerCopy(itKind->second.asString()) == "buffer") {
            auto itBytes = obj->find("bytes");
            if (itBytes != obj->end() && itBytes->second.isArray()) {
                const auto arr = itBytes->second.asArray();
                out.reserve(arr->size());
                for (const auto& item : *arr) {
                    out.push_back(clampByte(valueToInt64(item)));
                }
                return out;
            }
        }
    }

    const std::string s = v.toString();
    out.assign(s.begin(), s.end());
    return out;
}

static Value makeBufferObject(std::shared_ptr<BufferState> state) {
    auto obj = std::make_shared<Value::Object>();

    (*obj)["kind"] = Value("buffer");

    (*obj)["length"] = makeNative("Buffer.length", [state](const std::vector<Value>&) {
        return Value(static_cast<std::int64_t>(state->bytes.size()));
    });

    (*obj)["clear"] = makeNative("Buffer.clear", [state](const std::vector<Value>&) {
        state->bytes.clear();
        return Value{};
    });

    (*obj)["write"] = makeNative("Buffer.write", [state](const std::vector<Value>& args) {
        if (!args.empty()) {
            const auto bytes = valueToBytes(args[0]);
            state->bytes.insert(state->bytes.end(), bytes.begin(), bytes.end());
        }
        return Value(static_cast<std::int64_t>(state->bytes.size()));
    });

    (*obj)["appendByte"] = makeNative("Buffer.appendByte", [state](const std::vector<Value>& args) {
        if (!args.empty()) {
            state->bytes.push_back(clampByte(valueToInt64(args[0])));
        }
        return Value(static_cast<std::int64_t>(state->bytes.size()));
    });

    (*obj)["toString"] = makeNative("Buffer.toString", [state](const std::vector<Value>&) {
        return Value(std::string(state->bytes.begin(), state->bytes.end()));
    });

    (*obj)["toBytes"] = makeNative("Buffer.toBytes", [state](const std::vector<Value>&) {
        auto arr = std::make_shared<Value::Array>();
        arr->reserve(state->bytes.size());
        for (std::uint8_t b : state->bytes) {
            arr->emplace_back(static_cast<std::int64_t>(b));
        }
        return Value(arr);
    });

    (*obj)["get"] = makeNative("Buffer.get", [state](const std::vector<Value>& args) {
        if (args.empty()) return Value{};
        std::int64_t idx = valueToInt64(args[0], -1);
        if (idx < 0) idx = static_cast<std::int64_t>(state->bytes.size()) + idx;
        if (idx < 0 || idx >= static_cast<std::int64_t>(state->bytes.size())) return Value{};
        return Value(static_cast<std::int64_t>(state->bytes[static_cast<std::size_t>(idx)]));
    });

    (*obj)["set"] = makeNative("Buffer.set", [state](const std::vector<Value>& args) {
        if (args.size() < 2) return Value{};
        std::int64_t idx = valueToInt64(args[0], -1);
        if (idx < 0) idx = static_cast<std::int64_t>(state->bytes.size()) + idx;
        if (idx < 0) return Value{};

        std::size_t pos = static_cast<std::size_t>(idx);
        if (pos >= state->bytes.size()) {
            state->bytes.resize(pos + 1, 0);
        }
        state->bytes[pos] = clampByte(valueToInt64(args[1]));
        return Value(static_cast<std::int64_t>(state->bytes.size()));
    });

    (*obj)["slice"] = makeNative("Buffer.slice", [state](const std::vector<Value>& args) {
        std::int64_t start = 0;
        std::int64_t end = static_cast<std::int64_t>(state->bytes.size());

        if (!args.empty()) start = valueToInt64(args[0], 0);
        if (args.size() >= 2) end = valueToInt64(args[1], static_cast<std::int64_t>(state->bytes.size()));

        const std::int64_t size = static_cast<std::int64_t>(state->bytes.size());

        if (start < 0) start = size + start;
        if (end < 0) end = size + end;

        start = std::clamp<std::int64_t>(start, 0, size);
        end = std::clamp<std::int64_t>(end, 0, size);

        if (end < start) end = start;

        using Diff = std::vector<std::uint8_t>::difference_type;

        const auto beginOffset = static_cast<Diff>(start);
        const auto endOffset   = static_cast<Diff>(end);

        auto next = std::make_shared<BufferState>();

        next->bytes.insert(
           next->bytes.end(),
           state->bytes.begin() + beginOffset,
           state->bytes.begin() + endOffset
        );

        return makeBufferObject(next);
    });

    return Value(obj);
}

struct RegexState {
    std::string pattern;
    std::string flags;
    std::regex compiled;
};

static std::regex_constants::syntax_option_type regexOptionsFromFlags(const std::string& flags) {
    using namespace std::regex_constants;

    syntax_option_type options = ECMAScript;
    for (char ch : flags) {
        switch (ch) {
            case 'i': options |= icase; break;
            case 'n': options |= nosubs; break;
            case 'o': options |= optimize; break;
            case 'c': options |= collate; break;
            default:
                throw std::runtime_error("Unsupported regex flag '" + std::string(1, ch) + "'.");
        }
    }
    return options;
}

static std::shared_ptr<RegexState> makeRegexState(const std::string& pattern, const std::string& flags) {
    auto state = std::make_shared<RegexState>();
    state->pattern = pattern;
    state->flags = flags;
    state->compiled = std::regex(pattern, regexOptionsFromFlags(flags));
    return state;
}

static std::string valueAsText(const Value& v) {
    return v.isString() ? v.asString() : v.toString();
}

static Value makeRegexSearchResult(const std::smatch& match, const std::string& input) {
    auto obj = std::make_shared<Value::Object>();
    (*obj)["matched"] = Value(!match.empty());
    (*obj)["input"] = Value(input);

    if (!match.empty()) {
        (*obj)["value"] = Value(match.str());
        (*obj)["index"] = Value(static_cast<std::int64_t>(match.position()));
        (*obj)["length"] = Value(static_cast<std::int64_t>(match.length()));

        auto groups = std::make_shared<Value::Array>();
        groups->reserve(match.size());
        for (std::size_t i = 0; i < match.size(); ++i) {
            groups->emplace_back(match[i].str());
        }
        (*obj)["groups"] = Value(groups);
    }

    return Value(obj);
}

static Value makeRegexObject(std::shared_ptr<RegexState> state) {
    auto obj = std::make_shared<Value::Object>();

    (*obj)["kind"] = Value("regex");
    (*obj)["pattern"] = Value(state->pattern);
    (*obj)["flags"] = Value(state->flags);

    (*obj)["test"] = makeNative("Regex.test", [state](const std::vector<Value>& args) {
        if (args.empty()) return Value(false);
        return Value(std::regex_search(valueAsText(args[0]), state->compiled));
    });

    (*obj)["match"] = makeNative("Regex.match", [state](const std::vector<Value>& args) {
        if (args.empty()) return Value(false);
        return Value(std::regex_match(valueAsText(args[0]), state->compiled));
    });

    (*obj)["search"] = makeNative("Regex.search", [state](const std::vector<Value>& args) {
        if (args.empty()) return makeRegexSearchResult(std::smatch{}, std::string{});
        const std::string input = valueAsText(args[0]);
        std::smatch match;
        if (!std::regex_search(input, match, state->compiled)) {
            return makeRegexSearchResult(match, input);
        }
        return makeRegexSearchResult(match, input);
    });

    (*obj)["replace"] = makeNative("Regex.replace", [state](const std::vector<Value>& args) {
        if (args.empty()) return Value(std::string{});
        const std::string input = valueAsText(args[0]);
        const std::string replacement = args.size() >= 2 ? valueAsText(args[1]) : std::string{};
        return Value(std::regex_replace(input, state->compiled, replacement));
    });

    (*obj)["split"] = makeNative("Regex.split", [state](const std::vector<Value>& args) {
        auto out = std::make_shared<Value::Array>();
        if (args.empty()) return Value(out);

        const std::string input = valueAsText(args[0]);
        std::sregex_token_iterator it(input.begin(), input.end(), state->compiled, -1);
        std::sregex_token_iterator end;
        for (; it != end; ++it) {
            out->emplace_back(it->str());
        }
        return Value(out);
    });

    return Value(obj);
}


class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        if (!ok_ || pos_ != text_.size()) {
            return Value{};
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

    char peek() {
        skipWs();
        if (pos_ >= text_.size()) {
            ok_ = false;
            return '\0';
        }
        return text_[pos_];
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
            return Value{};
        }

        char c = text_[pos_];
        if (c == '"') return Value(parseString());
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't') return parseTrue();
        if (c == 'f') return parseFalse();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

        ok_ = false;
        return Value{};
    }

    Value parseNull() {
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Value{};
        }
        ok_ = false;
        return Value{};
    }

    Value parseTrue() {
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Value(true);
        }
        ok_ = false;
        return Value{};
    }

    Value parseFalse() {
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Value(false);
        }
        ok_ = false;
        return Value{};
    }

    Value parseNumber() {
        std::size_t start = pos_;

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
            return Value{};
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
                        /* Minimal fallback: skip \uXXXX and replace with '?'. */
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
        if (!consume('[')) return Value{};

        auto arr = std::make_shared<Value::Array>();
        skipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return Value(arr);
        }

        while (true) {
            Value item = parseValue();
            if (!ok_) return Value{};
            arr->push_back(item);

            skipWs();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                break;
            }
            if (!consume(',')) return Value{};
        }

        return Value(arr);
    }

    Value parseObject() {
        if (!consume('{')) return Value{};

        auto obj = std::make_shared<Value::Object>();
        skipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return Value(obj);
        }

        while (true) {
            skipWs();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                ok_ = false;
                return Value{};
            }

            std::string key = parseString();
            if (!ok_) return Value{};

            if (!consume(':')) return Value{};

            Value val = parseValue();
            if (!ok_) return Value{};

            (*obj)[key] = val;

            skipWs();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                break;
            }
            if (!consume(',')) return Value{};
        }

        return Value(obj);
    }
};

static std::string jsonStringifyValue(const Value& v) {
    if (v.isString()) {
        return "\"" + escapeJsonString(v.asString()) + "\"";
    }

    if (v.isArray()) {
        std::ostringstream oss;
        oss << '[';
        const auto arr = v.asArray();
        for (std::size_t i = 0; i < arr->size(); ++i) {
            if (i) oss << ',';
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
        for (const auto& [k, val] : *obj) {
            if (!first) oss << ',';
            first = false;
            oss << "\"" << escapeJsonString(k) << "\":"
                << jsonStringify(val);
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

static std::string jsonStringify(const Value& v) {
    return jsonStringifyValue(v);
}

/* --------------------------- Server / Socket --------------------------- */

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

struct SocketHandle {
    std::shared_ptr<SocketState> state;
    Value value;
};

struct ListenerHandle {
    std::shared_ptr<ListenerState> state;
    Value value;
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
    auto obj = std::make_shared<Value::Object>();

    (*obj)["connect"] = makeNative("System.Server.Socket.connect", [outState](const std::vector<Value>& a) {
        if (a.size() < 2) return Value(false);
        const std::string host = a[0].asString();
        const int port = parsePort(a[1]);
        return Value(connectSocketState(outState, host, port));
    });

    (*obj)["send"] = makeNative("System.Server.Socket.send", [outState](const std::vector<Value>& a) {
        if (a.empty()) return Value(static_cast<std::int64_t>(0));
        const std::string data = a[0].isString() ? a[0].asString() : a[0].toString();
        return Value(socketSendState(outState, data));
    });

    (*obj)["recv"] = makeNative("System.Server.Socket.recv", [outState](const std::vector<Value>& a) {
        std::size_t size = 4096;
        if (!a.empty()) {
            const int n = static_cast<int>(a[0].asDouble());
            if (n > 0) size = static_cast<std::size_t>(n);
        }
        return Value(socketRecvState(outState, size));
    });

    (*obj)["close"] = makeNative("System.Server.Socket.close", [outState](const std::vector<Value>&) {
        return Value(socketCloseState(outState));
    });

    (*obj)["setTimeout"] = makeNative("System.Server.Socket.setTimeout", [outState](const std::vector<Value>& a) {
        if (a.empty() || outState->fd < 0) return Value(false);
        const int ms = static_cast<int>(a[0].asDouble());
        return Value(setSocketTimeout(outState->fd, ms));
    });

    (*obj)["info"] = makeNative("System.Server.Socket.info", [outState](const std::vector<Value>&) {
        auto info = std::make_shared<Value::Object>();
        (*info)["fd"] = Value(static_cast<std::int64_t>(outState->fd));
        (*info)["connected"] = Value(outState->connected);
        (*info)["listening"] = Value(outState->listening);
        return Value(info);
    });

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
    auto obj = std::make_shared<Value::Object>();

    (*obj)["accept"] = makeNative("System.Server.Listen.accept", [outState](const std::vector<Value>&) {
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
    });

    (*obj)["close"] = makeNative("System.Server.Listen.close", [outState](const std::vector<Value>&) {
        return Value(listenerCloseState(outState));
    });

    (*obj)["info"] = makeNative("System.Server.Listen.info", [outState](const std::vector<Value>&) {
        auto info = std::make_shared<Value::Object>();
        (*info)["fd"] = Value(static_cast<std::int64_t>(outState->fd));
        (*info)["listening"] = Value(outState->listening);
        return Value(info);
    });

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
                                      const Value::Object* extraHeaders) {
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
        for (const auto& [k, v] : *extraHeaders) {
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
    auto server = std::make_shared<Value::Object>();

    (*server)["Socket"] = makeNative("System.Server.Socket", [](const std::vector<Value>&) {
        std::shared_ptr<SocketState> state;
        return makeSocketObject(state);
    });

    (*server)["Resolver"] = makeNative("System.Server.Resolver", [](const std::vector<Value>& a) {
        if (a.empty()) return Value(std::string{});

        const std::string host = a[0].asString();
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
    });

    (*server)["Connect"] = makeNative("System.Server.Connect", [](const std::vector<Value>& a) {
        if (a.size() < 2) return Value(false);

        const std::string host = a[0].asString();
        const int port = parsePort(a[1]);
        if (port < 0) return Value(false);

        std::shared_ptr<SocketState> state;
        return makeConnectedSocketObject(state, host, port);
    });

    (*server)["Listen"] = makeNative("System.Server.Listen", [](const std::vector<Value>& a) {
        if (a.empty()) return Value(false);

        const int port = parsePort(a[0]);
        if (port < 0) return Value(false);

        int backlog = 16;
        if (a.size() >= 2) {
            backlog = static_cast<int>(a[1].asDouble());
            if (backlog <= 0) backlog = 16;
        }

        std::shared_ptr<ListenerState> state;
        return makeListeningObject(state, port, backlog);
    });

    (*server)["JsonParse"] = makeNative("System.Server.JsonParse", [](const std::vector<Value>& a) {
        if (a.empty()) return Value{};
        JsonParser parser(a[0].toString());
        return parser.parse();
    });

    (*server)["JsonStringfy"] = makeNative("System.Server.JsonStringfy", [](const std::vector<Value>& a) {
        if (a.empty()) return Value(std::string("null"));
        return Value(jsonStringify(a[0]));
    });

    (*server)["JsonStringify"] = makeNative("System.Server.JsonStringify", [](const std::vector<Value>& a) {
        if (a.empty()) return Value(std::string("null"));
        return Value(jsonStringify(a[0]));
    });

    (*server)["ResponseHeader"] = makeNative("System.Server.ResponseHeader", [](const std::vector<Value>& a) {
        if (a.empty()) {
            return Value(std::string("HTTP/1.1 200 OK\r\nServer: JDX\r\nConnection: close\r\nX-JDX-Version: 0.1.0\r\n\r\n"));
        }

        const int statusCode = static_cast<int>(a[0].asDouble());
        const std::string contentType = (a.size() >= 2) ? a[1].asString() : "text/plain; charset=utf-8";

        std::string body;
        const Value::Object* extraHeaders = nullptr;

        if (a.size() >= 3) {
            if (a[2].isObject()) {
                extraHeaders = a[2].asObject().get();
            } else {
                body = a[2].toString();
            }
        }

        if (a.size() >= 4 && a[3].isObject()) {
            extraHeaders = a[3].asObject().get();
        }

        return Value(buildResponseHeader(statusCode, contentType, body, extraHeaders));
    });

    return Value(server);
}

Value makeSystemObject(const std::vector<std::string>& args) {
    auto obj = std::make_shared<Value::Object>();

    (*obj)["Output"] = makeNative("System.Output", [](const std::vector<Value>& a) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            std::cout << a[i].toString();
            if (i + 1 < a.size()) std::cout << ' ';
        }
        std::cout << std::endl;
        return Value{};
    });

    (*obj)["Input"] = makeNative("System.Input", [](const std::vector<Value>&) {
        std::string s;
        std::getline(std::cin, s);
        return Value(s);
    });

    (*obj)["Log"] = makeNative("System.Log", [](const std::vector<Value>& a) {
        for (const auto& v : a) {
            std::clog << v.toString() << ' ';
        }
        std::clog << std::endl;
        return Value{};
    });

    (*obj)["Warn"] = makeNative("System.Warn", [](const std::vector<Value>& a) {
        for (const auto& v : a) {
            std::cerr << v.toString() << ' ';
        }
        std::cerr << std::endl;
        return Value{};
    });

    (*obj)["Error"] = makeNative("System.Error", [](const std::vector<Value>& a) {
        for (const auto& v : a) {
            std::cerr << v.toString() << ' ';
        }
        std::cerr << std::endl;
        return Value{};
    });

    (*obj)["Time"] = makeNative("System.Time", [](const std::vector<Value>&) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return Value(static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count()
        ));
    });

    (*obj)["Clock"] = makeNative("System.Clock", [](const std::vector<Value>&) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);

        std::tm tm {};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return Value(oss.str());
    });

    (*obj)["Sleep"] = makeNative("System.Sleep", [](const std::vector<Value>& a) {
        if (!a.empty()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(a[0].asDouble()))
            );
        }
        return Value{};
    });

    (*obj)["Type"] = makeNative("System.Type", [](const std::vector<Value>& a) {
        return Value(a.empty() ? std::string("null") : a[0].typeName());
    });

    (*obj)["Len"] = makeNative("System.Len", [](const std::vector<Value>& a) {
        if (a.empty()) return Value(static_cast<std::int64_t>(0));
        if (a[0].isString()) return Value(static_cast<std::int64_t>(a[0].asString().size()));
        if (a[0].isArray()) return Value(static_cast<std::int64_t>(a[0].asArray()->size()));
        if (a[0].isObject()) return Value(static_cast<std::int64_t>(a[0].asObject()->size()));
        return Value(static_cast<std::int64_t>(a[0].toString().size()));
    });

    (*obj)["Upper"] = makeNative("System.Upper", [](const std::vector<Value>& a) {
        return Value(a.empty() ? std::string{} : toUpperCopy(a[0].asString()));
    });

    (*obj)["Lower"] = makeNative("System.Lower", [](const std::vector<Value>& a) {
        return Value(a.empty() ? std::string{} : toLowerCopy(a[0].asString()));
    });

    (*obj)["Trim"] = makeNative("System.Trim", [](const std::vector<Value>& a) {
        return Value(a.empty() ? std::string{} : trimCopy(a[0].asString()));
    });

    (*obj)["SocketError"] = makeNative("System.SocketError", [](const std::vector<Value>&) {
        return Value(socketErrorString());
    });

    (*obj)["Regex"] = makeNative("System.Regex", [](const std::vector<Value>& a) {
        if (a.empty()) {
            throw std::runtime_error("Regex(pattern[, flags]) requires at least one string argument.");
        }
        const std::string pattern = valueAsText(a[0]);
        const std::string flags = a.size() >= 2 ? valueAsText(a[1]) : std::string{};
        try {
            return makeRegexObject(makeRegexState(pattern, flags));
        } catch (const std::regex_error& e) {
            throw std::runtime_error(std::string("Invalid regular expression: ") + e.what());
        }
    });

    (*obj)["Random"] = makeNative("System.Random", [](const std::vector<Value>&) {
        static std::mt19937_64 rng{std::random_device{}()};
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value(dist(rng));
    });

    (*obj)["ReadFile"] = makeNative("System.ReadFile", [](const std::vector<Value>& a) {
        if (a.empty()) return Value(std::string{});
        std::ifstream in(a[0].asString(), std::ios::binary);
        if (!in) return Value(std::string{});
        std::ostringstream ss;
        ss << in.rdbuf();
        return Value(ss.str());
    });

    (*obj)["WriteFile"] = makeNative("System.WriteFile", [](const std::vector<Value>& a) {
        if (a.size() < 2) return Value(false);
        std::ofstream out(a[0].asString(), std::ios::binary);
        if (!out) return Value(false);
        out << a[1].toString();
        return Value(true);
    });

    (*obj)["Exists"] = makeNative("System.Exists", [](const std::vector<Value>& a) {
        return Value(!a.empty() && fs::exists(a[0].asString()));
    });

    (*obj)["Args"] = makeNative("System.Args", [args](const std::vector<Value>&) {
        auto arr = std::make_shared<Value::Array>();
        for (const auto& s : args) {
            arr->emplace_back(s);
        }
        return Value(arr);
    });

    (*obj)["Exit"] = makeNative("System.Exit", [](const std::vector<Value>& a) {
        int code = a.empty() ? 0 : static_cast<int>(a[0].asDouble());
        std::exit(code);
        return Value{};
    });

    (*obj)["GetEnv"] = makeNative("System.GetEnv", [](const std::vector<Value>& a) {
        if (a.empty()) return Value(std::string{});
        const char* v = std::getenv(a[0].asString().c_str());
        return Value(v ? std::string(v) : std::string{});
    });

    (*obj)["FileSystem"] = makeNative("System.FileSystem", [](const std::vector<Value>&) {
        auto o = std::make_shared<Value::Object>();

        std::string cwd;
        std::string home;
        std::string temp;
        std::string root;

        try { cwd = fs::current_path().string(); } catch (...) { cwd = "N/A"; }
        home = getEnvOr("HOME");
        try { temp = fs::temp_directory_path().string(); } catch (...) { temp = "N/A"; }
        root = fs::path("/").string();

        (*o)["cwd"] = Value(cwd);
        (*o)["home"] = Value(home);
        (*o)["temp"] = Value(temp);
        (*o)["root"] = Value(root);
        return Value(o);
    });

    (*obj)["ShowSystemInfo"] = makeNative("System.ShowSystemInfo", [](const std::vector<Value>&) {
        printSystemInfo();
        return Value{};
    });

    (*obj)["Buffer"] = makeNative("System.Buffer", [](const std::vector<Value>&) {
        return makeBufferObject(std::make_shared<BufferState>());
    });

    (*obj)["ExitSignals"] = makeNative("System.ExitSignals", [](const std::vector<Value>&) {
        auto o = std::make_shared<Value::Object>();

#ifdef SIGHUP
        (*o)["SIGHUP"] = Value(static_cast<std::int64_t>(SIGHUP));
#endif
#ifdef SIGINT
        (*o)["SIGINT"] = Value(static_cast<std::int64_t>(SIGINT));
#endif
#ifdef SIGQUIT
        (*o)["SIGQUIT"] = Value(static_cast<std::int64_t>(SIGQUIT));
#endif
#ifdef SIGILL
        (*o)["SIGILL"] = Value(static_cast<std::int64_t>(SIGILL));
#endif
#ifdef SIGABRT
        (*o)["SIGABRT"] = Value(static_cast<std::int64_t>(SIGABRT));
#endif
#ifdef SIGFPE
        (*o)["SIGFPE"] = Value(static_cast<std::int64_t>(SIGFPE));
#endif
#ifdef SIGKILL
        (*o)["SIGKILL"] = Value(static_cast<std::int64_t>(SIGKILL));
#endif
#ifdef SIGSEGV
        (*o)["SIGSEGV"] = Value(static_cast<std::int64_t>(SIGSEGV));
#endif
#ifdef SIGPIPE
        (*o)["SIGPIPE"] = Value(static_cast<std::int64_t>(SIGPIPE));
#endif
#ifdef SIGALRM
        (*o)["SIGALRM"] = Value(static_cast<std::int64_t>(SIGALRM));
#endif
#ifdef SIGTERM
        (*o)["SIGTERM"] = Value(static_cast<std::int64_t>(SIGTERM));
#endif
#ifdef SIGUSR1
        (*o)["SIGUSR1"] = Value(static_cast<std::int64_t>(SIGUSR1));
#endif
#ifdef SIGUSR2
        (*o)["SIGUSR2"] = Value(static_cast<std::int64_t>(SIGUSR2));
#endif
#ifdef SIGCHLD
        (*o)["SIGCHLD"] = Value(static_cast<std::int64_t>(SIGCHLD));
#endif
#ifdef SIGCONT
        (*o)["SIGCONT"] = Value(static_cast<std::int64_t>(SIGCONT));
#endif
#ifdef SIGSTOP
        (*o)["SIGSTOP"] = Value(static_cast<std::int64_t>(SIGSTOP));
#endif
#ifdef SIGTSTP
        (*o)["SIGTSTP"] = Value(static_cast<std::int64_t>(SIGTSTP));
#endif
#ifdef SIGTTIN
        (*o)["SIGTTIN"] = Value(static_cast<std::int64_t>(SIGTTIN));
#endif
#ifdef SIGTTOU
        (*o)["SIGTTOU"] = Value(static_cast<std::int64_t>(SIGTTOU));
#endif
#ifdef SIGBUS
        (*o)["SIGBUS"] = Value(static_cast<std::int64_t>(SIGBUS));
#endif
#ifdef SIGTRAP
        (*o)["SIGTRAP"] = Value(static_cast<std::int64_t>(SIGTRAP));
#endif
#ifdef SIGSYS
        (*o)["SIGSYS"] = Value(static_cast<std::int64_t>(SIGSYS));
#endif
#ifdef SIGURG
        (*o)["SIGURG"] = Value(static_cast<std::int64_t>(SIGURG));
#endif
#ifdef SIGXCPU
        (*o)["SIGXCPU"] = Value(static_cast<std::int64_t>(SIGXCPU));
#endif
#ifdef SIGXFSZ
        (*o)["SIGXFSZ"] = Value(static_cast<std::int64_t>(SIGXFSZ));
#endif
#ifdef SIGVTALRM
        (*o)["SIGVTALRM"] = Value(static_cast<std::int64_t>(SIGVTALRM));
#endif
#ifdef SIGPROF
        (*o)["SIGPROF"] = Value(static_cast<std::int64_t>(SIGPROF));
#endif
#ifdef SIGWINCH
        (*o)["SIGWINCH"] = Value(static_cast<std::int64_t>(SIGWINCH));
#endif
#ifdef SIGPOLL
        (*o)["SIGPOLL"] = Value(static_cast<std::int64_t>(SIGPOLL));
#endif

        return Value(o);
    });

    (*obj)["Server"] = makeServerNamespace();

    (void)args;
    return Value(obj);
}

} // namespace jdx::runtime
