#include "lexer/Lexer.hpp"
#include "utils/ErrorHandler.hpp"
#include <cctype>

namespace jdx::lexer {

Lexer::Lexer(std::string filename, std::string source)
    : filename_(std::move(filename)), source_(std::move(source)) {
    keywords_ = {
        {"let", TokenType::Let},
        {"const", TokenType::Const},
        {"return", TokenType::Return},
        {"if", TokenType::If},
        {"elif", TokenType::Elif},
        {"else", TokenType::Else},
        {"while", TokenType::While},
        {"for", TokenType::For},
        {"break", TokenType::Break},
        {"continue", TokenType::Continue},
        {"function", TokenType::Fname},
        {"fname", TokenType::Fname},
        {"import", TokenType::Import},
        {"export", TokenType::Export},
        {"default", TokenType::Default},
        {"from", TokenType::From},
        {"as", TokenType::As},
        {"NamedExport", TokenType::NamedExport},
        {"true", TokenType::True},
        {"false", TokenType::False},
        {"null", TokenType::Null},
        {"System", TokenType::System}
    };
    utils::SourceRegistry::instance().registerSource(filename_, source_);
}

bool Lexer::isAtEnd() const { return current_ >= source_.size(); }
char Lexer::peek() const { return isAtEnd() ? '\0' : source_[current_]; }
char Lexer::peekNext() const { return (current_ + 1 >= source_.size()) ? '\0' : source_[current_ + 1]; }

char Lexer::advance() {
    const char ch = source_[current_++];
    if (ch == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return ch;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[current_] != expected) return false;
    advance();
    return true;
}

void Lexer::addToken(TokenType type, const std::string& lexeme) {
    tokens_.push_back(Token{type, lexeme.empty() ? source_.substr(start_, current_ - start_) : lexeme, filename_, tokenLine_, tokenColumn_});
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        if (isAtEnd()) return;
        const char ch = peek();
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            advance();
            continue;
        }
        if (ch == '/' && peekNext() == '/') {
            while (!isAtEnd() && peek() != '\n') advance();
            continue;
        }
        if (ch == '/' && peekNext() == '*') {
            advance();
            advance();
            while (!isAtEnd()) {
                if (peek() == '*' && peekNext() == '/') {
                    advance();
                    advance();
                    break;
                }
                advance();
            }
            if (isAtEnd()) {
                utils::raiseSyntaxError(filename_, line_, column_, "Unterminated block comment.");
            }
            continue;
        }
        return;
    }
}

void Lexer::stringLiteral(char quote) {
    std::string value;
    while (!isAtEnd() && peek() != quote) {
        const char ch = advance();
        if (ch == '\\') {
            if (isAtEnd()) break;
            const char esc = advance();
            switch (esc) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case 'r': value.push_back('\r'); break;
                case '"': value.push_back('"'); break;
                case '\'': value.push_back('\''); break;
                case '\\': value.push_back('\\'); break;
                default: value.push_back(esc); break;
            }
        } else {
            value.push_back(ch);
        }
    }
    if (isAtEnd()) utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_, "Unterminated string literal.");
    advance();
    addToken(TokenType::String, value);
}

void Lexer::number() {
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    addToken(TokenType::Number);
}

void Lexer::identifier() {
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    const std::string text = source_.substr(start_, current_ - start_);
    const auto it = keywords_.find(text);
    addToken(it == keywords_.end() ? TokenType::Identifier : it->second, text);
}

std::vector<Token> Lexer::tokenize() {
    while (!isAtEnd()) {
        skipWhitespaceAndComments();
        if (isAtEnd()) break;
        start_ = current_;
        tokenLine_ = line_;
        tokenColumn_ = column_;
        const char ch = advance();
        switch (ch) {
            case '(': addToken(TokenType::LeftParen); break;
            case ')': addToken(TokenType::RightParen); break;
            case '{': addToken(TokenType::LeftBrace); break;
            case '}': addToken(TokenType::RightBrace); break;
            case ',': addToken(TokenType::Comma); break;
            case '.': addToken(TokenType::Dot); break;
            case ':': addToken(TokenType::Colon); break;
            case ';': addToken(TokenType::Semicolon); break;
            case '+': addToken(TokenType::Plus); break;
            case '-': addToken(TokenType::Minus); break;
            case '*': addToken(TokenType::Star); break;
            case '%': addToken(TokenType::Percent); break;
            case '!': addToken(match('=') ? TokenType::BangEqual : TokenType::Bang); break;
            case '=': addToken(match('=') ? TokenType::EqualEqual : TokenType::Equal); break;
            case '<': addToken(match('=') ? TokenType::LessEqual : TokenType::Less); break;
            case '>': addToken(match('=') ? TokenType::GreaterEqual : TokenType::Greater); break;
            case '/': addToken(TokenType::Slash); break;
            case '"': stringLiteral('"'); break;
            case '\'': stringLiteral('\''); break;
            default:
                if (std::isdigit(static_cast<unsigned char>(ch))) {
                    number();
                } else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
                    identifier();
                } else {
                    utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_, std::string("Unexpected character '") + ch + "'.");
                }
        }
    }
    tokens_.push_back(Token{TokenType::EndOfFile, "", filename_, line_, column_});
    return tokens_;
}

} // namespace jdx::lexer
