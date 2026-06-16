#include "parser/Parser.hpp"

#include "utils/ErrorHandler.hpp"

#include <utility>

namespace jdx::parser {
using namespace jdx::ast;
using namespace jdx::lexer;

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1U];
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::EndOfFile;
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        ++current_;
    }
    return previous();
}

bool Parser::check(const TokenType type) const {
    return !isAtEnd() && peek().type == type;
}

bool Parser::checkNext(const TokenType type) const {
    return current_ + 1U < tokens_.size() && tokens_[current_ + 1U].type == type;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (const TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(const TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }
    utils::raiseSyntaxError(peek().filename, peek().line, peek().column, message);
}

const Token& Parser::consumeIdentifier(const std::string& message) {
    return consume(TokenType::Identifier, message);
}

ast::Program Parser::parse() {
    ast::Program program;
    while (!isAtEnd()) {
        program.statements.push_back(declaration());
    }
    return program;
}

ast::StmtPtr Parser::declaration(const bool exported, const bool defaultExport) {
    if (match({TokenType::Import})) {
        return importDeclaration();
    }
    if (match({TokenType::Export})) {
        return exportDeclaration();
    }
    if (match({TokenType::Async})) {
        if (match({TokenType::FName})) {
            return functionDeclaration(exported, defaultExport, true);
        }
        utils::raiseSyntaxError(peek().filename, peek().line, peek().column,
                                "Expected 'fname' after 'async'.");
    }
    if (match({TokenType::Let})) {
        return varDeclaration(false, exported);
    }
    if (match({TokenType::Const})) {
        return varDeclaration(true, exported);
    }
    if (match({TokenType::FName})) {
        return functionDeclaration(exported, defaultExport, false);
    }
    if (match({TokenType::Class})) {
        return classDeclaration(exported, defaultExport);
    }
    return statement();
}

ast::StmtPtr Parser::importDeclaration() {
    const Token& keyword = previous();
    bool sideEffectOnly = false;
    std::string defaultLocal;
    std::string namespaceLocal;
    std::vector<ast::ImportBinding> named;
    std::string source;

    if (check(TokenType::String)) {
        sideEffectOnly = true;
        source = consume(TokenType::String, "Expected a module specifier string after 'import'.").lexeme;
    } else if (match({TokenType::Star})) {
        consume(TokenType::As, "Expected 'as' after '*' in import declaration.");
        namespaceLocal = consumeIdentifier("Expected a namespace binding after 'as'.").lexeme;
        consume(TokenType::From, "Expected 'from' after namespace import.");
        source = consume(TokenType::String, "Expected a module specifier string after 'from'.").lexeme;
    } else {
        if (check(TokenType::Identifier) && (checkNext(TokenType::From) || checkNext(TokenType::Comma))) {
            defaultLocal = advance().lexeme;
        }

        if (!defaultLocal.empty() && match({TokenType::Comma})) {
            if (match({TokenType::Star})) {
                utils::raiseSyntaxError(peek().filename, peek().line, peek().column,
                                        "Cannot combine a default import with a namespace import.");
            }
            if (match({TokenType::LeftBrace})) {
                named = parseImportBindings();
            } else {
                utils::raiseSyntaxError(peek().filename, peek().line, peek().column,
                                        "Expected '{' after ',' in import declaration.");
            }
        } else if (match({TokenType::LeftBrace})) {
            named = parseImportBindings();
        } else if (defaultLocal.empty()) {
            utils::raiseSyntaxError(peek().filename, peek().line, peek().column,
                                    "Expected an import clause or a string literal.");
        }

        consume(TokenType::From, "Expected 'from' in import declaration.");
        source = consume(TokenType::String, "Expected a module specifier string after 'from'.").lexeme;
    }

    consume(TokenType::Semicolon, "Expected ';' after import declaration.");

    return std::make_unique<ImportStmt>(keyword,
                                        source,
                                        sideEffectOnly,
                                        defaultLocal,
                                        namespaceLocal,
                                        std::move(named));
}

std::vector<ast::ImportBinding> Parser::parseImportBindings() {
    std::vector<ast::ImportBinding> bindings;
    if (!check(TokenType::RightBrace)) {
        do {
            const std::string imported = consumeIdentifier("Expected an imported binding name.").lexeme;
            std::string local = imported;
            if (match({TokenType::As})) {
                local = consumeIdentifier("Expected a local name after 'as'.").lexeme;
            }
            bindings.push_back(ast::ImportBinding{imported, local});
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RightBrace, "Expected '}' after import bindings.");
    return bindings;
}

ast::StmtPtr Parser::exportDeclaration() {
    const Token& keyword = previous();

    if (match({TokenType::Default})) {
        if (match({TokenType::Async})) {
            consume(TokenType::FName, "Expected 'fname' after 'async' in default export.");
            return functionDeclaration(false, true, true);
        }
        if (match({TokenType::Class})) {
            return classDeclaration(false, true);
        }
        if (match({TokenType::FName})) {
            return functionDeclaration(false, true, false);
        }
        auto value = expression();
        consume(TokenType::Semicolon, "Expected ';' after default export.");
        return std::make_unique<ExportDefaultStmt>(keyword, std::move(value));
    }

    if (match({TokenType::Async})) {
        consume(TokenType::FName, "Expected 'fname' after 'async' in export declaration.");
        return functionDeclaration(true, false, true);
    }
    if (match({TokenType::Let})) {
        return varDeclaration(false, true);
    }
    if (match({TokenType::Const})) {
        return varDeclaration(true, true);
    }
    if (match({TokenType::FName})) {
        return functionDeclaration(true, false, false);
    }
    if (match({TokenType::Class})) {
        return classDeclaration(true, false);
    }
    if (match({TokenType::LeftBrace})) {
        return exportListDeclaration();
    }

    utils::raiseSyntaxError(peek().filename, peek().line, peek().column,
                            "Expected a declaration after 'export'.");
    return nullptr;
}

ast::StmtPtr Parser::exportListDeclaration() {
    const Token& start = previous();
    std::vector<ast::ExportBinding> bindings;

    if (!check(TokenType::RightBrace)) {
        do {
            const std::string local = consumeIdentifier("Expected an exported binding name.").lexeme;
            std::string exported = local;
            if (match({TokenType::As})) {
                exported = consumeIdentifier("Expected an exported name after 'as'.").lexeme;
            }
            bindings.push_back(ast::ExportBinding{local, exported});
        } while (match({TokenType::Comma}));
    }

    consume(TokenType::RightBrace, "Expected '}' after export list.");
    consume(TokenType::Semicolon, "Expected ';' after export list.");
    return std::make_unique<ExportListStmt>(start, std::move(bindings));
}

ast::StmtPtr Parser::varDeclaration(const bool isConst, const bool exported) {
    const Token& name = consumeIdentifier("Expected a variable name.");
    ast::ExprPtr init;
    if (match({TokenType::Equal})) {
        init = expression();
    } else if (isConst) {
        utils::raiseSyntaxError(name.filename, name.line, name.column, "Constants require an initializer.");
    }
    consume(TokenType::Semicolon, "Expected ';' after variable declaration.");
    return std::make_unique<VarStmt>(name, isConst, exported, name.lexeme, std::move(init));
}

ast::StmtPtr Parser::functionDeclaration(const bool exported, const bool defaultExport, const bool isAsync) {
    const Token& keyword = previous();
    std::string name;
    if (check(TokenType::Identifier)) {
        name = advance().lexeme;
    } else if (!defaultExport) {
        utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected a function name.");
    }

    consume(TokenType::LeftParen, "Expected '(' after function name.");
    std::vector<std::string> params;
    if (!check(TokenType::RightParen)) {
        do {
            params.push_back(consumeIdentifier("Expected a parameter name.").lexeme);
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RightParen, "Expected ')' after parameter list.");
    consume(TokenType::LeftBrace, "Expected '{' before function body.");

    auto body = std::make_shared<BlockStmt>(previous());
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        body->statements.push_back(declaration(false, false));
    }
    consume(TokenType::RightBrace, "Expected '}' after function body.");

    return std::make_unique<FnStmt>(keyword, exported, defaultExport, isAsync, std::move(name), std::move(params), std::move(body));
}

ast::StmtPtr Parser::classDeclaration(const bool exported, const bool defaultExport) {
    const Token& keyword = previous();
    std::string name;
    if (check(TokenType::Identifier)) {
        name = advance().lexeme;
    } else if (!defaultExport) {
        utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected a class name.");
    }

    consume(TokenType::LeftBrace, "Expected '{' before class body.");
    std::vector<StmtPtr> body;
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        body.push_back(declaration(false, false));
    }
    consume(TokenType::RightBrace, "Expected '}' after class body.");

    return std::make_unique<ClassStmt>(keyword, std::move(name), std::move(body), exported, defaultExport);
}

ast::StmtPtr Parser::statement() {
    if (match({TokenType::If})) {
        return ifStatement();
    }
    if (match({TokenType::While})) {
        return whileStatement();
    }
    if (match({TokenType::For})) {
        return forStatement();
    }
    if (match({TokenType::Switch})) {
        return switchStatement();
    }
    if (match({TokenType::Try})) {
        return tryCatchStatement();
    }
    if (match({TokenType::Throw})) {
        return throwStatement();
    }
    if (match({TokenType::Return})) {
        return returnStatement();
    }
    if (match({TokenType::Break})) {
        const Token token = previous();
        consume(TokenType::Semicolon, "Expected ';' after break.");
        return std::make_unique<BreakStmt>(token);
    }
    if (match({TokenType::Continue})) {
        const Token token = previous();
        consume(TokenType::Semicolon, "Expected ';' after continue.");
        return std::make_unique<ContinueStmt>(token);
    }
    if (match({TokenType::LeftBrace})) {
        auto block = std::make_unique<BlockStmt>(previous());
        while (!check(TokenType::RightBrace) && !isAtEnd()) {
            block->statements.push_back(declaration(false, false));
        }
        consume(TokenType::RightBrace, "Expected '}' after block.");
        return block;
    }

    auto expr = expression();
    const Token& semi = consume(TokenType::Semicolon, "Expected ';' after expression.");
    return std::make_unique<ExprStmt>(semi, std::move(expr));
}

ast::StmtPtr Parser::tryCatchStatement() {
    const Token& token = previous();
    auto tryBlock = statement();

    consume(TokenType::Catch, "Expected 'catch' after 'try' block.");
    consume(TokenType::LeftParen, "Expected '(' after 'catch'.");
    const std::string catchName = consumeIdentifier("Expected a catch binding name.").lexeme;
    consume(TokenType::RightParen, "Expected ')' after catch binding.");
    auto catchBlock = statement();

    return std::make_unique<TryCatchStmt>(token, std::move(tryBlock), catchName, std::move(catchBlock));
}

ast::StmtPtr Parser::switchStatement() {
    const Token& token = previous();
    consume(TokenType::LeftParen, "Expected '(' after 'switch'.");
    auto cond = expression();
    consume(TokenType::RightParen, "Expected ')' after switch condition.");
    consume(TokenType::LeftBrace, "Expected '{' before switch body.");

    auto node = std::make_unique<SwitchStmt>(token, std::move(cond));
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        if (match({TokenType::Case})) {
            const Token caseToken = previous();
            auto caseValue = expression();
            consume(TokenType::Colon, "Expected ':' after case value.");
            auto body = parseSwitchBody();
            node->cases.emplace_back(caseToken, std::move(caseValue), std::move(body));
            continue;
        }
        if (match({TokenType::Default})) {
            if (node->hasDefault) {
                utils::raiseSyntaxError(previous().filename, previous().line, previous().column,
                                        "Duplicate default clause in switch statement.");
            }
            consume(TokenType::Colon, "Expected ':' after default.");
            node->defaultStatements = parseSwitchBody();
            node->hasDefault = true;
            continue;
        }
        utils::raiseSyntaxError(peek().filename, peek().line, peek().column,
                                "Expected 'case', 'default', or '}' in switch statement.");
    }

    consume(TokenType::RightBrace, "Expected '}' after switch body.");
    return node;
}

