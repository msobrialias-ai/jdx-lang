#include "interpreter/Interpreter.hpp"
#include "modules/ModuleManager.hpp"
#include "parser/Parser.hpp"
#include "lexer/Lexer.hpp"
#include "runtime/SystemRuntime.hpp"
#include "utils/ErrorHandler.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>

namespace jdx::interpreter {
using namespace jdx::ast;
using namespace jdx::lexer;
using namespace jdx::runtime;

namespace {
struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};

struct StackScope {
    StackScope(const std::string& frame, std::vector<Interpreter::CallFrame>& localStack, Interpreter::CallFrame local)
        : localStack_(localStack) {
        localStack_.push_back(std::move(local));
        utils::pushStackFrame(frame);
    }

    ~StackScope() {
        utils::popStackFrame();
        if (!localStack_.empty()) {
            localStack_.pop_back();
        }
    }

    std::vector<Interpreter::CallFrame>& localStack_;
};

bool isIntegerLike(double v) {
    return std::abs(v - std::round(v)) < 1e-12;
}

Value numericBinary(const Value& lhs, const Value& rhs, const Token& op) {
    if (!lhs.isNumber() || !rhs.isNumber()) {
        utils::raiseRuntimeError(op.filename, op.line, op.column, "Arithmetic operators require numeric operands.");
    }

    const double l = lhs.asDouble();
    const double r = rhs.asDouble();

    switch (op.type) {
        case TokenType::Plus:
            return (lhs.isInt() && rhs.isInt() && isIntegerLike(l + r))
                ? Value(static_cast<std::int64_t>(l + r))
                : Value(l + r);
        case TokenType::Minus:
            return (lhs.isInt() && rhs.isInt() && isIntegerLike(l - r))
                ? Value(static_cast<std::int64_t>(l - r))
                : Value(l - r);
        case TokenType::Star:
            return (lhs.isInt() && rhs.isInt() && isIntegerLike(l * r))
                ? Value(static_cast<std::int64_t>(l * r))
                : Value(l * r);
        case TokenType::Slash:
            if (r == 0.0) {
                utils::raiseRuntimeError(op.filename, op.line, op.column, "Attempted division by zero.");
            }
            return Value(l / r);
        case TokenType::Percent:
            if (r == 0.0) {
                utils::raiseRuntimeError(op.filename, op.line, op.column, "Attempted modulo by zero.");
            }
            return Value(std::fmod(l, r));
        default:
            break;
    }
    return Value{};
}

std::shared_ptr<Value::Object> asObjectOrThrow(const Value& value, const Token& where, const std::string& message) {
    if (!value.isObject()) {
        utils::raiseRuntimeError(where.filename, where.line, where.column, message);
    }
    return value.asObject();
}

} // namespace

Interpreter::Interpreter(std::shared_ptr<Environment> globals, modules::ModuleManager& moduleManager, std::vector<std::string> args)
    : globals_(std::move(globals)), moduleManager_(moduleManager), args_(std::move(args)) {}

std::string Interpreter::formatFrame(const CallFrame& f) const {
    std::ostringstream ss;
    ss << "at " << f.functionName << "() [file '" << f.filename << "', line " << f.line << "]";
    return ss.str();
}

Value Interpreter::executeProgram(const Program& program, const std::string& filename, std::shared_ptr<Environment> env) {
    if (!env) {
        env = globals_;
    }

    CallFrame frame{"main", filename, 1};
    StackScope scope(formatFrame(frame), callStack_, frame);

    try {
        for (const auto& stmt : program.statements) {
            if (dynamic_cast<const ImportStmt*>(stmt.get()) != nullptr) {
                execute(stmt.get(), filename, env);
            }
        }

        for (const auto& stmt : program.statements) {
            if (dynamic_cast<const ImportStmt*>(stmt.get()) == nullptr) {
                execute(stmt.get(), filename, env);
            }
        }
        return Value{};
    } catch (const utils::DiagnosticError&) {
        throw;
    }
}

