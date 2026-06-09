#include "interpreter/Interpreter.hpp"

#include "gc/GarbageCollector.hpp"
#include "lexer/Lexer.hpp"
#include "modules/ModuleManager.hpp"
#include "parser/Parser.hpp"
#include "runtime/SystemRuntime.hpp"
#include "utils/ErrorHandler.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <utility>

namespace jdx::interpreter {
using namespace jdx::ast;
using namespace jdx::lexer;

namespace {
struct ReturnSignal final {
    runtime::Value value;
};

struct BreakSignal final {};
struct ContinueSignal final {};

struct ThrownSignal final {
    runtime::Value value;
};

struct FrameScope final {
    explicit FrameScope(std::vector<Interpreter::CallFrame>& stack,
                        Interpreter::CallFrame frame,
                        const std::string& rendered)
        : stack_(stack) {
        stack_.push_back(std::move(frame));
        utils::pushStackFrame(rendered);
    }

    ~FrameScope() {
        utils::popStackFrame();
        if (!stack_.empty()) {
            stack_.pop_back();
        }
    }

    std::vector<Interpreter::CallFrame>& stack_;
};

struct FileScope final {
    explicit FileScope(std::string& file, std::string next) : file_(file), previous_(std::move(file)) {
        file_ = std::move(next);
    }

    ~FileScope() {
        file_ = std::move(previous_);
    }

