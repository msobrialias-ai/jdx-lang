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
    FName,
    Return,
    If,
    Elif,
    Else,
    While,
    For,
    Break,
    Continue,
    Class,
    Try,
    Catch,
    Throw,
    This,
    Import,
    Export,
    Default,
    From,
    As,

    True,
    False,
    Null,

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
    AndAnd,
    OrOr,
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

private:
    [[nodiscard]] bool isAtEnd() const;
    [[nodiscard]] char peek() const;
    [[nodiscard]] char peekNext() const;
    char advance();
    bool match(char expected);
    void skipWhitespaceAndComments();
    void addToken(TokenType type, const std::string& lexeme = {});
    void scanString(char quote);
    void scanNumber();
    void scanIdentifier();

    std::string filename_;
    std::string source_;
    std::vector<Token> tokens_;
    std::unordered_map<std::string, TokenType> keywords_;

    std::size_t start_ {0};
    std::size_t current_ {0};
    std::size_t line_ {1};
    std::size_t column_ {1};
    std::size_t tokenLine_ {1};
    std::size_t tokenColumn_ {1};
};

} // namespace jdx::lexer
