#pragma once

#include <string>
#include <vector>

#include "runtime/token.h"

namespace wevoaweb {

class Lexer {
  public:
    explicit Lexer(std::string source);

    std::vector<Token> scanTokens();

  private:
    static std::string normalizeNewlines(const std::string& source);
    static bool isIdentifierStart(char c);
    static bool isIdentifierPart(char c);

    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);

    void scanToken();
    void addToken(TokenType type);
    void addToken(TokenType type, std::string lexeme);
    void emitNewline();
    void string();
    void number();
    void identifier();

    SourceSpan spanFromCurrentToken() const;

    std::string source_;
    std::vector<Token> tokens_;
    std::size_t start_ = 0;
    std::size_t current_ = 0;
    SourceLocation tokenStart_ {};
    SourceLocation currentLocation_ {};
};

}  // namespace wevoaweb
