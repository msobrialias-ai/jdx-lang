#include "lexer/Lexer.hpp"

#include "utils/ErrorHandler.hpp"

#include <cctype>
#include <utility>

namespace jdx::lexer {

Lexer::Lexer(std::string filename, std::string source)
    : filename_(std::move(filename)), source_(std::move(source)) {
    keywords_ = {
        {"let", TokenType::Let},
        {"const", TokenType::Const},
        {"fname", TokenType::FName},
        {"return", TokenType::Return},
        {"if", TokenType::If},
        {"elif", TokenType::Elif},
        {"else", TokenType::Else},
        {"while", TokenType::While},
        {"for", TokenType::For},
        {"break", TokenType::Break},
        {"continue", TokenType::Continue},
        {"class", TokenType::Class},
        {"try", TokenType::Try},
        {"catch", TokenType::Catch},
        {"throw", TokenType::Throw},
        {"this", TokenType::This},
        {"import", TokenType::Import},
        {"export", TokenType::Export},
        {"default", TokenType::Default},
        {"from", TokenType::From},
        {"as", TokenType::As},
        {"true", TokenType::True},
        {"false", TokenType::False},
        {"null", TokenType::Null}
    };

    utils::SourceRegistry::instance().registerSource(filename_, source_);
}

bool Lexer::isAtEnd() const {
    return current_ >= source_.size();
}

char Lexer::peek() const {
    return isAtEnd() ? '\0' : source_[current_];
}

char Lexer::peekNext() const {
    return (current_ + 1U >= source_.size()) ? '\0' : source_[current_ + 1U];
}

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
    if (isAtEnd() || source_[current_] != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::addToken(const TokenType type, const std::string& lexeme) {
    tokens_.push_back(Token{
        type,
        lexeme.empty() ? source_.substr(start_, current_ - start_) : lexeme,
        filename_,
        tokenLine_,
        tokenColumn_
    });
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        if (isAtEnd()) {
            return;
        }

        const char ch = peek();
        if (ch == ' ' || ch == '\r' || ch == '\t') {
            advance();
            continue;
        }
        if (ch == '\n') {
            advance();
            continue;
        }
        if (ch == '/' && peekNext() == '/') {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }
        if (ch == '/' && peekNext() == '*') {
            advance();
            advance();
            while (!isAtEnd() && !(peek() == '*' && peekNext() == '/')) {
                advance();
            }
            if (!isAtEnd()) {
                advance();
                advance();
            }
            continue;
        }
        break;
    }
}

void Lexer::scanString(const char quote) {
    std::string value;
    while (!isAtEnd()) {
        const char ch = advance();
        if (ch == quote) {
            addToken(TokenType::String, value);
            return;
        }
        if (ch == '\\' && !isAtEnd()) {
            const char esc = advance();
            switch (esc) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '\'': value.push_back('\''); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(esc); break;
            }
            continue;
        }
        if (ch == '\n') {
            utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_, "Unterminated string literal.");
        }
        value.push_back(ch);
    }

    utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_, "Unterminated string literal.");
}

void Lexer::scanNumber() {
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        advance();
    }
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext())) != 0) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            advance();
        }
    }
    addToken(TokenType::Number);
}

void Lexer::scanIdentifier() {
    while (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_') {
        advance();
    }

    const std::string text = source_.substr(start_, current_ - start_);
    if (text == "function" || text == "NamedExport") {
        utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_,
                                "Legacy keyword '" + text + "' is not supported.");
    }

    const auto found = keywords_.find(text);
    addToken(found == keywords_.end() ? TokenType::Identifier : found->second, text);
}

std::vector<Token> Lexer::tokenize() {
    while (!isAtEnd()) {
        skipWhitespaceAndComments();
        if (isAtEnd()) {
            break;
        }

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
            case '=':
                addToken(match('=') ? TokenType::EqualEqual : TokenType::Equal);
                break;
            case '<':
                addToken(match('=') ? TokenType::LessEqual : TokenType::Less);
                break;
            case '>':
                addToken(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
                break;
            case '&':
                if (!match('&')) {
                    utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_, "Unexpected '&'.");
                }
                addToken(TokenType::AndAnd);
                break;
            case '|':
                if (!match('|')) {
                    utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_, "Unexpected '|'.");
                }
                addToken(TokenType::OrOr);
                break;
            case '/':
                addToken(TokenType::Slash);
                break;
            case '"':
            case '\'':
                scanString(ch);
                break;
            default:
                if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                    scanNumber();
                } else if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
                    scanIdentifier();
                } else {
                    utils::raiseSyntaxError(filename_, tokenLine_, tokenColumn_,
                                            std::string("Unexpected character '") + ch + "'.");
                }
                break;
        }
    }

    tokens_.push_back(Token{TokenType::EndOfFile, "", filename_, line_, column_});
    return tokens_;
}

} // namespace jdx::lexer