ast::StmtPtr Parser::throwStatement() {
    const Token token = previous();
    auto value = expression();
    consume(TokenType::Semicolon, "Expected ';' after throw expression.");
    return std::make_unique<ThrowStmt>(token, std::move(value));
}

ast::StmtPtr Parser::ifStatement() {
    const Token& token = previous();
    consume(TokenType::LeftParen, "Expected '(' after 'if'.");
    auto cond = expression();
    consume(TokenType::RightParen, "Expected ')' after if condition.");
    auto thenBranch = statement();
    auto node = std::make_unique<IfStmt>(token, std::move(cond), std::move(thenBranch));

    while (match({TokenType::Elif})) {
        consume(TokenType::LeftParen, "Expected '(' after 'elif'.");
        auto branchCond = expression();
        consume(TokenType::RightParen, "Expected ')' after elif condition.");
        auto branchStmt = statement();
        node->elseIfBranches.emplace_back(std::move(branchCond), std::move(branchStmt));
    }

    if (match({TokenType::Else})) {
        node->elseBranch = statement();
    }

    return node;
}

ast::StmtPtr Parser::whileStatement() {
    const Token& token = previous();
    consume(TokenType::LeftParen, "Expected '(' after 'while'.");
    auto cond = expression();
    consume(TokenType::RightParen, "Expected ')' after while condition.");
    auto body = statement();
    return std::make_unique<WhileStmt>(token, std::move(cond), std::move(body));
}

