#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "interpreter/Interpreter.hpp"
#include "lexer/Lexer.hpp"
#include "modules/ModuleManager.hpp"
#include "parser/Parser.hpp"
#include "runtime/SystemRuntime.hpp"
#include "runtime/Value.hpp"
#include "utils/ErrorHandler.hpp"
#include "utils/Logger.hpp"

namespace {

struct TestFailure final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

jdx::runtime::Value runScript(const std::string& filename,
                              const std::string& source,
                              jdx::interpreter::Interpreter& interpreter,
                              std::shared_ptr<jdx::interpreter::Environment> env) {
    jdx::lexer::Lexer lexer(filename, source);
    const auto tokens = lexer.tokenize();
    jdx::parser::Parser parser(tokens);
    const auto program = parser.parse();
    return interpreter.executeProgram(program, filename, std::move(env));
}

} // namespace

int main() {
    try {
        auto globals = jdx::runtime::makeGlobalEnvironment({});
        jdx::modules::ModuleManager moduleManager(".");
        jdx::interpreter::Interpreter interpreter(globals, moduleManager, {});

        bool rejectedFunction = false;
        try {
            jdx::lexer::Lexer legacyLexer("legacy_function.jdx", "function demo() { return 1; }");
            static_cast<void>(legacyLexer.tokenize());
        } catch (const jdx::utils::DiagnosticError&) {
            rejectedFunction = true;
        }
        require(rejectedFunction, "Legacy keyword 'function' was not rejected.");

        bool rejectedNamedExport = false;
        try {
            jdx::lexer::Lexer legacyLexer("legacy_namedexport.jdx", "NamedExport { a };");
            static_cast<void>(legacyLexer.tokenize());
        } catch (const jdx::utils::DiagnosticError&) {
            rejectedNamedExport = true;
        }
        require(rejectedNamedExport, "Legacy keyword 'NamedExport' was not rejected.");

        {
            const std::string source = R"(
                fname add(a, b) {
                    return a + b;
                }

                let result = add(2, 3);
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("class_test.jdx", source, runInterpreter, env));
            jdx::runtime::Value result;
            require(env->get("result", result), "Result binding missing after class execution.");
            require(result.isInt() && result.asInt() == 5, "Class callable did not evaluate correctly.");
        }

        {
            const std::string source = R"(
                fname divide(a, b) {
                    return a / b;
                }

                let outcome = System.SafeExec(divide, 10, 0);
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("safeexec_test.jdx", source, runInterpreter, env));
            jdx::runtime::Value outcome;
            require(env->get("outcome", outcome), "SafeExec outcome binding missing.");
            require(outcome.isObject(), "SafeExec did not return an object.");
            const auto obj = outcome.asObject();
            require(obj->properties.at("ok").isBool() && !obj->properties.at("ok").asBool(), "SafeExec did not report failure.");
            require(obj->properties.at("error").isString() && !obj->properties.at("error").asString().empty(), "SafeExec error payload missing.");
        }

        {
            const std::string source = R"(
                class Counter {
                    let value = 0;

                    fname init(start) {
                        this.value = start;
                    }

                    fname add(delta) {
                        this.value = this.value + delta;
                        return this.value;
                    }
                }

                let counter = Counter(10);
                let total = counter.add(7);
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("oop_test.jdx", source, runInterpreter, env));
            jdx::runtime::Value total;
            jdx::runtime::Value counter;
            require(env->get("total", total), "OOP total binding missing.");
            require(total.isInt() && total.asInt() == 17, "Method dispatch did not work.");
            require(env->get("counter", counter) && counter.isObject(), "Counter instance missing.");
            require(counter.asObject()->properties.at("value").isInt() && counter.asObject()->properties.at("value").asInt() == 17,
                    "Instance field was not updated.");
        }

        {
            const std::string source = R"(
                async fname add(a, b) {
                    return a + b;
                }

                let task = add(2, 3);
                let result = await task;
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("async_test.jdx", source, runInterpreter, env));
            jdx::runtime::Value task;
            jdx::runtime::Value result;
            require(env->get("task", task) && task.isAsyncTask(), "Async task binding missing.");
            require(env->get("result", result), "Async result binding missing.");
            require(result.isInt() && result.asInt() == 5, "Async/await did not resolve the task.");
        }

        {
            const std::string source = R"(
                let value = 2;
                let label = "unset";

                switch (value) {
                    case 1:
                        label = "one";
                        break;
                    case 2:
                        label = "two";
                        break;
                    default:
                        label = "other";
                }
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("switch_test.jdx", source, runInterpreter, env));
            jdx::runtime::Value label;
            require(env->get("label", label), "Switch label binding missing.");
            require(label.isString() && label.asString() == "two", "Switch/case did not match the correct branch.");
        }

        {
            const std::string source = R"(
                fname inner() {
                    return 1 / 0;
                }

                fname outer() {
                    return inner();
                }

                let outcome = System.SafeExec(outer);
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("stacktrace_runtime.jdx", source, runInterpreter, env));
            jdx::runtime::Value outcome;
            require(env->get("outcome", outcome), "SafeExec outcome missing.");
            require(outcome.isObject(), "SafeExec did not return an object.");
            const auto obj = outcome.asObject();
            require(obj->properties.at("ok").isBool() && !obj->properties.at("ok").asBool(), "SafeExec should report failure.");
            require(obj->properties.at("error").isString(), "SafeExec error text missing.");
            const std::string errorText = obj->properties.at("error").asString();
            require(errorText.find("Stack Trace") != std::string::npos, "Runtime stack trace was not included.");
            require(errorText.find("outer") != std::string::npos && errorText.find("inner") != std::string::npos,
                    "Runtime stack trace did not include function frames.");
        }

        {
            const std::string source = R"(
                let captured = null;

                try {
                    throw "boom";
                } catch (err) {
                    captured = err.message;
                }
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("trycatch_test.jdx", source, runInterpreter, env));
            jdx::runtime::Value captured;
            require(env->get("captured", captured), "Catch binding missing.");
            require(captured.isString() && captured.asString() == "boom", "Try/catch did not capture the thrown value.");
        }


        {
            const std::string source = R"(
                Develoment.Stacktrace.Level = 2;
                Develoment.Stacktrace.Type = "compact";
            )";
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            static_cast<void>(runScript("stacktrace_config.jdx", source, runInterpreter, env));
            require(jdx::utils::ErrorSettings::instance().stacktraceLevel() == 2U, "Stacktrace level was not updated.");
            require(jdx::utils::ErrorSettings::instance().stacktraceType() == "compact", "Stacktrace type was not updated.");
        }

        {
            const auto logDecorated = jdx::utils::Logger::decorate(jdx::utils::LogLevel::Log, "hello");
            const auto warnDecorated = jdx::utils::Logger::decorate(jdx::utils::LogLevel::Warn, "hello");
            const auto errorDecorated = jdx::utils::Logger::decorate(jdx::utils::LogLevel::Error, "hello");
            require(logDecorated.find("\x1b[32m") != std::string::npos, "Log color missing.");
            require(warnDecorated.find("\x1b[33m") != std::string::npos, "Warn color missing.");
            require(errorDecorated.find("\x1b[31m") != std::string::npos, "Error color missing.");
        }

        {
            auto env = jdx::runtime::makeGlobalEnvironment({});
            moduleManager = jdx::modules::ModuleManager(".");
            jdx::interpreter::Interpreter runInterpreter(env, moduleManager, {});
            const std::string source = R"(
                let rx = System.JGex("^:digit+$");
                let testOk = rx.test("12345");
                let search = rx.search("abc12345xyz");
                let replaced = rx.replace("abc12345xyz", "_");
                let parts = rx.split("abc12345xyz");
            )";
            static_cast<void>(runScript("jgex_test.jdx", source, runInterpreter, env));
            jdx::runtime::Value testOk;
            jdx::runtime::Value search;
            jdx::runtime::Value replaced;
            jdx::runtime::Value parts;
            require(env->get("testOk", testOk) && testOk.isBool() && testOk.asBool(), "JGex test failed.");
            require(env->get("search", search) && search.isObject(), "JGex search failed.");
            require(search.asObject()->properties.at("index").isInt() && search.asObject()->properties.at("index").asInt() == 3, "JGex search index mismatch.");
            require(env->get("replaced", replaced) && replaced.isString(), "JGex replace missing.");
            require(replaced.asString() == "abc_xyz", "JGex replace result mismatch.");
            require(env->get("parts", parts) && parts.isArray(), "JGex split missing.");
            require(parts.asArray()->size() >= 2U, "JGex split result too small.");
        }

        std::cout << "All tests passed.\n";
        return 0;
    } catch (const TestFailure& failure) {
        std::cerr << "TEST FAILURE: " << failure.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED ERROR: " << error.what() << '\n';
        return 1;
    }
}
