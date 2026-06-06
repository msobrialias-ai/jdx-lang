#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast/AST.hpp"
#include "interpreter/Environment.hpp"
#include "interpreter/Interpreter.hpp"
#include "lexer/Lexer.hpp"
#include "modules/ModuleManager.hpp"
#include "parser/Parser.hpp"
#include "runtime/SystemRuntime.hpp"
#include "runtime/Value.hpp"
#include "utils/ErrorHandler.hpp"

namespace fs = std::filesystem;

namespace {

enum class Mode {
    Run,
    Eval,
    Repl,
    Format
};

struct Options {
    Mode mode {Mode::Run};
    bool watch {false};
    bool write {false};
    std::string filename;
    std::string source;
    std::vector<std::string> runtimeArgs;
};

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Unable to open input file '" + path + "'.");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Unable to write output file '" + path + "'.");
    }
    out << content;
    if (!out) {
        throw std::runtime_error("Failed while writing output file '" + path + "'.");
    }
}

std::string trimRight(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.pop_back();
    }
    return s;
}

bool needsTrailingSemicolon(const std::string& source) {
    const std::string trimmed = trimRight(source);
    if (trimmed.empty()) {
        return false;
    }
    const char tail = trimmed.back();
    return tail != ';' && tail != '{' && tail != '}';
}

std::string indent(const std::size_t level) {
    return std::string(level * 4U, ' ');
}

std::string escapeStringLiteral(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 2U);
    out.push_back('"');
    for (const char ch : input) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    out.push_back('"');
    return out;
}

std::string tokenToKeyword(const jdx::lexer::TokenType type) {
    using jdx::lexer::TokenType;
    switch (type) {
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::EqualEqual: return "==";
        case TokenType::BangEqual: return "!=";
        case TokenType::Less: return "<";
        case TokenType::LessEqual: return "<=";
        case TokenType::Greater: return ">";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::Bang: return "!";
        default: break;
    }
    return {};
}

int precedenceOf(const jdx::lexer::TokenType type) {
    using jdx::lexer::TokenType;
    switch (type) {
        case TokenType::Equal:
            return 1;
        case TokenType::EqualEqual:
        case TokenType::BangEqual:
            return 2;
        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual:
            return 3;
        case TokenType::Plus:
        case TokenType::Minus:
            return 4;
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:
            return 5;
        case TokenType::Bang:
            return 6;
        default:
            return 0;
    }
}

std::string formatExpr(const jdx::ast::Expr* expr, int parentPrecedence = 0);
std::string formatStmt(const jdx::ast::Stmt* stmt, std::size_t level = 0);

std::string formatExprList(const std::vector<jdx::ast::ExprPtr>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += formatExpr(items[i].get());
    }
    return out;
}

std::string formatBlock(const jdx::ast::BlockStmt* block, const std::size_t level) {
    std::string out;
    out += "{\n";
    for (std::size_t i = 0; i < block->statements.size(); ++i) {
        out += indent(level + 1U);
        out += formatStmt(block->statements[i].get(), level + 1U);
        out += "\n";
    }
    out += indent(level);
    out += '}';
    return out;
}

std::string formatEmbeddedStmt(const jdx::ast::Stmt* stmt, const std::size_t level) {
    if (const auto* block = dynamic_cast<const jdx::ast::BlockStmt*>(stmt); block != nullptr) {
        return formatBlock(block, level);
    }
    std::string out;
    out += "{\n";
    out += indent(level + 1U);
    out += formatStmt(stmt, level + 1U);
    out += "\n";
    out += indent(level);
    out += '}';
    return out;
}

std::string formatVarDecl(const jdx::ast::VarStmt* var, const bool includeExport) {
    std::string out;
    if (includeExport && var->isExported) {
        out += "export ";
    }
    out += var->isConst ? "const " : "let ";
    out += var->name;
    if (var->initializer) {
        out += " = ";
        out += formatExpr(var->initializer.get());
    }
    out += ';';
    return out;
}

std::string formatForInitializer(const jdx::ast::Stmt* stmt) {
    if (const auto* var = dynamic_cast<const jdx::ast::VarStmt*>(stmt); var != nullptr) {
        std::string text = formatVarDecl(var, false);
        if (!text.empty() && text.back() == ';') {
            text.pop_back();
        }
        return text;
    }
    if (const auto* expr = dynamic_cast<const jdx::ast::ExprStmt*>(stmt); expr != nullptr) {
        return formatExpr(expr->expression.get());
    }
    throw std::runtime_error("Formatter does not support this for-loop initializer form.");
}