ast::StmtPtr Parser::forStatement() {
    const Token& token = previous();
    consume(TokenType::LeftParen, "Expected '(' after 'for'.");
    ast::StmtPtr init;
    if (match({TokenType::Semicolon})) {
        init = nullptr;
    } else if (match({TokenType::Let})) {
        init = varDeclaration(false, false);
    } else if (match({TokenType::Const})) {
        init = varDeclaration(true, false);
    } else if (match({TokenType::FName})) {
        init = functionDeclaration(false, false, false);
    } else {
        auto expr = expression();
        consume(TokenType::Semicolon, "Expected ';' after loop initializer.");
        init = std::make_unique<ExprStmt>(previous(), std::move(expr));
    }

    ast::ExprPtr cond;
    if (!check(TokenType::Semicolon)) {
        cond = expression();
    }
    consume(TokenType::Semicolon, "Expected ';' after loop condition.");

    ast::ExprPtr inc;
    if (!check(TokenType::RightParen)) {
        inc = expression();
    }
    consume(TokenType::RightParen, "Expected ')' after for clauses.");

    auto body = statement();
    return std::make_unique<ForStmt>(token, std::move(init), std::move(cond), std::move(inc), std::move(body));
}

ast::StmtPtr Parser::returnStatement() {
    const Token& token = previous();
    ast::ExprPtr value;
    if (!check(TokenType::Semicolon)) {
        value = expression();
    }
    consume(TokenType::Semicolon, "Expected ';' after return value.");
    return std::make_unique<ReturnStmt>(token, std::move(value));
}

