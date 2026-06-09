#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
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

namespace fs = std::filesystem;

namespace {
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Unable to open input file '" + path + "'.");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <script.jdx> [args...]\n";
            return 1;
        }

        const std::string filename = argv[1];
        const std::string source = readFile(filename);

        std::vector<std::string> runtimeArgs;
        runtimeArgs.reserve(argc > 2 ? static_cast<std::size_t>(argc - 2) : 0U);
        for (int i = 2; i < argc; ++i) {
            runtimeArgs.emplace_back(argv[i]);
        }

        auto globals = jdx::runtime::makeGlobalEnvironment(runtimeArgs);
        jdx::modules::ModuleManager moduleManager(fs::current_path().string());
        jdx::interpreter::Interpreter interpreter(globals, moduleManager, runtimeArgs);

        jdx::lexer::Lexer lexer(filename, source);
        const auto tokens = lexer.tokenize();
        jdx::parser::Parser parser(tokens);
        const auto program = parser.parse();
        static_cast<void>(interpreter.executeProgram(program, filename, globals));

        return 0;
    } catch (const jdx::utils::DiagnosticError& error) {
        std::cerr << error.what();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