std::string formatFunction(const jdx::ast::FunctionStmt* fn, const std::size_t level) {
    std::string out;
    if (fn->isExported && !fn->isDefaultExport) {
        out += "export ";
    }
    if (fn->isDefaultExport) {
        out += "export default ";
    }
    out += "fname";
    if (!fn->name.empty()) {
        out += ' ';
        out += fn->name;
    }
    out += '(';
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += fn->params[i];
    }
    out += ") ";
    out += formatBlock(fn->body.get(), level);
    return out;
}

std::string formatImport(const jdx::ast::ImportStmt* imp) {
    std::string out;
    out += "import ";
    if (imp->sideEffectOnly) {
        out += escapeStringLiteral(imp->source);
        out += ';';
        return out;
    }

    bool needComma = false;
    if (!imp->defaultLocal.empty()) {
        out += imp->defaultLocal;
        needComma = true;
    }
    if (!imp->namespaceLocal.empty()) {
        if (needComma) {
            out += ", ";
        }
        out += "* as ";
        out += imp->namespaceLocal;
        needComma = true;
    }
    if (!imp->named.empty()) {
        if (needComma) {
            out += ", ";
        }
        out += '{';
        for (std::size_t i = 0; i < imp->named.size(); ++i) {
            if (i != 0U) {
                out += ", ";
            }
            out += imp->named[i].imported;
            if (imp->named[i].local != imp->named[i].imported) {
                out += " as ";
                out += imp->named[i].local;
            }
        }
        out += "} ";
        out += "from ";
    } else {
        out += " from ";
    }
    out += escapeStringLiteral(imp->source);
    out += ';';
    return out;
}

std::string formatExportList(const jdx::ast::ExportListStmt* stmt) {
    std::string out;
    out += "export { ";
    for (std::size_t i = 0; i < stmt->bindings.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += stmt->bindings[i].local;
        if (stmt->bindings[i].exported != stmt->bindings[i].local) {
            out += " as ";
            out += stmt->bindings[i].exported;
        }
    }
    out += " };";
    return out;
}

std::string formatExpr(const jdx::ast::Expr* expr, const int parentPrecedence) {
    using namespace jdx::ast;
    using jdx::lexer::TokenType;

    if (const auto* lit = dynamic_cast<const LiteralExpr*>(expr); lit != nullptr) {
        switch (lit->literalToken.type) {
            case TokenType::True: return "true";
            case TokenType::False: return "false";
            case TokenType::Null: return "null";
            case TokenType::Number: return lit->literalToken.lexeme;
            case TokenType::String: return escapeStringLiteral(lit->literalToken.lexeme);
            default: break;
        }
    }
    if (const auto* var = dynamic_cast<const VariableExpr*>(expr); var != nullptr) {
        return var->name;
    }
    if (const auto* assign = dynamic_cast<const AssignExpr*>(expr); assign != nullptr) {
        const int myPrec = 1;
        std::string text = assign->name + " = " + formatExpr(assign->value.get(), myPrec);
        if (myPrec < parentPrecedence) {
            return "(" + text + ")";
        }
        return text;
    }
    if (const auto* set = dynamic_cast<const SetExpr*>(expr); set != nullptr) {
        const int myPrec = 1;
        std::string text = formatExpr(set->object.get(), 7) + '.' + set->name + " = " + formatExpr(set->value.get(), myPrec);
        if (myPrec < parentPrecedence) {
            return "(" + text + ")";
        }
        return text;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr); unary != nullptr) {
        const int myPrec = 6;
        std::string text = tokenToKeyword(unary->op.type) + formatExpr(unary->right.get(), myPrec);
        if (myPrec < parentPrecedence) {
            return '(' + text + ')';
        }
        return text;
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr); binary != nullptr) {
        const int myPrec = precedenceOf(binary->op.type);
        if (myPrec == 0) {
            throw std::runtime_error("Formatter does not support this binary operator.");
        }
        std::string text = formatExpr(binary->left.get(), myPrec) + ' ' + tokenToKeyword(binary->op.type) + ' ' + formatExpr(binary->right.get(), myPrec + 1);
        if (myPrec < parentPrecedence) {
            return '(' + text + ')';
        }
        return text;
    }
    if (const auto* group = dynamic_cast<const GroupingExpr*>(expr); group != nullptr) {
        return '(' + formatExpr(group->expression.get()) + ')';
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expr); call != nullptr) {
        std::string text = formatExpr(call->callee.get(), 7);
        text += '(';
        text += formatExprList(call->arguments);
        text += ')';
        if (7 < parentPrecedence) {
            return '(' + text + ')';
        }
        return text;
    }
    if (const auto* get = dynamic_cast<const GetExpr*>(expr); get != nullptr) {
        std::string text = formatExpr(get->object.get(), 7);
        text += '.';
        text += get->name;
        if (7 < parentPrecedence) {
            return '(' + text + ')';
        }
        return text;
    }
    if (const auto* imp = dynamic_cast<const ImportExpr*>(expr); imp != nullptr) {
        std::string text = "import(" + formatExpr(imp->path.get()) + ')';
        if (7 < parentPrecedence) {
            return '(' + text + ')';
        }
        return text;
    }

    throw std::runtime_error("Formatter encountered an unsupported expression node.");
}