std::vector<ast::StmtPtr> Parser::parseSwitchBody() {
    std::vector<ast::StmtPtr> body;
    while (!check(TokenType::RightBrace) && !check(TokenType::Case) && !check(TokenType::Default) && !isAtEnd()) {
        body.push_back(declaration(false, false));
    }
    return body;
}

ast::ExprPtr Parser::expression() {
    return assignment();
}

ast::ExprPtr Parser::assignment() {
    auto expr = logicalOr();
    if (match({TokenType::Equal})) {
        const Token& equals = previous();
        auto value = assignment();
        if (auto* variable = dynamic_cast<VariableExpr*>(expr.get()); variable != nullptr) {
            return std::make_unique<AssignExpr>(equals, variable->name, std::move(value));
        }
        if (auto* get = dynamic_cast<GetExpr*>(expr.get()); get != nullptr) {
            return std::make_unique<SetExpr>(std::move(get->object), equals, get->name, std::move(value));
        }
        utils::raiseSyntaxError(equals.filename, equals.line, equals.column, "Invalid assignment target.");
    }
    return expr;
}

ast::ExprPtr Parser::logicalOr() {
    auto expr = logicalAnd();
    while (match({TokenType::OrOr})) {
        const Token op = previous();
        auto right = logicalAnd();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::logicalAnd() {
    auto expr = equality();
    while (match({TokenType::AndAnd})) {
        const Token op = previous();
        auto right = equality();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::equality() {
    auto expr = comparison();
    while (match({TokenType::BangEqual, TokenType::EqualEqual})) {
        const Token op = previous();
        auto right = comparison();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::comparison() {
    auto expr = term();
    while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual})) {
        const Token op = previous();
        auto right = term();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::term() {
    auto expr = factor();
    while (match({TokenType::Plus, TokenType::Minus})) {
        const Token op = previous();
        auto right = factor();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::factor() {
    auto expr = unary();
    while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
        const Token op = previous();
        auto right = unary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::unary() {
    if (match({TokenType::Await})) {
        const Token op = previous();
        auto right = unary();
        return std::make_unique<AwaitExpr>(op, std::move(right));
    }
    if (match({TokenType::Bang, TokenType::Minus})) {
        const Token op = previous();
        auto right = unary();
        return std::make_unique<UnaryExpr>(op, std::move(right));
    }
    return call();
}

std::vector<ast::ExprPtr> Parser::arguments() {
    std::vector<ast::ExprPtr> args;
    if (!check(TokenType::RightParen)) {
        do {
            args.push_back(expression());
        } while (match({TokenType::Comma}));
    }
    return args;
}

ast::ExprPtr Parser::call() {
    auto expr = primary();
    for (;;) {
        if (match({TokenType::LeftParen})) {
            const Token paren = previous();
            auto args = arguments();
            consume(TokenType::RightParen, "Expected ')' after arguments.");
            expr = std::make_unique<CallExpr>(std::move(expr), paren, std::move(args));
        } else if (match({TokenType::Dot})) {
            const Token& name = consumeIdentifier("Expected a property name after '.'.");
            expr = std::make_unique<GetExpr>(std::move(expr), name, name.lexeme);
        } else {
            break;
        }
    }
    return expr;
}

ast::ExprPtr Parser::primary() {
    if (match({TokenType::False})) {
        return std::make_unique<LiteralExpr>(previous());
    }
    if (match({TokenType::True})) {
        return std::make_unique<LiteralExpr>(previous());
    }
    if (match({TokenType::Null})) {
        return std::make_unique<LiteralExpr>(previous());
    }
    if (match({TokenType::This})) {
        return std::make_unique<ThisExpr>(previous());
    }
    if (match({TokenType::Number, TokenType::String})) {
        return std::make_unique<LiteralExpr>(previous());
    }
    if (match({TokenType::Identifier})) {
        return std::make_unique<VariableExpr>(previous(), previous().lexeme);
    }
    if (match({TokenType::LeftParen})) {
        auto expr = expression();
        consume(TokenType::RightParen, "Expected ')' after expression.");
        return std::make_unique<GroupingExpr>(previous(), std::move(expr));
    }

    utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected a valid expression.");
    return nullptr;
}

} // namespace jdx::parser
