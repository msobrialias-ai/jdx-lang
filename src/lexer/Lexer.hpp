#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace jdx::lexer {

enum class TokenType {
    EndOfFile,
    Identifier,
    Number,
    String,
    Let,
    Const,
    Return,
    If,
    Elif,
    Else,
    While,
    For,
    Break,
    Continue,
    Fname,
    Import,
    Export,
    NamedExport,
    From,
    As,
    Default,
    True,
    False,
    Null,
    System,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Equal,
    EqualEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Dot,
    Comma,
    Colon,
    Semicolon,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace
};

struct Token {
    TokenType type {TokenType::EndOfFile};
    std::string lexeme;
    std::string filename;
    std::size_t line {1};
    std::size_t column {1};
};

class Lexer {
public:
    Lexer(std::string filename, std::string source);
    std::vector<Token> tokenize();
    const std::string& source() const { return source_; }
private:
    char peek() const;
    char peekNext() const;
    char advance();
    bool match(char expected);
    bool isAtEnd() const;
    void addToken(TokenType type, const std::string& lexeme = {});
    void skipWhitespaceAndComments();
    void stringLiteral(char quote);
    void number();
    void identifier();

    std::string filename_;
    std::string source_;
    std::vector<Token> tokens_;
    std::size_t start_ {0};
    std::size_t current_ {0};
    std::size_t line_ {1};
    std::size_t column_ {1};
    std::size_t tokenLine_ {1};
    std::size_t tokenColumn_ {1};
    std::unordered_map<std::string, TokenType> keywords_;
};

} // namespace jdx::lexer
