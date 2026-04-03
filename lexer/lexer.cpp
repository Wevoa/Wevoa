#include "lexer/lexer.h"

#include <cctype>
#include <unordered_map>
#include <utility>

#include "utils/error.h"

namespace wevoaweb {

namespace {

SourceSpan makeSpan(const SourceLocation& start, const SourceLocation& end) {
    return SourceSpan {start, end};
}

}  // namespace

Lexer::Lexer(std::string source) : source_(normalizeNewlines(std::move(source))) {}

std::vector<Token> Lexer::scanTokens() {
    tokens_.clear();
    start_ = 0;
    current_ = 0;
    tokenStart_ = {};
    currentLocation_ = {};

    while (!isAtEnd()) {
        start_ = current_;
        tokenStart_ = currentLocation_;
        scanToken();
    }

    tokens_.push_back(Token {TokenType::Eof, "", makeSpan(currentLocation_, currentLocation_)});
    return tokens_;
}

std::string Lexer::normalizeNewlines(std::string source) {
    std::string normalized;
    normalized.reserve(source.size());

    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\r') {
            if (i + 1 < source.size() && source[i + 1] == '\n') {
                ++i;
            }
            normalized.push_back('\n');
            continue;
        }

        normalized.push_back(source[i]);
    }

    return normalized;
}

bool Lexer::isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool Lexer::isIdentifierPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool Lexer::isAtEnd() const {
    return current_ >= source_.size();
}

char Lexer::advance() {
    const char c = source_[current_++];
    currentLocation_.index = current_;
    if (c == '\n') {
        ++currentLocation_.line;
        currentLocation_.column = 1;
    } else {
        ++currentLocation_.column;
    }
    return c;
}

char Lexer::peek() const {
    if (isAtEnd()) {
        return '\0';
    }
    return source_[current_];
}

char Lexer::peekNext() const {
    if (current_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[current_ + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[current_] != expected) {
        return false;
    }

    advance();
    return true;
}

void Lexer::scanToken() {
    const char c = advance();

    switch (c) {
        case '(':
            addToken(TokenType::LeftParen);
            break;
        case ')':
            addToken(TokenType::RightParen);
            break;
        case '{':
            addToken(TokenType::LeftBrace);
            break;
        case '}':
            addToken(TokenType::RightBrace);
            break;
        case '[':
            addToken(TokenType::LeftBracket);
            break;
        case ']':
            addToken(TokenType::RightBracket);
            break;
        case ',':
            addToken(TokenType::Comma);
            break;
        case '.':
            addToken(TokenType::Dot);
            break;
        case '-':
            addToken(TokenType::Minus);
            break;
        case '+':
            addToken(TokenType::Plus);
            break;
        case '*':
            addToken(TokenType::Star);
            break;
        case ':':
            addToken(TokenType::Colon);
            break;
        case '&':
            if (match('&')) {
                addToken(TokenType::AndAnd);
                break;
            }
            throw LexError("Unexpected character '&'.", spanFromCurrentToken());
        case '|':
            if (match('|')) {
                addToken(TokenType::OrOr);
                break;
            }
            throw LexError("Unexpected character '|'.", spanFromCurrentToken());
        case ';':
            addToken(TokenType::Semicolon);
            break;
        case '!':
            addToken(match('=') ? TokenType::BangEqual : TokenType::Bang);
            break;
        case '=':
            addToken(match('=') ? TokenType::EqualEqual : TokenType::Equal);
            break;
        case '>':
            addToken(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
            break;
        case '<':
            addToken(match('=') ? TokenType::LessEqual : TokenType::Less);
            break;
        case '/':
            if (match('/')) {
                while (!isAtEnd() && peek() != '\n') {
                    advance();
                }
            } else {
                addToken(TokenType::Slash);
            }
            break;
        case '"':
            string();
            break;
        case '\n':
            emitNewline();
            break;
        case ' ':
        case '\t':
            break;
        default:
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                number();
            } else if (isIdentifierStart(c)) {
                identifier();
            } else {
                throw LexError("Unexpected character '" + std::string(1, c) + "'.", spanFromCurrentToken());
            }
            break;
    }
}

void Lexer::addToken(TokenType type) {
    addToken(type, source_.substr(start_, current_ - start_));
}

void Lexer::addToken(TokenType type, std::string lexeme) {
    tokens_.push_back(Token {type, std::move(lexeme), spanFromCurrentToken()});
}

void Lexer::emitNewline() {
    tokens_.push_back(Token {TokenType::Newline, "\\n", spanFromCurrentToken()});
}

void Lexer::string() {
    std::string value;

    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') {
            throw LexError("Unterminated string literal.", spanFromCurrentToken());
        }

        if (peek() == '\\') {
            advance();
            if (isAtEnd()) {
                throw LexError("Unterminated string escape sequence.", spanFromCurrentToken());
            }

            const char escaped = advance();
            switch (escaped) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                default:
                    throw LexError("Unsupported escape sequence '\\" + std::string(1, escaped) + "'.",
                                   spanFromCurrentToken());
            }
        } else {
            value.push_back(advance());
        }
    }

    if (isAtEnd()) {
        throw LexError("Unterminated string literal.", spanFromCurrentToken());
    }

    advance();
    addToken(TokenType::String, std::move(value));
}

void Lexer::number() {
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        advance();
    }

    addToken(TokenType::Integer);
}

void Lexer::identifier() {
    while (isIdentifierPart(peek())) {
        advance();
    }

    static const std::unordered_map<std::string, TokenType> keywords {
        {"let", TokenType::Let},
        {"const", TokenType::Const},
        {"func", TokenType::Func},
        {"route", TokenType::Route},
        {"component", TokenType::Component},
        {"if", TokenType::If},
        {"else", TokenType::Else},
        {"while", TokenType::While},
        {"loop", TokenType::Loop},
        {"break", TokenType::Break},
        {"continue", TokenType::Continue},
        {"import", TokenType::Import},
        {"html", TokenType::Html},
        {"return", TokenType::Return},
        {"true", TokenType::True},
        {"false", TokenType::False},
    };

    const std::string lexeme = source_.substr(start_, current_ - start_);
    const auto found = keywords.find(lexeme);
    addToken(found == keywords.end() ? TokenType::Identifier : found->second, lexeme);
}

SourceSpan Lexer::spanFromCurrentToken() const {
    return makeSpan(tokenStart_, currentLocation_);
}

}  // namespace wevoaweb