    std::string& file_;
    std::string previous_;
};

bool isWholeNumber(const double value) {
    return std::abs(value - std::round(value)) < 1e-12;
}

runtime::Value numberResult(const double value, const bool preferInt) {
    if (preferInt && isWholeNumber(value)) {
        return runtime::Value(static_cast<std::int64_t>(value));
    }
    return runtime::Value(value);
}

runtime::Value makeErrorObject(const std::string& message) {
    auto object = std::make_shared<runtime::Object>("Error");
    object->properties.emplace("message", message);
    return runtime::Value(object);
}

runtime::Value compareLess(const runtime::Value& lhs, const runtime::Value& rhs, const Token& token) {
    if (lhs.isNumber() && rhs.isNumber()) {
        return runtime::Value(lhs.asDouble() < rhs.asDouble());
    }
    if (lhs.isString() && rhs.isString()) {
        return runtime::Value(lhs.asString() < rhs.asString());
    }
    utils::raiseRuntimeError(token.filename, token.line, token.column, "Comparison requires comparable operands.");
}

runtime::Value compareLessEqual(const runtime::Value& lhs, const runtime::Value& rhs, const Token& token) {
    if (lhs.isNumber() && rhs.isNumber()) {
        return runtime::Value(lhs.asDouble() <= rhs.asDouble());
    }
    if (lhs.isString() && rhs.isString()) {
        return runtime::Value(lhs.asString() <= rhs.asString());
    }
    utils::raiseRuntimeError(token.filename, token.line, token.column, "Comparison requires comparable operands.");
}

runtime::Value compareGreater(const runtime::Value& lhs, const runtime::Value& rhs, const Token& token) {
    if (lhs.isNumber() && rhs.isNumber()) {
        return runtime::Value(lhs.asDouble() > rhs.asDouble());
    }
    if (lhs.isString() && rhs.isString()) {
        return runtime::Value(lhs.asString() > rhs.asString());
    }
    utils::raiseRuntimeError(token.filename, token.line, token.column, "Comparison requires comparable operands.");
}

runtime::Value compareGreaterEqual(const runtime::Value& lhs, const runtime::Value& rhs, const Token& token) {
    if (lhs.isNumber() && rhs.isNumber()) {
        return runtime::Value(lhs.asDouble() >= rhs.asDouble());
    }
    if (lhs.isString() && rhs.isString()) {
        return runtime::Value(lhs.asString() >= rhs.asString());
    }
    utils::raiseRuntimeError(token.filename, token.line, token.column, "Comparison requires comparable operands.");
}

} // namespace

Interpreter::Interpreter(std::shared_ptr<Environment> globals,
                         modules::ModuleManager& moduleManager,
                         std::vector<std::string> args)
    : globals_(std::move(globals)),
      moduleManager_(moduleManager),
      args_(std::move(args)) {}

std::string Interpreter::formatFrame(const CallFrame& f) const {
    std::ostringstream out;
    out << f.functionName << " (" << f.filename << ":" << f.line << ")";
    return out.str();
}

runtime::Value Interpreter::executeProgram(const ast::Program& program,
                                           const std::string& filename,
                                           std::shared_ptr<Environment> env) {
    currentFile_ = filename;
    if (env == nullptr) {
        env = globals_;
    }

    try {
        static_cast<void>(runStatements(program.statements, filename, env, false));
    } catch (const ReturnSignal&) {
        utils::raiseRuntimeError(filename, 1U, 1U, "Return statement is not allowed at the top level.");
    } catch (const BreakSignal&) {
        utils::raiseRuntimeError(filename, 1U, 1U, "Break statement escaped its loop.");
    } catch (const ContinueSignal&) {
        utils::raiseRuntimeError(filename, 1U, 1U, "Continue statement escaped its loop.");
    } catch (const ThrownSignal& signal) {
        utils::raiseRuntimeError(filename, 1U, 1U, "Uncaught throw: " + signal.value.toString());
    }

    gc::GarbageCollector::instance().collect();
    return runtime::Value();
}

runtime::Value Interpreter::runStatements(const std::vector<StmtPtr>& statements,
                                          const std::string& filename,
                                          const std::shared_ptr<Environment>& env,
                                          const bool allowReturn) {
    runtime::Value last = runtime::makeNull();
    for (const auto& stmt : statements) {
        try {
            execute(stmt.get(), filename, env);
        } catch (const ReturnSignal& signal) {
            if (allowReturn) {
                return signal.value;
            }
            throw;
        }
        last = runtime::makeNull();
        gc::GarbageCollector::instance().collect();
    }
    return last;
}

void Interpreter::execute(const Stmt* stmt,
                          const std::string& currentFile,
                          const std::shared_ptr<Environment>& env) {
    if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt); exprStmt != nullptr) {
        static_cast<void>(evaluate(exprStmt->expression.get(), currentFile, env));
        return;
    }

    if (const auto* varStmt = dynamic_cast<const VarStmt*>(stmt); varStmt != nullptr) {
        runtime::Value value = runtime::makeNull();
        if (varStmt->initializer != nullptr) {
            value = evaluate(varStmt->initializer.get(), currentFile, env);
        }
        env->define(varStmt->name, value, varStmt->isConst, varStmt->isExported);
        return;
    }

    if (const auto* fnStmt = dynamic_cast<const FnStmt*>(stmt); fnStmt != nullptr) {
        runtime::Value fn = runtime::makeFunctionCallable(fnStmt->name,
                                                         fnStmt->params,
                                                         fnStmt->body,
                                                         env,
                                                         currentFile,
                                                         fnStmt->token.line);
        if (!fnStmt->name.empty()) {
            env->define(fnStmt->name, fn, true, fnStmt->isExported);
            if (fnStmt->isDefaultExport) {
                env->setDefaultExport(fn);
            }
        } else if (fnStmt->isDefaultExport) {
            env->setDefaultExport(fn);
        }
        return;
    }

    if (const auto* classStmt = dynamic_cast<const ClassStmt*>(stmt); classStmt != nullptr) {
        auto classEnv = std::make_shared<Environment>(env);
        static_cast<void>(runStatements(classStmt->body, currentFile, classEnv, false));

        runtime::Value callable = runtime::makeClassCallable(classStmt->name,
                                                             classEnv,
                                                             currentFile,
                                                             classStmt->token.line);
        if (!classStmt->name.empty()) {
            env->define(classStmt->name, callable, true, classStmt->isExported);
            if (classStmt->isDefaultExport) {
                env->setDefaultExport(callable);
            }
        } else if (classStmt->isDefaultExport) {
            env->setDefaultExport(callable);
        }
        return;
    }

    if (const auto* block = dynamic_cast<const BlockStmt*>(stmt); block != nullptr) {
        auto local = std::make_shared<Environment>(env);
        static_cast<void>(runStatements(block->statements, currentFile, local, false));
        return;
    }

    if (const auto* ifStmt = dynamic_cast<const IfStmt*>(stmt); ifStmt != nullptr) {
        if (evaluate(ifStmt->condition.get(), currentFile, env).truthy()) {
            execute(ifStmt->thenBranch.get(), currentFile, env);
            return;
        }
        for (const auto& branch : ifStmt->elseIfBranches) {
            if (evaluate(branch.first.get(), currentFile, env).truthy()) {
                execute(branch.second.get(), currentFile, env);
                return;
            }
        }
        if (ifStmt->elseBranch != nullptr) {
            execute(ifStmt->elseBranch.get(), currentFile, env);
        }
        return;
    }

    if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(stmt); whileStmt != nullptr) {
        while (evaluate(whileStmt->condition.get(), currentFile, env).truthy()) {
            try {
                execute(whileStmt->body.get(), currentFile, env);
            } catch (const ContinueSignal&) {
                continue;
            } catch (const BreakSignal&) {
                break;
            }
        }
        return;
    }

    if (const auto* forStmt = dynamic_cast<const ForStmt*>(stmt); forStmt != nullptr) {
        auto loopEnv = std::make_shared<Environment>(env);
        if (forStmt->initializer != nullptr) {
            execute(forStmt->initializer.get(), currentFile, loopEnv);
        }
        while (!forStmt->condition || evaluate(forStmt->condition.get(), currentFile, loopEnv).truthy()) {
            try {
                execute(forStmt->body.get(), currentFile, loopEnv);
            } catch (const ContinueSignal&) {
                if (forStmt->increment != nullptr) {
                    static_cast<void>(evaluate(forStmt->increment.get(), currentFile, loopEnv));
                }
                continue;
            } catch (const BreakSignal&) {
                break;
            }
            if (forStmt->increment != nullptr) {
                static_cast<void>(evaluate(forStmt->increment.get(), currentFile, loopEnv));
            }
        }
        return;
    }

    if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(stmt); returnStmt != nullptr) {
        runtime::Value value = runtime::makeNull();
        if (returnStmt->value != nullptr) {
            value = evaluate(returnStmt->value.get(), currentFile, env);
        }
        throw ReturnSignal{value};
    }

    if (const auto* throwStmt = dynamic_cast<const ThrowStmt*>(stmt); throwStmt != nullptr) {
        const runtime::Value value = evaluate(throwStmt->value.get(), currentFile, env);
        throw ThrownSignal{value};
    }

    if (dynamic_cast<const BreakStmt*>(stmt) != nullptr) {
        throw BreakSignal{};
    }

    if (dynamic_cast<const ContinueStmt*>(stmt) != nullptr) {
        throw ContinueSignal{};
    }

    if (const auto* tryCatch = dynamic_cast<const TryCatchStmt*>(stmt); tryCatch != nullptr) {
        try {
            execute(tryCatch->tryBlock.get(), currentFile, env);
        } catch (const ReturnSignal&) {
            throw;
        } catch (const BreakSignal&) {
            throw;
        } catch (const ContinueSignal&) {
            throw;
        } catch (const ThrownSignal& signal) {
            auto catchEnv = std::make_shared<Environment>(env);
            catchEnv->define(tryCatch->catchName, makeErrorObject(signal.value.toString()), false, false);
            execute(tryCatch->catchBlock.get(), currentFile, catchEnv);
        } catch (const utils::DiagnosticError& error) {
            auto catchEnv = std::make_shared<Environment>(env);
            catchEnv->define(tryCatch->catchName, makeErrorObject(error.what()), false, false);
            execute(tryCatch->catchBlock.get(), currentFile, catchEnv);
        } catch (const std::exception& error) {
            auto catchEnv = std::make_shared<Environment>(env);
            catchEnv->define(tryCatch->catchName, makeErrorObject(error.what()), false, false);
            execute(tryCatch->catchBlock.get(), currentFile, catchEnv);
        }
        return;
    }

    if (const auto* importStmt = dynamic_cast<const ImportStmt*>(stmt); importStmt != nullptr) {
        auto moduleEnv = importModule(importStmt->source, currentFile);

        if (!importStmt->sideEffectOnly) {
            if (!importStmt->defaultLocal.empty()) {
                if (!moduleEnv->hasDefaultExport()) {
                    utils::raiseRuntimeError(importStmt->token.filename,
                                             importStmt->token.line,
                                             importStmt->token.column,
                                             "Module has no default export.");
                }
                runtime::Value value = moduleEnv->defaultExport().value();
                env->define(importStmt->defaultLocal, value, true, false);
            }

            if (!importStmt->namespaceLocal.empty()) {
                auto namespaceObject = std::make_shared<runtime::Object>("ModuleNamespace");
                if (moduleEnv->hasDefaultExport()) {
                    namespaceObject->properties.emplace("default", moduleEnv->defaultExport().value());
                }
                for (const auto& [exported, local] : moduleEnv->exports()) {
                    runtime::Value value;
                    if (!moduleEnv->getLocal(local, value)) {
                        continue;
                    }
                    namespaceObject->properties.emplace(exported, value);
                }
                env->define(importStmt->namespaceLocal, runtime::Value(namespaceObject), true, false);
            }

            for (const auto& binding : importStmt->named) {
                const auto exportIt = moduleEnv->exports().find(binding.imported);
                if (exportIt == moduleEnv->exports().end()) {
                    utils::raiseRuntimeError(importStmt->token.filename,
                                             importStmt->token.line,
                                             importStmt->token.column,
                                             "Module does not export '" + binding.imported + "'.");
                }
                runtime::Value value;
                if (!moduleEnv->getLocal(exportIt->second, value)) {
                    utils::raiseRuntimeError(importStmt->token.filename,
                                             importStmt->token.line,
                                             importStmt->token.column,
                                             "Export '" + binding.imported + "' is unavailable.");
                }
                env->define(binding.local, value, true, false);
            }
        }

        return;
    }

    if (const auto* exportList = dynamic_cast<const ExportListStmt*>(stmt); exportList != nullptr) {
        for (const auto& binding : exportList->bindings) {
            runtime::Value value;
            if (!env->getLocal(binding.local, value)) {
                utils::raiseRuntimeError(exportList->token.filename,
                                         exportList->token.line,
                                         exportList->token.column,
                                         "Cannot export unknown binding '" + binding.local + "'.");
            }
            env->exportName(binding.local, binding.exported);
        }
        return;
    }

    if (const auto* exportDefault = dynamic_cast<const ExportDefaultStmt*>(stmt); exportDefault != nullptr) {
        runtime::Value value = evaluate(exportDefault->value.get(), currentFile, env);
        env->setDefaultExport(value);
        return;
    }

    utils::raiseRuntimeError(stmt->token.filename,
                             stmt->token.line,
                             stmt->token.column,
                             "Unsupported statement kind.");
}

