#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace jdx::ast {
struct BlockStmt;
}

namespace jdx::interpreter {
class Environment;
class Interpreter;
}

namespace jdx::runtime {

struct Object;
class Callable;

class Value {
public:
    using Array = std::vector<Value>;
    using Variant = std::variant<std::monostate,
                                 bool,
                                 std::int64_t,
                                 double,
                                 std::string,
                                 std::shared_ptr<Array>,
                                 std::shared_ptr<Object>,
                                 std::shared_ptr<Callable>>;

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
    Value(std::shared_ptr<Callable> v) : data_(std::move(v)) {}

    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    [[nodiscard]] const Variant& variant() const { return data_; }
    [[nodiscard]] Variant& variant() { return data_; }

    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isBool() const;
    [[nodiscard]] bool isInt() const;
    [[nodiscard]] bool isDouble() const;
    [[nodiscard]] bool isNumber() const;
    [[nodiscard]] bool isString() const;
    [[nodiscard]] bool isArray() const;
    [[nodiscard]] bool isObject() const;
    [[nodiscard]] bool isCallable() const;

    [[nodiscard]] bool asBool() const;
    [[nodiscard]] std::int64_t asInt() const;
    [[nodiscard]] double asDouble() const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] std::shared_ptr<Array> asArray() const;
    [[nodiscard]] std::shared_ptr<Object> asObject() const;
    [[nodiscard]] std::shared_ptr<Callable> asCallable() const;

    [[nodiscard]] std::string typeName() const;
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] bool truthy() const;
    [[nodiscard]] bool equals(const Value& rhs) const;

private:
    Variant data_ {std::monostate{}};
};

struct Object {
    std::unordered_map<std::string, Value> properties;
    std::string tag;

    explicit Object(std::string tagName = {}) : properties(), tag(std::move(tagName)) {}
};

class Callable {
public:
    virtual ~Callable() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual Value call(interpreter::Interpreter& interpreter,
                                     const std::vector<Value>& args) const = 0;
};

struct NativeFunction final : Callable {
    using Callback = std::function<Value(interpreter::Interpreter&, const std::vector<Value>&)>;

    std::string functionName;
    Callback callback;

    NativeFunction(std::string name, Callback fn)
        : functionName(std::move(name)), callback(std::move(fn)) {}

    [[nodiscard]] std::string name() const override { return functionName; }
    [[nodiscard]] Value call(interpreter::Interpreter& interpreter,
                             const std::vector<Value>& args) const override;
};

struct FunctionCallable final : Callable {
    std::string functionName;
    std::vector<std::string> params;
    std::shared_ptr<ast::BlockStmt> body;
    std::weak_ptr<interpreter::Environment> closure;
    std::string declarationFilename;
    std::size_t declarationLine {1};

    FunctionCallable(std::string name,
                     std::vector<std::string> parameters,
                     std::shared_ptr<ast::BlockStmt> block,
                     std::weak_ptr<interpreter::Environment> capture,
                     std::string filename,
                     std::size_t line)
        : functionName(std::move(name)),
          params(std::move(parameters)),
          body(std::move(block)),
          closure(std::move(capture)),
          declarationFilename(std::move(filename)),
          declarationLine(line) {}

    [[nodiscard]] std::string name() const override { return functionName; }
    [[nodiscard]] Value call(interpreter::Interpreter& interpreter,
                             const std::vector<Value>& args) const override;
};

struct BoundCallable final : Callable {
    std::shared_ptr<Callable> target;
    Value receiver;

    BoundCallable(std::shared_ptr<Callable> callable, Value self)
        : target(std::move(callable)), receiver(std::move(self)) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] Value call(interpreter::Interpreter& interpreter,
                             const std::vector<Value>& args) const override;
};

struct ClassCallable final : Callable {
    std::string className;
    std::shared_ptr<interpreter::Environment> definitionEnv;
    std::string declarationFilename;
    std::size_t declarationLine {1};

    ClassCallable(std::string name,
                  std::shared_ptr<interpreter::Environment> definition,
                  std::string filename,
                  std::size_t line)
        : className(std::move(name)),
          definitionEnv(std::move(definition)),
          declarationFilename(std::move(filename)),
          declarationLine(line) {}

    [[nodiscard]] std::string name() const override { return className; }
    [[nodiscard]] Value call(interpreter::Interpreter& interpreter,
                             const std::vector<Value>& args) const override;
};

Value makeNull();
Value makeArray();
Value makeObject(std::string tag = {});
Value makeNative(std::string name, NativeFunction::Callback callback);
Value makeFunctionCallable(std::string name,
                           std::vector<std::string> params,
                           std::shared_ptr<ast::BlockStmt> body,
                           std::weak_ptr<interpreter::Environment> closure,
                           std::string filename,
                           std::size_t line);
Value makeBoundCallable(std::shared_ptr<Callable> target, Value receiver);
Value makeClassCallable(std::string name,
                        std::shared_ptr<interpreter::Environment> definition,
                        std::string filename,
                        std::size_t line);

} // namespace jdx::runtime
