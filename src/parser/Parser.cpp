#include "parser/Parser.hpp"
#include "utils/ErrorHandler.hpp"

namespace jdx::parser {
using namespace jdx::lexer;

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

bool Parser::isAtEnd() const { return peek().type == TokenType::EndOfFile; }
const Token& Parser::peek() const { return tokens_[current_]; }
const Token& Parser::previous() const { return tokens_[current_ - 1]; }
const Token& Parser::advance() { if (!isAtEnd()) ++current_; return previous(); }
bool Parser::check(TokenType type) const { return !isAtEnd() && peek().type == type; }
bool Parser::checkNext(TokenType type) const { return current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == type; }

bool Parser::match(std::initializer_list<TokenType> types) {
    for (const auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    utils::raiseSyntaxError(peek().filename, peek().line, peek().column, message);
    return peek();
}

bool Parser::isPropertyNameToken(TokenType type) const {
    switch (type) {
        case TokenType::Identifier:
        case TokenType::Let:
        case TokenType::Const:
        case TokenType::Return:
        case TokenType::If:
        case TokenType::Elif:
        case TokenType::Else:
        case TokenType::While:
        case TokenType::For:
        case TokenType::Break:
        case TokenType::Continue:
        case TokenType::Fname:
        case TokenType::Import:
        case TokenType::Export:
        case TokenType::NamedExport:
        case TokenType::From:
        case TokenType::As:
        case TokenType::Default:
        case TokenType::True:
        case TokenType::False:
        case TokenType::Null:
        case TokenType::System:
            return true;
        default:
            return false;
    }
}

const Token& Parser::consumePropertyName(const std::string& message) {
    if (isPropertyNameToken(peek().type)) return advance();
    utils::raiseSyntaxError(peek().filename, peek().line, peek().column, message);
    return peek();
}


ast::Program Parser::parse() {
    ast::Program program;
    while (!isAtEnd()) {
        program.statements.push_back(declaration(false));
    }
    return program;
}

ast::StmtPtr Parser::declaration(bool exported) {
    if (match({TokenType::Import})) return importDeclaration();
    if (match({TokenType::Export})) return exportDeclaration();
    if (match({TokenType::Let})) return varDeclaration(false, exported);
    if (match({TokenType::Const})) return varDeclaration(true, exported);
    if (match({TokenType::Fname})) return functionDeclaration(exported, false, false);
    if (match({TokenType::NamedExport})) return exportListDeclaration();
    return statement();
}

ast::StmtPtr Parser::importDeclaration() {
    const Token& t = previous();
    bool sideEffectOnly = false;
    std::string defaultLocal;
    std::string namespaceLocal;
    std::vector<ast::ImportBinding> named;
    std::string source;

    if (check(TokenType::String)) {
        sideEffectOnly = true;
        source = consume(TokenType::String, "Expected a string literal module specifier after 'import'.").lexeme;
    } else if (match({TokenType::Star})) {
        consume(TokenType::As, "Expected 'as' after '*' in import declaration.");
        namespaceLocal = consume(TokenType::Identifier, "Expected a namespace binding after 'as'.").lexeme;
        consume(TokenType::From, "Expected 'from' after namespace import.");
        source = consume(TokenType::String, "Expected a string literal module specifier after 'from'.").lexeme;
    } else {
        if (check(TokenType::Identifier) && (checkNext(TokenType::From) || checkNext(TokenType::Comma))) {
            defaultLocal = advance().lexeme;
        }

        if (!defaultLocal.empty() && match({TokenType::Comma})) {
            if (match({TokenType::Star})) {
                utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Cannot combine a default import with a namespace import.");
            }
            if (match({TokenType::LeftBrace})) {
                named = parseImportBindings();
            } else {
                utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected '{' after ',' in import declaration.");
            }
        } else if (match({TokenType::LeftBrace})) {
            named = parseImportBindings();
        } else if (defaultLocal.empty()) {
            utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected an import clause or a string literal.");
        }

        consume(TokenType::From, "Expected 'from' in import declaration.");
        source = consume(TokenType::String, "Expected a string literal module specifier after 'from'.").lexeme;
    }

    consume(TokenType::Semicolon, "Expected ';' after import declaration.");

    return std::make_unique<ast::ImportStmt>(t, source, sideEffectOnly, defaultLocal, namespaceLocal, std::move(named));
}

std::vector<ast::ImportBinding> Parser::parseImportBindings() {
    std::vector<ast::ImportBinding> bindings;
    if (!check(TokenType::RightBrace)) {
        do {
            const std::string imported = consume(TokenType::Identifier, "Expected an imported name.").lexeme;
            std::string local = imported;
            if (match({TokenType::As})) {
                local = consume(TokenType::Identifier, "Expected a local name after 'as'.").lexeme;
            }
            bindings.push_back(ast::ImportBinding{imported, local});
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RightBrace, "Expected '}' after import bindings.");
    return bindings;
}

ast::StmtPtr Parser::exportDeclaration() {
    const Token& t = previous();

    if (match({TokenType::Default})) {
        if (match({TokenType::Fname})) {
            return functionDeclaration(true, true, true);
        }
        auto value = expression();
        consume(TokenType::Semicolon, "Expected ';' after default export.");
        return std::make_unique<ast::ExportDefaultStmt>(t, std::move(value));
    }

    if (match({TokenType::Let})) return varDeclaration(false, true);
    if (match({TokenType::Const})) return varDeclaration(true, true);
    if (match({TokenType::Fname})) return functionDeclaration(true, false, false);
    if (match({TokenType::NamedExport})) return exportListDeclaration();
    if (match({TokenType::LeftBrace})) return exportListDeclaration();

    utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected a declaration after 'export'.");
    return nullptr;
}

ast::StmtPtr Parser::exportListDeclaration() {
    const Token& t = previous();
    std::vector<ast::ExportBinding> bindings;

    if (t.type != TokenType::LeftBrace) {
        consume(TokenType::LeftBrace, "Expected '{' after 'NamedExport'.");
    }

    if (!check(TokenType::RightBrace)) {
        do {
            const std::string local = consume(TokenType::Identifier, "Expected an exported binding name.").lexeme;
            std::string exported = local;
            if (match({TokenType::As})) {
                exported = consume(TokenType::Identifier, "Expected an exported name after 'as'.").lexeme;
            }
            bindings.push_back(ast::ExportBinding{local, exported});
        } while (match({TokenType::Comma}));
    }

    consume(TokenType::RightBrace, "Expected '}' after export list.");
    consume(TokenType::Semicolon, "Expected ';' after export list.");
    return std::make_unique<ast::ExportListStmt>(t, std::move(bindings));
}

ast::StmtPtr Parser::varDeclaration(bool isConst, bool exported) {
    const Token& name = consume(TokenType::Identifier, "Expected a variable name.");
    ast::ExprPtr init;
    if (match({TokenType::Equal})) {
        init = expression();
    } else if (isConst) {
        utils::raiseSyntaxError(name.filename, name.line, name.column, "Constants require an initializer.");
    }
    consume(TokenType::Semicolon, "Expected ';' after variable declaration.");
    return std::make_unique<ast::VarStmt>(name, isConst, exported, name.lexeme, std::move(init));
}

ast::StmtPtr Parser::functionDeclaration(bool exported, bool defaultExport, bool allowAnonymous) {
    const Token& keyword = previous();
    std::string name;
    if (check(TokenType::Identifier)) {
        name = advance().lexeme;
    } else if (!allowAnonymous) {
        utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected a function name.");
    }

    consume(TokenType::LeftParen, "Expected '(' after function name.");
    std::vector<std::string> params;
    if (!check(TokenType::RightParen)) {
        do {
            params.push_back(consume(TokenType::Identifier, "Expected a parameter name.").lexeme);
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RightParen, "Expected ')' after parameter list.");
    consume(TokenType::LeftBrace, "Expected '{' before function body.");

    auto body = std::make_shared<ast::BlockStmt>(previous());
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        body->statements.push_back(declaration(false));
    }
    consume(TokenType::RightBrace, "Expected '}' after function body.");

    return std::make_unique<ast::FunctionStmt>(keyword, std::move(name), std::move(params), std::move(body), exported, defaultExport);
}

ast::StmtPtr Parser::statement() {
    if (match({TokenType::If})) return ifStatement();
    if (match({TokenType::While})) return whileStatement();
    if (match({TokenType::For})) return forStatement();
    if (match({TokenType::Return})) return returnStatement();
    if (match({TokenType::Break})) {
        const Token t = previous();
        consume(TokenType::Semicolon, "Expected ';' after break.");
        return std::make_unique<ast::BreakStmt>(t);
    }
    if (match({TokenType::Continue})) {
        const Token t = previous();
        consume(TokenType::Semicolon, "Expected ';' after continue.");
        return std::make_unique<ast::ContinueStmt>(t);
    }
    if (match({TokenType::LeftBrace})) {
        auto block = std::make_unique<ast::BlockStmt>(previous());
        while (!check(TokenType::RightBrace) && !isAtEnd()) {
            block->statements.push_back(declaration(false));
        }
        consume(TokenType::RightBrace, "Expected '}' after block.");
        return block;
    }

    auto expr = expression();
    const Token& semi = consume(TokenType::Semicolon, "Expected ';' after expression.");
    return std::make_unique<ast::ExprStmt>(semi, std::move(expr));
}

ast::StmtPtr Parser::ifStatement() {
    const Token& t = previous();
    consume(TokenType::LeftParen, "Expected '(' after 'if'.");
    auto cond = expression();
    consume(TokenType::RightParen, "Expected ')' after if condition.");
    auto thenBranch = statement();
    auto node = std::make_unique<ast::IfStmt>(t, std::move(cond), std::move(thenBranch));
    while (match({TokenType::Elif})) {
        consume(TokenType::LeftParen, "Expected '(' after 'elif'.");
        auto c = expression();
        consume(TokenType::RightParen, "Expected ')' after elif condition.");
        auto branch = statement();
        node->elseIfBranches.emplace_back(std::move(c), std::move(branch));
    }
    if (match({TokenType::Else})) {
        node->elseBranch = statement();
    }
    return node;
}

ast::StmtPtr Parser::whileStatement() {
    const Token& t = previous();
    consume(TokenType::LeftParen, "Expected '(' after 'while'.");
    auto cond = expression();
    consume(TokenType::RightParen, "Expected ')' after while condition.");
    auto body = statement();
    return std::make_unique<ast::WhileStmt>(t, std::move(cond), std::move(body));
}

ast::StmtPtr Parser::forStatement() {
    const Token& t = previous();
    consume(TokenType::LeftParen, "Expected '(' after 'for'.");
    ast::StmtPtr init;
    if (match({TokenType::Semicolon})) {
        init = nullptr;
    } else if (match({TokenType::Let})) {
        init = varDeclaration(false, false);
    } else if (match({TokenType::Const})) {
        init = varDeclaration(true, false);
    } else {
        auto e = expression();
        consume(TokenType::Semicolon, "Expected ';' after loop initializer.");
        init = std::make_unique<ast::ExprStmt>(previous(), std::move(e));
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
    return std::make_unique<ast::ForStmt>(t, std::move(init), std::move(cond), std::move(inc), std::move(body));
}

ast::StmtPtr Parser::returnStatement() {
    const Token& t = previous();
    ast::ExprPtr value;
    if (!check(TokenType::Semicolon)) {
        value = expression();
    }
    consume(TokenType::Semicolon, "Expected ';' after return statement.");
    return std::make_unique<ast::ReturnStmt>(t, std::move(value));
}

ast::ExprPtr Parser::expression() { return assignment(); }

ast::ExprPtr Parser::assignment() {
    auto expr = equality();
    if (match({TokenType::Equal})) {
        const Token& equals = previous();
        auto value = assignment();
        if (auto* var = dynamic_cast<ast::VariableExpr*>(expr.get())) {
            return std::make_unique<ast::AssignExpr>(equals, var->name, std::move(value));
        }
        if (auto* get = dynamic_cast<ast::GetExpr*>(expr.get())) {
            return std::make_unique<ast::SetExpr>(std::move(get->object), equals, get->name, std::move(value));
        }
        utils::raiseSyntaxError(equals.filename, equals.line, equals.column, "Invalid assignment target.");
    }
    return expr;
}

ast::ExprPtr Parser::equality() {
    auto expr = comparison();
    while (match({TokenType::BangEqual, TokenType::EqualEqual})) {
        auto op = previous();
        auto right = comparison();
        expr = std::make_unique<ast::BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::comparison() {
    auto expr = term();
    while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual})) {
        auto op = previous();
        auto right = term();
        expr = std::make_unique<ast::BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::term() {
    auto expr = factor();
    while (match({TokenType::Plus, TokenType::Minus})) {
        auto op = previous();
        auto right = factor();
        expr = std::make_unique<ast::BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::factor() {
    auto expr = unary();
    while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
        auto op = previous();
        auto right = unary();
        expr = std::make_unique<ast::BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

ast::ExprPtr Parser::unary() {
    if (match({TokenType::Bang, TokenType::Minus})) {
        auto op = previous();
        auto right = unary();
        return std::make_unique<ast::UnaryExpr>(op, std::move(right));
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
            auto paren = previous();
            auto args = arguments();
            consume(TokenType::RightParen, "Expected ')' after arguments.");
            expr = std::make_unique<ast::CallExpr>(std::move(expr), paren, std::move(args));
        } else if (match({TokenType::Dot})) {
            const Token& name = consumePropertyName("Expected a property name after '.'");
            expr = std::make_unique<ast::GetExpr>(std::move(expr), name, name.lexeme);
        } else {
            break;
        }
    }
    return expr;
}

ast::ExprPtr Parser::primary() {
    if (match({TokenType::False})) return std::make_unique<ast::LiteralExpr>(previous());
    if (match({TokenType::True})) return std::make_unique<ast::LiteralExpr>(previous());
    if (match({TokenType::Null})) return std::make_unique<ast::LiteralExpr>(previous());
    if (match({TokenType::Number, TokenType::String})) return std::make_unique<ast::LiteralExpr>(previous());
    if (match({TokenType::Identifier})) return std::make_unique<ast::VariableExpr>(previous(), previous().lexeme);
    if (match({TokenType::Import})) {
        const Token& t = previous();
        consume(TokenType::LeftParen, "Expected '(' after 'import'.");
        auto path = expression();
        consume(TokenType::RightParen, "Expected ')' after import path.");
        return std::make_unique<ast::ImportExpr>(t, std::move(path));
    }
    if (match({TokenType::System})) return std::make_unique<ast::VariableExpr>(previous(), previous().lexeme);
    if (match({TokenType::LeftParen})) {
        auto expr = expression();
        consume(TokenType::RightParen, "Expected ')' after expression.");
        return std::make_unique<ast::GroupingExpr>(previous(), std::move(expr));
    }
    utils::raiseSyntaxError(peek().filename, peek().line, peek().column, "Expected a valid expression.");
    return nullptr;
}

} // namespace jdx::parser