std::string formatStmt(const jdx::ast::Stmt* stmt, const std::size_t level) {
    using namespace jdx::ast;

    if (const auto* expr = dynamic_cast<const ExprStmt*>(stmt); expr != nullptr) {
        return formatExpr(expr->expression.get()) + ';';
    }
    if (const auto* var = dynamic_cast<const VarStmt*>(stmt); var != nullptr) {
        return formatVarDecl(var, true);
    }
    if (const auto* ret = dynamic_cast<const ReturnStmt*>(stmt); ret != nullptr) {
        std::string out = "return";
        if (ret->value) {
            out += ' ';
            out += formatExpr(ret->value.get());
        }
        out += ';';
        return out;
    }
    if (dynamic_cast<const BreakStmt*>(stmt) != nullptr) {
        return "break;";
    }
    if (dynamic_cast<const ContinueStmt*>(stmt) != nullptr) {
        return "continue;";
    }
    if (const auto* block = dynamic_cast<const BlockStmt*>(stmt); block != nullptr) {
        return formatBlock(block, level);
    }
    if (const auto* iff = dynamic_cast<const IfStmt*>(stmt); iff != nullptr) {
        std::string out = "if (";
        out += formatExpr(iff->condition.get());
        out += ") ";
        out += formatEmbeddedStmt(iff->thenBranch.get(), level);
        for (const auto& [cond, branch] : iff->elseIfBranches) {
            out += " elif (";
            out += formatExpr(cond.get());
            out += ") ";
            out += formatEmbeddedStmt(branch.get(), level);
        }
        if (iff->elseBranch) {
            out += " else ";
            out += formatEmbeddedStmt(iff->elseBranch.get(), level);
        }
        return out;
    }
    if (const auto* wh = dynamic_cast<const WhileStmt*>(stmt); wh != nullptr) {
        std::string out = "while (";
        out += formatExpr(wh->condition.get());
        out += ") ";
        out += formatEmbeddedStmt(wh->body.get(), level);
        return out;
    }
    if (const auto* fr = dynamic_cast<const ForStmt*>(stmt); fr != nullptr) {
        std::string out = "for (";
        if (fr->initializer) {
            out += formatForInitializer(fr->initializer.get());
        }
        out += "; ";
        if (fr->condition) {
            out += formatExpr(fr->condition.get());
        }
        out += "; ";
        if (fr->increment) {
            out += formatExpr(fr->increment.get());
        }
        out += ") ";
        out += formatEmbeddedStmt(fr->body.get(), level);
        return out;
    }
    if (const auto* fn = dynamic_cast<const FunctionStmt*>(stmt); fn != nullptr) {
        return formatFunction(fn, level);
    }
    if (const auto* imp = dynamic_cast<const ImportStmt*>(stmt); imp != nullptr) {
        return formatImport(imp);
    }
    if (const auto* exportList = dynamic_cast<const ExportListStmt*>(stmt); exportList != nullptr) {
        return formatExportList(exportList);
    }
    if (const auto* exportDefault = dynamic_cast<const ExportDefaultStmt*>(stmt); exportDefault != nullptr) {
        return "export default " + formatExpr(exportDefault->value.get()) + ';';
    }

    throw std::runtime_error("Formatter encountered an unsupported statement node.");
}

std::string formatProgram(const jdx::ast::Program& program) {
    std::string out;
    for (std::size_t i = 0; i < program.statements.size(); ++i) {
        out += formatStmt(program.statements[i].get());
        if (i + 1U < program.statements.size()) {
            out += '\n';
        }
    }
    if (!out.empty()) {
        out += '\n';
    }
    return out;
}

