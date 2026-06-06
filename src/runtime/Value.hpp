#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <optional>
#include <cstdint>

namespace jdx::ast { struct Stmt; struct BlockStmt; }
namespace jdx::interpreter { class Environment; }
namespace jdx::lexer { struct Token; }

namespace jdx::runtime {

struct NativeFunction {
    std::string name;
    std::function<class Value(const std::vector<class Value>&)> callable;
};

struct UserFunction {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<ast::BlockStmt> body;
    std::weak_ptr<interpreter::Environment> closure;
    std::string declarationFilename;
    std::size_t declarationLine {1};
    std::size_t declarationColumn {1};
    bool isExported {false};
};

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::unordered_map<std::string, Value>;
    using Variant = std::variant<std::monostate, bool, std::int64_t, double, std::string,
                                 std::shared_ptr<Array>, std::shared_ptr<Object>,
                                 std::shared_ptr<NativeFunction>, std::shared_ptr<UserFunction>>;

    Value() = default;
    Value(std::nullptr_t) : data_(std::monostate{}) {}
    Value(bool v) : data_(v) {}
    Value(std::int64_t v) : data_(v) {}
    Value(int v) : data_(static_cast<std::int64_t>(v)) {}
    Value(double v) : data_(v) {}
    Value(std::string v) : data_(std::move(v)) {}
    Value(const char* v) : data_(std::string(v)) {}
    Value(std::shared_ptr<Array> v) : data_(std::move(v)) {}
    Value(std::shared_ptr<Object> v) : data_(std::move(v)) {}
    Value(std::shared_ptr<NativeFunction> v) : data_(std::move(v)) {}
    Value(std::shared_ptr<UserFunction> v) : data_(std::move(v)) {}

    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    const Variant& variant() const { return data_; }
    Variant& variant() { return data_; }

    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isDouble() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;
    bool isNativeFunction() const;
    bool isUserFunction() const;

    bool asBool() const;
    std::int64_t asInt() const;
    double asDouble() const;
    const std::string& asString() const;
    std::shared_ptr<Array> asArray() const;
    std::shared_ptr<Object> asObject() const;
    std::shared_ptr<NativeFunction> asNativeFunction() const;
    std::shared_ptr<UserFunction> asUserFunction() const;

    std::string typeName() const;
    std::string toString() const;
    bool truthy() const;

private:
    Variant data_ {std::monostate{}};
};

Value makeNull();
Value makeObject();
Value makeArray();

} // namespace jdx::runtime
