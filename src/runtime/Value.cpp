#include "runtime/Value.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace jdx::runtime {

bool Value::isNull() const { return std::holds_alternative<std::monostate>(data_); }
bool Value::isBool() const { return std::holds_alternative<bool>(data_); }
bool Value::isInt() const { return std::holds_alternative<std::int64_t>(data_); }
bool Value::isDouble() const { return std::holds_alternative<double>(data_); }
bool Value::isNumber() const { return isInt() || isDouble(); }
bool Value::isString() const { return std::holds_alternative<std::string>(data_); }
bool Value::isArray() const { return std::holds_alternative<std::shared_ptr<Array>>(data_); }
bool Value::isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(data_); }
bool Value::isNativeFunction() const { return std::holds_alternative<std::shared_ptr<NativeFunction>>(data_); }
bool Value::isUserFunction() const { return std::holds_alternative<std::shared_ptr<UserFunction>>(data_); }

bool Value::asBool() const { return std::get<bool>(data_); }
std::int64_t Value::asInt() const { return std::get<std::int64_t>(data_); }
double Value::asDouble() const { return isInt() ? static_cast<double>(asInt()) : std::get<double>(data_); }
const std::string& Value::asString() const { return std::get<std::string>(data_); }
std::shared_ptr<Value::Array> Value::asArray() const { return std::get<std::shared_ptr<Array>>(data_); }
std::shared_ptr<Value::Object> Value::asObject() const { return std::get<std::shared_ptr<Object>>(data_); }
std::shared_ptr<NativeFunction> Value::asNativeFunction() const { return std::get<std::shared_ptr<NativeFunction>>(data_); }
std::shared_ptr<UserFunction> Value::asUserFunction() const { return std::get<std::shared_ptr<UserFunction>>(data_); }

std::string Value::typeName() const {
    if (isNull()) return "null";
    if (isBool()) return "bool";
    if (isInt() || isDouble()) return "number";
    if (isString()) return "string";
    if (isArray()) return "array";
    if (isObject()) return "object";
    if (isNativeFunction() || isUserFunction()) return "function";
    return "unknown";
}

std::string Value::toString() const {
    std::ostringstream out;
    if (isNull()) return "null";
    if (isBool()) return asBool() ? "true" : "false";
    if (isInt()) return std::to_string(asInt());
    if (isDouble()) {
        out << std::setprecision(15) << asDouble();
        std::string s = out.str();
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.push_back('0');
        }
        return s;
    }
    if (isString()) return asString();
    if (isArray()) {
        out << '[';
        const auto& arr = *asArray();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i) out << ", ";
            out << arr[i].toString();
        }
        out << ']';
        return out.str();
    }
    if (isObject()) {
        out << '{';
        const auto& obj = *asObject();
        bool first = true;
        for (const auto& [k, v] : obj) {
            if (!first) out << ", ";
            first = false;
            out << k << ": " << v.toString();
        }
        out << '}';
        return out.str();
    }
    if (isNativeFunction()) return "<native function>";
    if (isUserFunction()) return "<function>";
    return "<unknown>";
}

bool Value::truthy() const {
    if (isNull()) return false;
    if (isBool()) return asBool();
    if (isInt()) return asInt() != 0;
    if (isDouble()) return std::abs(asDouble()) > 0.0;
    if (isString()) return !asString().empty();
    if (isArray()) return !asArray()->empty();
    if (isObject()) return !asObject()->empty();
    return true;
}

Value makeNull() { return Value{}; }
Value makeObject() { return Value(std::make_shared<Value::Object>()); }
Value makeArray() { return Value(std::make_shared<Value::Array>()); }

} // namespace jdx::runtime