runtime::Value Interpreter::evaluate(const Expr* expr,
                                     const std::string& currentFile,
                                     const std::shared_ptr<Environment>& env) {
    if (const auto* literal = dynamic_cast<const LiteralExpr*>(expr); literal != nullptr) {
        const std::string& text = literal->literalToken.lexeme;
        switch (literal->literalToken.type) {
            case TokenType::True: return runtime::Value(true);
            case TokenType::False: return runtime::Value(false);
            case TokenType::Null: return runtime::makeNull();
            case TokenType::Number:
                if (text.find('.') != std::string::npos) {
                    return runtime::Value(std::stod(text));
                }
                return runtime::Value(static_cast<std::int64_t>(std::stoll(text)));
            case TokenType::String: return runtime::Value(text);
            default: break;
        }
        return runtime::makeNull();
    }

    if (const auto* variable = dynamic_cast<const VariableExpr*>(expr); variable != nullptr) {
        runtime::Value value;
        if (!env->get(variable->name, value)) {
            utils::raiseRuntimeError(expr->token.filename,
                                     expr->token.line,
                                     expr->token.column,
                                     "Undefined identifier '" + variable->name + "'.");
        }
        return value;
    }

    if (dynamic_cast<const ThisExpr*>(expr) != nullptr) {
        runtime::Value value;
        if (!env->get("this", value)) {
            utils::raiseRuntimeError(expr->token.filename,
                                     expr->token.line,
                                     expr->token.column,
                                     "Cannot use 'this' outside of a method.");
        }
        return value;
    }

    if (const auto* assign = dynamic_cast<const AssignExpr*>(expr); assign != nullptr) {
        const runtime::Value value = evaluate(assign->value.get(), currentFile, env);
        if (!env->assign(assign->name, value)) {
            utils::raiseRuntimeError(assign->token.filename,
                                     assign->token.line,
                                     assign->token.column,
                                     "Cannot assign to unknown or constant variable '" + assign->name + "'.");
        }
        return value;
    }

    if (const auto* grouping = dynamic_cast<const GroupingExpr*>(expr); grouping != nullptr) {
        return evaluate(grouping->expression.get(), currentFile, env);
    }

    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr); unary != nullptr) {
        const runtime::Value right = evaluate(unary->right.get(), currentFile, env);
        if (unary->op.type == TokenType::Bang) {
            return runtime::Value(!right.truthy());
        }
        if (unary->op.type == TokenType::Minus) {
            if (!right.isNumber()) {
                utils::raiseRuntimeError(unary->op.filename, unary->op.line, unary->op.column,
                                         "Unary '-' requires a numeric operand.");
            }
            return right.isInt() ? runtime::Value(-right.asInt()) : runtime::Value(-right.asDouble());
        }
    }

    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr); binary != nullptr) {
        const TokenType opType = binary->op.type;

        if (opType == TokenType::AndAnd) {
            const runtime::Value left = evaluate(binary->left.get(), currentFile, env);
            if (!left.truthy()) {
                return runtime::Value(false);
            }
            return runtime::Value(evaluate(binary->right.get(), currentFile, env).truthy());
        }
        if (opType == TokenType::OrOr) {
            const runtime::Value left = evaluate(binary->left.get(), currentFile, env);
            if (left.truthy()) {
                return runtime::Value(true);
            }
            return runtime::Value(evaluate(binary->right.get(), currentFile, env).truthy());
        }

        const runtime::Value left = evaluate(binary->left.get(), currentFile, env);
        const runtime::Value right = evaluate(binary->right.get(), currentFile, env);

        switch (opType) {
            case TokenType::Plus:
                if (left.isString() || right.isString()) {
                    return runtime::Value(left.toString() + right.toString());
                }
                if (left.isNumber() && right.isNumber()) {
                    const bool preferInt = left.isInt() && right.isInt();
                    return numberResult(left.asDouble() + right.asDouble(), preferInt);
                }
                break;
            case TokenType::Minus:
                if (left.isNumber() && right.isNumber()) {
                    const bool preferInt = left.isInt() && right.isInt();
                    return numberResult(left.asDouble() - right.asDouble(), preferInt);
                }
                break;
            case TokenType::Star:
                if (left.isNumber() && right.isNumber()) {
                    const bool preferInt = left.isInt() && right.isInt();
                    return numberResult(left.asDouble() * right.asDouble(), preferInt);
                }
                break;
            case TokenType::Slash:
                if (left.isNumber() && right.isNumber()) {
                    if (right.asDouble() == 0.0) {
                        utils::raiseRuntimeError(binary->op.filename, binary->op.line, binary->op.column,
                                                 "Attempted division by zero.");
                    }
                    return runtime::Value(left.asDouble() / right.asDouble());
                }
                break;
            case TokenType::Percent:
                if (left.isNumber() && right.isNumber()) {
                    if (right.asDouble() == 0.0) {
                        utils::raiseRuntimeError(binary->op.filename, binary->op.line, binary->op.column,
                                                 "Attempted modulo by zero.");
                    }
                    return runtime::Value(std::fmod(left.asDouble(), right.asDouble()));
                }
                break;
            case TokenType::EqualEqual:
                return runtime::Value(left.equals(right));
            case TokenType::BangEqual:
                return runtime::Value(!left.equals(right));
            case TokenType::Less:
                return compareLess(left, right, binary->op);
            case TokenType::LessEqual:
                return compareLessEqual(left, right, binary->op);
            case TokenType::Greater:
                return compareGreater(left, right, binary->op);
            case TokenType::GreaterEqual:
                return compareGreaterEqual(left, right, binary->op);
            default:
                break;
        }

        utils::raiseRuntimeError(binary->op.filename,
                                 binary->op.line,
                                 binary->op.column,
                                 "Operands are not compatible with operator '" + binary->op.lexeme + "'.");
    }

    if (const auto* call = dynamic_cast<const CallExpr*>(expr); call != nullptr) {
        const runtime::Value callee = evaluate(call->callee.get(), currentFile, env);
        std::vector<runtime::Value> args;
        args.reserve(call->arguments.size());
        for (const auto& arg : call->arguments) {
            args.push_back(evaluate(arg.get(), currentFile, env));
        }
        return callValue(callee, args);
    }

    if (const auto* get = dynamic_cast<const GetExpr*>(expr); get != nullptr) {
        const runtime::Value object = evaluate(get->object.get(), currentFile, env);
        if (!object.isObject()) {
            utils::raiseRuntimeError(expr->token.filename,
                                     expr->token.line,
                                     expr->token.column,
                                     "Property access requires an object value.");
        }
        const auto instance = object.asObject();
        const auto found = instance->properties.find(get->name);
        if (found == instance->properties.end()) {
            utils::raiseRuntimeError(expr->token.filename,
                                     expr->token.line,
                                     expr->token.column,
                                     "Object has no property named '" + get->name + "'.");
        }
        if (found->second.isCallable()) {
            return runtime::makeBoundCallable(found->second.asCallable(), object);
        }
        return found->second;
    }

    if (const auto* set = dynamic_cast<const SetExpr*>(expr); set != nullptr) {
        const runtime::Value object = evaluate(set->object.get(), currentFile, env);
        if (!object.isObject()) {
            utils::raiseRuntimeError(expr->token.filename,
                                     expr->token.line,
                                     expr->token.column,
                                     "Property assignment requires an object value.");
        }
        const runtime::Value value = evaluate(set->value.get(), currentFile, env);
        const auto instance = object.asObject();
        instance->properties[set->name] = value;
        if (instance->tag == "Develoment.Stacktrace") {
            if (set->name == "Level") {
                if (value.isInt()) {
                    utils::ErrorSettings::instance().setStacktraceLevel(static_cast<std::size_t>(value.asInt()));
                } else if (value.isDouble()) {
                    utils::ErrorSettings::instance().setStacktraceLevel(static_cast<std::size_t>(value.asDouble()));
                } else {
                    utils::raiseRuntimeError(expr->token.filename,
                                             expr->token.line,
                                             expr->token.column,
                                             "Stacktrace.Level expects a numeric value.");
                }
            } else if (set->name == "Type") {
                if (!value.isString()) {
                    utils::raiseRuntimeError(expr->token.filename,
                                             expr->token.line,
                                             expr->token.column,
                                             "Stacktrace.Type expects a string value.");
                }
                utils::ErrorSettings::instance().setStacktraceType(value.asString());
            }
        }
        return value;
    }

    utils::raiseRuntimeError(expr->token.filename,
                             expr->token.line,
                             expr->token.column,
                             "Unsupported expression kind.");
}