Value Interpreter::evaluate(const Expr* expr, const std::string&, const std::shared_ptr<Environment>& env) {
    if (auto lit = dynamic_cast<const LiteralExpr*>(expr)) {
        const auto& tok = lit->literalToken;
        switch (tok.type) {
            case TokenType::True: return Value(true);
            case TokenType::False: return Value(false);
            case TokenType::Null: return Value{};
            case TokenType::Number:
                return tok.lexeme.find('.') != std::string::npos
                    ? Value(std::stod(tok.lexeme))
                    : Value(static_cast<std::int64_t>(std::stoll(tok.lexeme)));
            case TokenType::String: return Value(tok.lexeme);
            default: return Value{};
        }
    }

    if (auto var = dynamic_cast<const VariableExpr*>(expr)) {
        Value out;
        if (!env->get(var->name, out)) {
            utils::raiseRuntimeError(var->token.filename, var->token.line, var->token.column, "Undefined variable '" + var->name + "'.");
        }
        return out;
    }

    if (auto assign = dynamic_cast<const AssignExpr*>(expr)) {
        auto value = evaluate(assign->value.get(), assign->token.filename, env);
        if (!env->assign(assign->name, value)) {
            utils::raiseRuntimeError(assign->token.filename, assign->token.line, assign->token.column,
                                     "Undefined variable '" + assign->name + "' or attempted assignment to a constant binding.");
        }
        return value;
    }

    if (auto set = dynamic_cast<const SetExpr*>(expr)) {
        auto object = evaluate(set->object.get(), set->token.filename, env);
        auto obj = asObjectOrThrow(object, set->token, "Member assignment is only valid on objects.");
        auto value = evaluate(set->value.get(), set->token.filename, env);
        (*obj)[set->name] = value;
        return value;
    }

    if (auto unary = dynamic_cast<const UnaryExpr*>(expr)) {
        auto right = evaluate(unary->right.get(), unary->token.filename, env);
        switch (unary->op.type) {
            case TokenType::Minus:
                if (!right.isNumber()) {
                    utils::raiseRuntimeError(unary->op.filename, unary->op.line, unary->op.column, "Unary '-' requires a numeric operand.");
                }
                return right.isInt() ? Value(-right.asInt()) : Value(-right.asDouble());
            case TokenType::Bang:
                return Value(!right.truthy());
            default:
                break;
        }
    }

    if (auto group = dynamic_cast<const GroupingExpr*>(expr)) {
        return evaluate(group->expression.get(), group->token.filename, env);
    }

    if (auto binary = dynamic_cast<const BinaryExpr*>(expr)) {
        auto left = evaluate(binary->left.get(), binary->token.filename, env);
        auto right = evaluate(binary->right.get(), binary->token.filename, env);
        switch (binary->op.type) {
            case TokenType::Plus:
                if (left.isString() || right.isString()) return Value(left.toString() + right.toString());
                return numericBinary(left, right, binary->op);
            case TokenType::Minus:
            case TokenType::Star:
            case TokenType::Slash:
            case TokenType::Percent:
                return numericBinary(left, right, binary->op);
            case TokenType::EqualEqual:
                return Value(left.toString() == right.toString());
            case TokenType::BangEqual:
                return Value(left.toString() != right.toString());
            case TokenType::Less:
                return Value(left.asDouble() < right.asDouble());
            case TokenType::LessEqual:
                return Value(left.asDouble() <= right.asDouble());
            case TokenType::Greater:
                return Value(left.asDouble() > right.asDouble());
            case TokenType::GreaterEqual:
                return Value(left.asDouble() >= right.asDouble());
            default:
                break;
        }
    }

    if (auto get = dynamic_cast<const GetExpr*>(expr)) {
        auto object = evaluate(get->object.get(), get->token.filename, env);
        auto obj = asObjectOrThrow(object, get->token, "Member access is only valid on objects.");
        const auto it = obj->find(get->name);
        if (it == obj->end()) {
            utils::raiseRuntimeError(get->token.filename, get->token.line, get->token.column, "Undefined member '" + get->name + "'.");
        }
        return it->second;
    }

    if (auto imp = dynamic_cast<const ImportExpr*>(expr)) {
        auto path = evaluate(imp->path.get(), imp->token.filename, env);
        if (!path.isString()) {
            utils::raiseRuntimeError(imp->token.filename, imp->token.line, imp->token.column, "import() requires a string path.");
        }
        return importModule(path.asString(), imp->token.filename);
    }

    if (auto call = dynamic_cast<const CallExpr*>(expr)) {
        auto callee = evaluate(call->callee.get(), call->token.filename, env);
        std::vector<Value> args;
        args.reserve(call->arguments.size());
        for (const auto& a : call->arguments) {
            args.push_back(evaluate(a.get(), call->token.filename, env));
        }

        if (callee.isNativeFunction()) {
            return callee.asNativeFunction()->callable(args);
        }

        if (callee.isUserFunction()) {
            auto fn = callee.asUserFunction();
            CallFrame frame{fn->name.empty() ? std::string{"<anonymous>"} : fn->name, fn->declarationFilename, fn->declarationLine};
            StackScope scope(formatFrame(frame), callStack_, frame);
            auto closureEnv = fn->closure.lock();
            auto scopeEnv = std::make_shared<Environment>(closureEnv ? closureEnv : globals_);
            for (std::size_t i = 0; i < fn->params.size(); ++i) {
                scopeEnv->define(fn->params[i], i < args.size() ? args[i] : Value{}, false);
            }
            try {
                for (const auto& stmt : fn->body->statements) {
                    execute(stmt.get(), fn->declarationFilename, scopeEnv);
                }
            } catch (const ReturnSignal& ret) {
                return ret.value;
            }
            return Value{};
        }

        utils::raiseRuntimeError(call->paren.filename, call->paren.line, call->paren.column, "Attempted to call a non-callable value.");
    }

    utils::raiseRuntimeError(expr->token.filename, expr->token.line, expr->token.column, "Unsupported expression form.");
    return Value{};
}

