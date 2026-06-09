#include "runtime/Value.hpp"

#include "gc/GarbageCollector.hpp"
#include "interpreter/Environment.hpp"
#include "interpreter/Interpreter.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace jdx::runtime {

namespace {
template <typename T>
void maybeTrack(const std::shared_ptr<T>& ptr) {
    gc::GarbageCollector::instance().track(ptr);
}
} // namespace

bool Value::isNull() const {
    return std::holds_alternative<std::monostate>(data_);
}

bool Value::isBool() const {
    return std::holds_alternative<bool>(data_);
}

bool Value::isInt() const {
    return std::holds_alternative<std::int64_t>(data_);
}

bool Value::isDouble() const {
    return std::holds_alternative<double>(data_);
}

bool Value::isNumber() const {
    return isInt() || isDouble();
}

bool Value::isString() const {
    return std::holds_alternative<std::string>(data_);
}

bool Value::isArray() const {
    return std::holds_alternative<std::shared_ptr<Array>>(data_);
}

bool Value::isObject() const {
    return std::holds_alternative<std::shared_ptr<Object>>(data_);
}

bool Value::isCallable() const {
    return std::holds_alternative<std::shared_ptr<Callable>>(data_);
}

bool Value::asBool() const {
    return std::get<bool>(data_);
}

std::int64_t Value::asInt() const {
    return std::get<std::int64_t>(data_);
}

double Value::asDouble() const {
    return isInt() ? static_cast<double>(asInt()) : std::get<double>(data_);
}

const std::string& Value::asString() const {
    return std::get<std::string>(data_);
}

std::shared_ptr<Value::Array> Value::asArray() const {
    return std::get<std::shared_ptr<Array>>(data_);
}

std::shared_ptr<Object> Value::asObject() const {
    return std::get<std::shared_ptr<Object>>(data_);
}

std::shared_ptr<Callable> Value::asCallable() const {
    return std::get<std::shared_ptr<Callable>>(data_);
}

std::string Value::typeName() const {
    if (isNull()) {
        return "null";
    }
    if (isBool()) {
        return "bool";
    }
    if (isNumber()) {
        return "number";
    }
    if (isString()) {
        return "string";
    }
    if (isArray()) {
        return "array";
    }
    if (isObject()) {
        return "object";
    }
    if (isCallable()) {
        return "callable";
    }
    return "unknown";
}

std::string Value::toString() const {
    std::ostringstream out;

    if (isNull()) {
        return "null";
    }
    if (isBool()) {
        return asBool() ? "true" : "false";
    }
    if (isInt()) {
        return std::to_string(asInt());
    }
    if (isDouble()) {
        out << std::setprecision(15) << asDouble();
        std::string text = out.str();
        if (text.find('.') != std::string::npos) {
            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.push_back('0');
            }
        }
        return text;
    }
    if (isString()) {
        return asString();
    }
    if (isArray()) {
        out << '[';
        const auto& items = *asArray();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0U) {
                out << ", ";
            }
            out << items[i].toString();
        }
        out << ']';
        return out.str();
    }
    if (isObject()) {
        const auto object = asObject();
        if (!object->tag.empty()) {
            out << '<' << object->tag << ' ';
        }
        out << '{';
        bool first = true;
        for (const auto& [key, value] : object->properties) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << key << ": " << value.toString();
        }
        out << '}';
        if (!object->tag.empty()) {
            out << '>';
        }
        return out.str();
    }
    if (isCallable()) {
        return "<callable " + asCallable()->name() + ">";
    }
    return "<unknown>";
}

bool Value::truthy() const {
    if (isNull()) {
        return false;
    }
    if (isBool()) {
        return asBool();
    }
    if (isInt()) {
        return asInt() != 0;
    }
    if (isDouble()) {
        return std::abs(asDouble()) > 0.0;
    }
    if (isString()) {
        return !asString().empty();
    }
    if (isArray()) {
        return !asArray()->empty();
    }
    if (isObject()) {
        return true;
    }
    return true;
}

bool Value::equals(const Value& rhs) const {
    if (data_.index() != rhs.data_.index()) {
        if (isNumber() && rhs.isNumber()) {
            return asDouble() == rhs.asDouble();
        }
        return false;
    }

    if (isNull()) {
        return true;
    }
    if (isBool()) {
        return asBool() == rhs.asBool();
    }
    if (isInt()) {
        return asInt() == rhs.asInt();
    }
    if (isDouble()) {
        return asDouble() == rhs.asDouble();
    }
    if (isString()) {
        return asString() == rhs.asString();
    }
    if (isArray()) {
        return asArray() == rhs.asArray();
    }
    if (isObject()) {
        return asObject() == rhs.asObject();
    }
    if (isCallable()) {
        return asCallable() == rhs.asCallable();
    }
    return false;
}

Value NativeFunction::call(interpreter::Interpreter& interpreter,
                           const std::vector<Value>& args) const {
    return callback(interpreter, args);
}

std::string BoundCallable::name() const {
    return target ? target->name() : "<bound>";
}

Value BoundCallable::call(interpreter::Interpreter& interpreter,
                          const std::vector<Value>& args) const {
    if (const auto fn = std::dynamic_pointer_cast<FunctionCallable>(target)) {
        return interpreter.callFunction(*fn, args, receiver);
    }
    return target->call(interpreter, args);
}

Value FunctionCallable::call(interpreter::Interpreter& interpreter,
                            const std::vector<Value>& args) const {
    return interpreter.callFunction(*this, args, std::nullopt);
}

Value ClassCallable::call(interpreter::Interpreter& interpreter,
                          const std::vector<Value>& args) const {
    return interpreter.callClass(*this, args);
}

Value makeNull() {
    return Value{};
}

Value makeArray() {
    auto ptr = std::make_shared<Value::Array>();
    maybeTrack(ptr);
    return Value(ptr);
}

Value makeObject(std::string tag) {
    auto ptr = std::make_shared<Object>(std::move(tag));
    maybeTrack(ptr);
    return Value(ptr);
}

Value makeNative(std::string name, NativeFunction::Callback callback) {
    auto ptr = std::make_shared<NativeFunction>(std::move(name), std::move(callback));
    maybeTrack(ptr);
    return Value(ptr);
}

Value makeFunctionCallable(std::string name,
                           std::vector<std::string> params,
                           std::shared_ptr<ast::BlockStmt> body,
                           std::weak_ptr<interpreter::Environment> closure,
                           std::string filename,
                           std::size_t line) {
    auto ptr = std::make_shared<FunctionCallable>(std::move(name),
                                                  std::move(params),
                                                  std::move(body),
                                                  std::move(closure),
                                                  std::move(filename),
                                                  line);
    maybeTrack(ptr);
    return Value(ptr);
}

Value makeBoundCallable(std::shared_ptr<Callable> target, Value receiver) {
    auto ptr = std::make_shared<BoundCallable>(std::move(target), std::move(receiver));
    maybeTrack(ptr);
    return Value(ptr);
}

Value makeClassCallable(std::string name,
                        std::shared_ptr<interpreter::Environment> definition,
                        std::string filename,
                        std::size_t line) {
    auto ptr = std::make_shared<ClassCallable>(std::move(name), std::move(definition), std::move(filename), line);
    maybeTrack(ptr);
    return Value(ptr);
}

} // namespace jdx::runtime