std::shared_ptr<Environment> Interpreter::importModule(const std::string& specifier,
                                                       const std::string& currentFile) {
    const std::string resolved = moduleManager_.resolveModule(specifier, currentFile);
    const auto cached = moduleCache_.find(resolved);
    if (cached != moduleCache_.end()) {
        return cached->second;
    }

    const std::string source = moduleManager_.readModuleSource(resolved);
    lexer::Lexer lexer(resolved, source);
    const std::vector<Token> tokens = lexer.tokenize();
    parser::Parser parser(tokens);
    const ast::Program program = parser.parse();

    auto moduleEnv = std::make_shared<Environment>(globals_);
    static_cast<void>(executeProgram(program, resolved, moduleEnv));
    moduleCache_[resolved] = moduleEnv;
    return moduleEnv;
}

runtime::Value Interpreter::callValue(const runtime::Value& callee,
                                      const std::vector<runtime::Value>& args) {
    if (!callee.isCallable()) {
        utils::raiseRuntimeError(currentFile_.empty() ? std::string{"<runtime>"} : currentFile_,
                                 1U,
                                 1U,
                                 "Attempted to call a non-callable value of type '" + callee.typeName() + "'.");
    }
    return callee.asCallable()->call(*this, args);
}

runtime::Value Interpreter::callFunction(const runtime::FunctionCallable& callable,
                                         const std::vector<runtime::Value>& args,
                                         const std::optional<runtime::Value>& receiver) {
    if (args.size() != callable.params.size()) {
        utils::raiseRuntimeError(callable.declarationFilename,
                                 callable.declarationLine,
                                 1U,
                                 "Callable '" + callable.functionName + "' expects " + std::to_string(callable.params.size()) + " argument(s).");
    }

    auto closure = callable.closure.lock();
    if (closure == nullptr) {
        closure = globals_;
    }
    auto local = std::make_shared<Environment>(closure);

    if (receiver.has_value()) {
        local->define("this", receiver.value(), false, false);
    }

    for (std::size_t i = 0; i < callable.params.size(); ++i) {
        local->define(callable.params[i], args[i], false, false);
    }

    CallFrame frame{callable.functionName.empty() ? std::string{"<anonymous fn>"} : callable.functionName,
                    callable.declarationFilename,
                    callable.declarationLine};
    const std::string rendered = formatFrame(frame);
    FrameScope scope(callStack_, frame, rendered);
    FileScope fileScope(currentFile_, callable.declarationFilename);

    try {
        return runStatements(callable.body->statements, callable.declarationFilename, local, true);
    } catch (const ReturnSignal& signal) {
        return signal.value;
    } catch (const BreakSignal&) {
        utils::raiseRuntimeError(callable.declarationFilename,
                                 callable.declarationLine,
                                 1U,
                                 "Break statement escaped its function.");
    } catch (const ContinueSignal&) {
        utils::raiseRuntimeError(callable.declarationFilename,
                                 callable.declarationLine,
                                 1U,
                                 "Continue statement escaped its function.");
    }
}