void Interpreter::execute(const Stmt* stmt, const std::string& currentFile, const std::shared_ptr<Environment>& env) {
    (void)currentFile;

    if (auto expr = dynamic_cast<const ExprStmt*>(stmt)) {
        (void)evaluate(expr->expression.get(), expr->token.filename, env);
        return;
    }

    if (auto imp = dynamic_cast<const ImportStmt*>(stmt)) {
        Value moduleValue = importModule(imp->source, imp->token.filename);
        auto moduleObject = asObjectOrThrow(moduleValue, imp->token, "Imported module did not resolve to an object.");
        const auto bindImported = [&](const std::string& localName, const std::string& exportedName) {
            const auto it = moduleObject->find(exportedName);
            if (it == moduleObject->end()) {
                utils::raiseRuntimeError(imp->token.filename, imp->token.line, imp->token.column,
                                         "Module does not export '" + exportedName + "'.");
            }
            env->define(localName, it->second, true);
        };

        if (!imp->defaultLocal.empty()) {
            bindImported(imp->defaultLocal, "default");
        }
        if (!imp->namespaceLocal.empty()) {
            env->define(imp->namespaceLocal, moduleValue, true);
        }
        for (const auto& spec : imp->named) {
            bindImported(spec.local, spec.imported);
        }
        return;
    }

    if (auto var = dynamic_cast<const VarStmt*>(stmt)) {
        Value init = var->initializer ? evaluate(var->initializer.get(), var->token.filename, env) : Value{};
        env->define(var->name, init, var->isConst, var->isExported);
        return;
    }

    if (auto fn = dynamic_cast<const FunctionStmt*>(stmt)) {
        auto user = std::make_shared<UserFunction>();
        user->name = fn->name;
        user->params = fn->params;
        user->body = fn->body;
        user->closure = env;
        user->declarationFilename = fn->token.filename;
        user->declarationLine = fn->token.line;
        user->declarationColumn = fn->token.column;
        user->isExported = fn->isExported;

        const Value fnValue = Value(user);
        if (!fn->name.empty()) {
            env->define(fn->name, fnValue, true, fn->isExported && !fn->isDefaultExport);
        }
        if (fn->isDefaultExport) {
            env->setDefaultExport(fnValue);
            if (!fn->name.empty()) {
                env->exportName(fn->name, "default");
            }
        }
        return;
    }

    if (auto ret = dynamic_cast<const ReturnStmt*>(stmt)) {
        throw ReturnSignal{ret->value ? evaluate(ret->value.get(), ret->token.filename, env) : Value{}};
    }

    if (dynamic_cast<const BreakStmt*>(stmt)) throw BreakSignal{};
    if (dynamic_cast<const ContinueStmt*>(stmt)) throw ContinueSignal{};

    if (auto block = dynamic_cast<const BlockStmt*>(stmt)) {
        auto scope = std::make_shared<Environment>(env);
        for (const auto& s : block->statements) {
            execute(s.get(), block->token.filename, scope);
        }
        return;
    }

    if (auto iff = dynamic_cast<const IfStmt*>(stmt)) {
        if (evaluate(iff->condition.get(), iff->token.filename, env).truthy()) {
            execute(iff->thenBranch.get(), iff->token.filename, env);
            return;
        }
        for (const auto& [cond, branch] : iff->elseIfBranches) {
            if (evaluate(cond.get(), iff->token.filename, env).truthy()) {
                execute(branch.get(), iff->token.filename, env);
                return;
            }
        }
        if (iff->elseBranch) {
            execute(iff->elseBranch.get(), iff->token.filename, env);
        }
        return;
    }

    if (auto wh = dynamic_cast<const WhileStmt*>(stmt)) {
        while (evaluate(wh->condition.get(), wh->token.filename, env).truthy()) {
            try {
                execute(wh->body.get(), wh->token.filename, env);
            } catch (const ContinueSignal&) {
                continue;
            } catch (const BreakSignal&) {
                break;
            }
        }
        return;
    }

    if (auto fr = dynamic_cast<const ForStmt*>(stmt)) {
        auto scope = std::make_shared<Environment>(env);
        if (fr->initializer) {
            execute(fr->initializer.get(), fr->token.filename, scope);
        }
        while (!fr->condition || evaluate(fr->condition.get(), fr->token.filename, scope).truthy()) {
            try {
                execute(fr->body.get(), fr->token.filename, scope);
            } catch (const ContinueSignal&) {
                if (fr->increment) {
                    (void)evaluate(fr->increment.get(), fr->token.filename, scope);
                }
                continue;
            } catch (const BreakSignal&) {
                break;
            }
            if (fr->increment) {
                (void)evaluate(fr->increment.get(), fr->token.filename, scope);
            }
        }
        return;
    }

    if (auto exportList = dynamic_cast<const ExportListStmt*>(stmt)) {
        for (const auto& binding : exportList->bindings) {
            env->exportName(binding.local, binding.exported);
        }
        return;
    }

    if (auto exportDefault = dynamic_cast<const ExportDefaultStmt*>(stmt)) {
        auto value = evaluate(exportDefault->value.get(), exportDefault->token.filename, env);
        env->setDefaultExport(value);
        return;
    }

    utils::raiseRuntimeError(stmt->token.filename, stmt->token.line, stmt->token.column, "Unsupported statement form.");
}

