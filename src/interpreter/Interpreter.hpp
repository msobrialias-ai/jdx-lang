#pragma once

#include <memory>
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ast/AST.hpp"
#include "interpreter/Environment.hpp"
#include "runtime/Value.hpp"

namespace jdx::modules {
class ModuleManager;
}

namespace jdx::interpreter {

class Interpreter {
public:
    struct CallFrame {
        std::string functionName;
        std::string filename;
        std::size_t line {1};
    };

    Interpreter(std::shared_ptr<Environment> globals,
                modules::ModuleManager& moduleManager,
                std::vector<std::string> args);

    runtime::Value executeProgram(const ast::Program& program,
                                  const std::string& filename,
                                  std::shared_ptr<Environment> env = nullptr);

    runtime::Value evaluate(const ast::Expr* expr,
                            const std::string& currentFile,
                            const std::shared_ptr<Environment>& env);

    void execute(const ast::Stmt* stmt,
                 const std::string& currentFile,
                 const std::shared_ptr<Environment>& env);

    std::shared_ptr<Environment> importModule(const std::string& specifier,
                                             const std::string& currentFile);

    runtime::Value callValue(const runtime::Value& callee,
                             const std::vector<runtime::Value>& args);

    runtime::Value callFunction(const runtime::FunctionCallable& callable,
                                const std::vector<runtime::Value>& args,
                                const std::optional<runtime::Value>& receiver);

    runtime::Value callFunctionBody(const runtime::FunctionCallable& callable,
                                    const std::vector<runtime::Value>& args,
                                    const std::optional<runtime::Value>& receiver);

    runtime::Value callClass(const runtime::ClassCallable& callable,
                             const std::vector<runtime::Value>& args);

    runtime::Value spawnAsyncFunction(const runtime::FunctionCallable& callable,
                                      const std::vector<runtime::Value>& args,
                                      const std::optional<runtime::Value>& receiver);

    [[nodiscard]] std::string formatFrame(const CallFrame& f) const;
    [[nodiscard]] const std::vector<CallFrame>& callStack() const { return callStack_; }
    [[nodiscard]] const std::vector<std::string>& args() const { return args_; }
    [[nodiscard]] std::shared_ptr<Environment> globals() const { return globals_; }
    [[nodiscard]] const std::string& currentFile() const { return currentFile_; }

private:
    runtime::Value runStatements(const std::vector<ast::StmtPtr>& statements,
                                 const std::string& filename,
                                 const std::shared_ptr<Environment>& env,
                                 bool allowReturn);

    std::shared_ptr<Environment> globals_;
    modules::ModuleManager& moduleManager_;
    std::vector<std::string> args_;
    std::vector<CallFrame> callStack_;
    std::unordered_map<std::string, std::shared_ptr<Environment>> moduleCache_;
    std::string currentFile_;
};

} // namespace jdx::interpreter