runtime::Value Interpreter::callClass(const runtime::ClassCallable& callable,
                                      const std::vector<runtime::Value>& args) {
    auto classEnv = callable.definitionEnv;
    if (classEnv == nullptr) {
        classEnv = globals_;
    }

    auto instance = std::make_shared<runtime::Object>(callable.className.empty() ? "Instance" : callable.className);
    const auto names = classEnv->localNames();
    for (const auto& name : names) {
        runtime::Value value;
        if (classEnv->getLocal(name, value)) {
            instance->properties[name] = value;
        }
    }

    runtime::Value instanceValue(instance);
    runtime::Value initValue;
    if (instance->properties.count("init") != 0U) {
        initValue = instance->properties.at("init");
        if (!initValue.isCallable()) {
            utils::raiseRuntimeError(callable.declarationFilename,
                                     callable.declarationLine,
                                     1U,
                                     "Class initializer 'init' is not callable.");
        }
        auto bound = runtime::makeBoundCallable(initValue.asCallable(), instanceValue);
        static_cast<void>(callValue(bound, args));
    } else if (!args.empty()) {
        utils::raiseRuntimeError(callable.declarationFilename,
                                 callable.declarationLine,
                                 1U,
                                 "Class '" + callable.className + "' does not accept constructor arguments.");
    }

    return instanceValue;
}

} // namespace jdx::interpreter