static void populateModuleExports(const std::shared_ptr<Environment>& moduleEnv,
                                  const std::shared_ptr<Value::Object>& moduleObject,
                                  const std::string& filename) {
    for (const auto& [exportedName, localName] : moduleEnv->exports()) {
        Value value;
        if (!moduleEnv->getLocal(localName, value)) {
            utils::raiseRuntimeError(filename, 1, 1, "Export references an undefined local binding '" + localName + "'.");
        }
        (*moduleObject)[exportedName] = value;
    }

    if (moduleEnv->hasDefaultExport()) {
        (*moduleObject)["default"] = *moduleEnv->defaultExport();
    }
}

Value Interpreter::importModule(const std::string& specifier, const std::string& currentFile) {
    const std::string resolved = moduleManager_.resolveModule(specifier, currentFile);

    const auto cached = moduleCache_.find(resolved);
    if (cached != moduleCache_.end()) {
        return Value(cached->second);
    }

    const std::string source = moduleManager_.readModuleSource(resolved);
    lexer::Lexer lexer(resolved, source);
    auto tokens = lexer.tokenize();
    parser::Parser parser(std::move(tokens));
    auto program = parser.parse();

    auto moduleEnv = std::make_shared<Environment>(globals_);
    auto moduleObject = std::make_shared<Value::Object>();
    moduleCache_[resolved] = moduleObject;

    try {
        executeProgram(program, resolved, moduleEnv);
        populateModuleExports(moduleEnv, moduleObject, resolved);
    } catch (...) {
        moduleCache_.erase(resolved);
        throw;
    }

    return Value(moduleObject);
}

} // namespace jdx::interpreter