struct FileSnapshot {
    std::unordered_map<std::string, std::uintmax_t> sizes;
    std::unordered_map<std::string, fs::file_time_type> stamps;
};

FileSnapshot snapshotProject(const fs::path& root) {
    FileSnapshot snapshot;
    if (!fs::exists(root)) {
        return snapshot;
    }

    const fs::directory_options options = fs::directory_options::skip_permission_denied;
    for (const auto& entry : fs::recursive_directory_iterator(root, options)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto path = entry.path();
        if (path.extension() != ".jdx") {
            continue;
        }
        std::error_code ec;
        snapshot.sizes[path.string()] = entry.file_size(ec);
        snapshot.stamps[path.string()] = entry.last_write_time(ec);
    }
    return snapshot;
}

bool projectChanged(const FileSnapshot& before, const FileSnapshot& after) {
    return before.sizes != after.sizes || before.stamps != after.stamps;
}

void printHelp() {
    std::cout
        << "JDX CLI\n\n"
        << "Usage:\n"
        << "  jdx [script.jdx] [args...]\n"
        << "  jdx -e \"code\" [args...]\n"
        << "  jdx fmt [file.jdx] [--write]\n"
        << "  jdx --watch script.jdx [args...]\n\n"
        << "Options:\n"
        << "  -e, --eval   Execute inline source code\n"
        << "  -w, --watch  Watch project files and rerun on changes\n"
        << "  --write      Overwrite file when used with fmt\n"
        << "  -h, --help   Show this help\n"
        << "Notes: Try Coffee For Your Brain";
}

Options parseArgs(int argc, char** argv) {
    Options opt;
    bool modeSelected = false;
    bool evalSourceCaptured = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (evalSourceCaptured) {
            opt.runtimeArgs.push_back(arg);
            continue;
        }

        if (arg == "-h" || arg == "--help") {
            printHelp();
            std::exit(0);
        }
        if (arg == "-w" || arg == "--watch") {
            opt.watch = true;
            continue;
        }
        if (arg == "--write") {
            opt.write = true;
            continue;
        }
        if (arg == "-e" || arg == "--eval") {
            opt.mode = Mode::Eval;
            modeSelected = true;
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing source string after -e/--eval.");
            }
            opt.source = argv[++i];
            evalSourceCaptured = true;
            continue;
        }
        if (arg == "fmt" || arg == "--format") {
            opt.mode = Mode::Format;
            modeSelected = true;
            continue;
        }

        if (arg == "--") {
            for (++i; i < argc; ++i) {
                opt.runtimeArgs.push_back(argv[i]);
            }
            break;
        }

        if (opt.mode == Mode::Format) {
            if (opt.filename.empty()) {
                opt.filename = arg;
            } else {
                opt.runtimeArgs.push_back(arg);
            }
            continue;
        }

        if (!modeSelected) {
            opt.filename = arg;
            modeSelected = true;
            continue;
        }

        opt.runtimeArgs.push_back(arg);
    }

    if (opt.mode == Mode::Run && opt.filename.empty()) {
        opt.mode = Mode::Repl;
    }

    return opt;
}

struct ExecutionContext {
    std::vector<std::string> args;
    jdx::modules::ModuleManager moduleManager;
    std::shared_ptr<jdx::interpreter::Environment> globals;
    jdx::interpreter::Interpreter interpreter;

    explicit ExecutionContext(std::vector<std::string> argv)
        : args(std::move(argv)),
          moduleManager(fs::current_path().string()),
          globals(std::make_shared<jdx::interpreter::Environment>()),
          interpreter(globals, moduleManager, args) {
        globals->define("System", jdx::runtime::makeSystemObject(args), true);
    }
};

int runProgramSource(ExecutionContext& ctx, const std::string& filename, const std::string& source) {
    jdx::lexer::Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    jdx::parser::Parser parser(std::move(tokens));
    auto program = parser.parse();
    ctx.interpreter.executeProgram(program, filename, ctx.globals);
    return 0;
}

int runEvalSource(ExecutionContext& ctx, const std::string& filename, const std::string& source, const bool echoResult) {
    jdx::lexer::Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    jdx::parser::Parser parser(std::move(tokens));
    auto program = parser.parse();

    if (echoResult && program.statements.size() == 1U) {
        if (const auto* exprStmt = dynamic_cast<jdx::ast::ExprStmt*>(program.statements.front().get()); exprStmt != nullptr) {
            const auto value = ctx.interpreter.evaluate(exprStmt->expression.get(), filename, ctx.globals);
            if (!value.isNull()) {
                std::cout << value.toString() << '\n';
            }
            return 0;
        }
    }

    ctx.interpreter.executeProgram(program, filename, ctx.globals);
    return 0;
}

