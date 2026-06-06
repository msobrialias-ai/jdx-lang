#pragma once
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include "ast/AST.hpp"

namespace jdx::parser {

class Parser {
public:
    Parser(std::vector<lexer::Token> tokens);
    ast::Program parse();

private:
    using Token = lexer::Token;
    using TokenType = lexer::TokenType;

    bool isAtEnd() const;
    const Token& peek() const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool checkNext(TokenType type) const;
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);
    bool isPropertyNameToken(TokenType type) const;
    const Token& consumePropertyName(const std::string& message);

    ast::StmtPtr declaration(bool exported = false);
    ast::StmtPtr importDeclaration();
    ast::StmtPtr exportDeclaration();
    ast::StmtPtr exportListDeclaration();
    ast::StmtPtr varDeclaration(bool isConst, bool exported);
    ast::StmtPtr functionDeclaration(bool exported, bool defaultExport, bool allowAnonymous);
    ast::StmtPtr statement();
    ast::StmtPtr ifStatement();
    ast::StmtPtr whileStatement();
    ast::StmtPtr forStatement();
    ast::StmtPtr returnStatement();

    ast::ExprPtr expression();
    ast::ExprPtr assignment();
    ast::ExprPtr equality();
    ast::ExprPtr comparison();
    ast::ExprPtr term();
    ast::ExprPtr factor();
    ast::ExprPtr unary();
    ast::ExprPtr call();
    ast::ExprPtr primary();
    std::vector<ast::ExprPtr> arguments();

    std::vector<ast::ImportBinding> parseImportBindings();
    std::vector<ast::ExportBinding> parseExportBindings();

    std::vector<Token> tokens_;
    std::size_t current_ {0};
};

} // namespace jdx::parser
