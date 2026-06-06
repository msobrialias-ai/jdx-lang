#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "ast/AST.hpp"
#include "runtime/Value.hpp"
#include "interpreter/Environment.hpp"

namespace jdx::modules { class ModuleManager; }

namespace jdx::interpreter {

class Interpreter {
public:
    struct CallFrame {
        std::string functionName;
        std::string filename;
        std::size_t line {1};
    };

    Interpreter(std::shared_ptr<Environment> globals, modules::ModuleManager& moduleManager, std::vector<std::string> args);

    runtime::Value executeProgram(const ast::Program& program, const std::string& filename, std::shared_ptr<Environment> env = nullptr);
    runtime::Value evaluate(const ast::Expr* expr, const std::string& currentFile, const std::shared_ptr<Environment>& env);
    void execute(const ast::Stmt* stmt, const std::string& currentFile, const std::shared_ptr<Environment>& env);
    runtime::Value importModule(const std::string& specifier, const std::string& currentFile);
    std::string formatFrame(const CallFrame& f) const;
    const std::vector<CallFrame>& callStack() const { return callStack_; }
    const std::vector<std::string>& args() const { return args_; }
    std::shared_ptr<Environment> globals() const { return globals_; }

private:
    std::shared_ptr<Environment> globals_;
    modules::ModuleManager& moduleManager_;
    std::vector<std::string> args_;
    std::vector<CallFrame> callStack_;
    std::unordered_map<std::string, std::shared_ptr<runtime::Value::Object>> moduleCache_;
};

} // namespace jdx::interpreter
