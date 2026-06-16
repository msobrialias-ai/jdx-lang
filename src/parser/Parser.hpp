#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "ast/AST.hpp"
#include "lexer/Lexer.hpp"

namespace jdx::parser {

class Parser final {
public:
    explicit Parser(std::vector<lexer::Token> tokens);

    ast::Program parse();

private:
    const lexer::Token& peek() const;
    const lexer::Token& previous() const;
    bool isAtEnd() const;
    const lexer::Token& advance();
    bool check(lexer::TokenType type) const;
    bool checkNext(lexer::TokenType type) const;
    bool match(std::initializer_list<lexer::TokenType> types);
    const lexer::Token& consume(lexer::TokenType type, const std::string& message);
    const lexer::Token& consumeIdentifier(const std::string& message);

    ast::StmtPtr declaration(bool exported = false, bool defaultExport = false);
    ast::StmtPtr importDeclaration();
    ast::StmtPtr exportDeclaration();
    ast::StmtPtr exportListDeclaration();
    ast::StmtPtr varDeclaration(bool isConst, bool exported);
    ast::StmtPtr functionDeclaration(bool exported, bool defaultExport, bool isAsync = false);
    ast::StmtPtr classDeclaration(bool exported, bool defaultExport);
    ast::StmtPtr statement();
    ast::StmtPtr ifStatement();
    ast::StmtPtr whileStatement();
    ast::StmtPtr forStatement();
    ast::StmtPtr returnStatement();
    ast::StmtPtr throwStatement();
    ast::StmtPtr tryCatchStatement();
    ast::StmtPtr switchStatement();

    ast::ExprPtr expression();
    ast::ExprPtr assignment();
    ast::ExprPtr logicalOr();
    ast::ExprPtr logicalAnd();
    ast::ExprPtr equality();
    ast::ExprPtr comparison();
    ast::ExprPtr term();
    ast::ExprPtr factor();
    ast::ExprPtr unary();
    ast::ExprPtr call();
    ast::ExprPtr primary();
    std::vector<ast::ExprPtr> arguments();

    std::vector<ast::ImportBinding> parseImportBindings();
    std::vector<ast::StmtPtr> parseSwitchBody();

    std::vector<lexer::Token> tokens_;
    std::size_t current_ {0};
};

} // namespace jdx::parser