int runFileOnce(const std::string& filename, const std::vector<std::string>& runtimeArgs) {
    const std::string source = readFile(filename);
    ExecutionContext ctx(runtimeArgs);
    return runProgramSource(ctx, filename, source);
}

int runEvalOnce(const std::string& code, const std::vector<std::string>& runtimeArgs) {
    ExecutionContext ctx(runtimeArgs);
    return runEvalSource(ctx, "<eval>", code, true);
}

int runFormatter(const Options& opt) {
    const std::string filename = opt.filename.empty() ? std::string{"<stdin>"} : opt.filename;
    const std::string source = opt.filename.empty() ? std::string{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()} : readFile(opt.filename);

    jdx::lexer::Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    jdx::parser::Parser parser(std::move(tokens));
    auto program = parser.parse();
    const std::string formatted = formatProgram(program);

    if (opt.write) {
        if (opt.filename.empty()) {
            throw std::runtime_error("--write requires an input file.");
        }
        writeFile(opt.filename, formatted);
    } else {
        std::cout << formatted;
    }
    return 0;
}

int runInteractiveRepl() {
    ExecutionContext ctx({});
    std::string buffer;

    std::cout << "JDX REPL\nType .help for help, .exit to quit\n";

    while (true) {
        const bool continuation = !buffer.empty();
        std::cout << (continuation ? "... " : "> ") << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << '\n';
            return 0;
        }

        if (!continuation && line == ".exit") {
            return 0;
        }
        if (!continuation && line == ".help") {
            std::cout << "Commands: .help, .exit\n";
            continue;
        }

        if (!buffer.empty()) {
            buffer.push_back('\n');
        }
        buffer += line;

        int braces = 0;
        int parens = 0;
        bool inString = false;
        char stringDelimiter = '\0';
        bool escaping = false;
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            const char ch = buffer[i];
            if (inString) {
                if (escaping) {
                    escaping = false;
                    continue;
                }
                if (ch == '\\') {
                    escaping = true;
                    continue;
                }
                if (ch == stringDelimiter) {
                    inString = false;
                }
                continue;
            }
            if (ch == '"' || ch == '\'') {
                inString = true;
                stringDelimiter = ch;
                continue;
            }
            if (ch == '{') {
                ++braces;
            } else if (ch == '}') {
                --braces;
            } else if (ch == '(') {
                ++parens;
            } else if (ch == ')') {
                --parens;
            }
        }

        if (braces != 0 || parens != 0 || inString) {
            continue;
        }

        std::string source = buffer;
        if (needsTrailingSemicolon(source)) {
            source.push_back(';');
        }

        try {
            runEvalSource(ctx, "<repl>", source, true);
        } catch (const jdx::utils::DiagnosticError& e) {
            std::cerr << e.what();
        } catch (const std::exception& e) {
            std::cerr << "  [Runtime Error] " << e.what() << '\n';
        }

        buffer.clear();
    }
}

int runWatchMode(const std::string& filename, const std::vector<std::string>& runtimeArgs) {
    const fs::path root = fs::current_path();
    FileSnapshot previous = snapshotProject(root);

    for (;;) {
        try {
            (void)runFileOnce(filename, runtimeArgs);
        } catch (const jdx::utils::DiagnosticError& e) {
            std::cerr << e.what();
        } catch (const std::exception& e) {
            std::cerr << "  [Runtime Error] " << e.what() << '\n';
        }

        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const FileSnapshot current = snapshotProject(root);
            if (projectChanged(previous, current)) {
                previous = current;
                std::cout << "[watch] change detected, rerunning...\n";
                break;
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);

        switch (options.mode) {
            case Mode::Format:
                return runFormatter(options);
            case Mode::Eval:
                return runEvalOnce(options.source, options.runtimeArgs);
            case Mode::Repl:
                return runInteractiveRepl();
            case Mode::Run:
                if (options.watch) {
                    return runWatchMode(options.filename, options.runtimeArgs);
                }
                return runFileOnce(options.filename, options.runtimeArgs);
        }
    } catch (const jdx::utils::DiagnosticError& e) {
        std::cerr << e.what();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "  [Runtime Error] " << e.what() << '\n';
        return 1;
    }

    return 0;
}