#include "parser/parser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>

#include "utils/error.h"

namespace wevoaweb {

namespace {

SourceSpan combine(const SourceSpan& first, const SourceSpan& second) {
    return SourceSpan {first.start, second.end};
}

SourceSpan combine(const Token& first, const Token& second) {
    return SourceSpan {first.span.start, second.span.end};
}

std::string normalizedMethod(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

}  // namespace

Parser::Parser(const std::vector<Token>& tokens, std::string source)
    : tokens_(tokens), source_(std::move(source)) {}

std::vector<std::unique_ptr<Stmt>> Parser::parse() {
    std::vector<std::unique_ptr<Stmt>> statements;
    skipSeparators();

    while (!isAtEnd()) {
        statements.push_back(declaration());
        skipSeparators();
    }

    return statements;
}

std::unique_ptr<Expr> Parser::parseExpressionOnly() {
    skipSeparators();
    auto expr = expression();
    skipSeparators();
    consume(TokenType::Eof, "Unexpected tokens after expression.");
    return expr;
}

std::unique_ptr<Stmt> Parser::declaration() {
    skipSeparators();

    if (match({TokenType::Import})) {
        return importDeclaration();
    }

    if (match({TokenType::Func})) {
        return functionDeclaration();
    }

    if (match({TokenType::Route})) {
        return routeDeclaration();
    }

    if (match({TokenType::Component})) {
        return componentDeclaration();
    }

    if (match({TokenType::Let, TokenType::Const})) {
        return variableDeclaration(previous(), true);
    }

    return statement();
}

std::unique_ptr<Stmt> Parser::importDeclaration() {
    const Token keyword = previous();
    auto path = expression();
    consumeStatementTerminator("Expected newline or ';' after import statement.");
    return std::make_unique<ImportStmt>(keyword, std::move(path), SourceSpan {keyword.span.start, path->span.end});
}

std::unique_ptr<Stmt> Parser::functionDeclaration() {
    const Token keyword = previous();
    const Token& name = consume(TokenType::Identifier, "Expected function name.");
    consume(TokenType::LeftParen, "Expected '(' after function name.");

    std::vector<Token> params;
    if (!check(TokenType::RightParen)) {
        do {
            params.push_back(consume(TokenType::Identifier, "Expected parameter name."));
        } while (match({TokenType::Comma}));
    }

    consume(TokenType::RightParen, "Expected ')' after parameter list.");
    skipSeparators();
    const Token& leftBrace = consume(TokenType::LeftBrace, "Expected '{' before function body.");
    auto body = blockStatement(leftBrace);
    const SourceSpan bodySpan = body->span;
    return std::make_unique<FuncDeclStmt>(name, std::move(params), std::move(body), combine(keyword.span, bodySpan));
}

std::unique_ptr<Stmt> Parser::routeDeclaration() {
    const Token keyword = previous();
    auto path = expression();
    std::string method = "GET";
    std::vector<Token> middleware;

    skipSeparators();
    if (check(TokenType::Identifier) && peek().lexeme == "method") {
        advance();

        if (!check(TokenType::Identifier) && !check(TokenType::String)) {
            throw ParseError("Expected HTTP method after 'method'.", peek().span);
        }

        method = normalizedMethod(advance().lexeme);
        skipSeparators();
    }

    if (check(TokenType::Identifier) && peek().lexeme == "middleware") {
        advance();

        do {
            middleware.push_back(consume(TokenType::Identifier, "Expected middleware name after 'middleware'."));
        } while (match({TokenType::Comma}));

        skipSeparators();
    }

    const Token& leftBrace = consume(TokenType::LeftBrace, "Expected '{' before route body.");
    auto body = blockStatement(leftBrace);
    return std::make_unique<RouteDeclStmt>(keyword,
                                           std::move(path),
                                           std::move(method),
                                           std::move(middleware),
                                           std::move(body),
                                           SourceSpan {keyword.span.start, body->span.end});
}

std::unique_ptr<Stmt> Parser::componentDeclaration() {
    const Token keyword = previous();
    Token name;
    if (check(TokenType::Identifier) || check(TokenType::String)) {
        name = advance();
    } else {
        throw ParseError("Expected component name.", peek().span);
    }

    consume(TokenType::LeftBrace, "Expected '{' before component body.");
    skipSeparators();
    consume(TokenType::Html, "Expected html block inside component.");
    auto body = htmlBlock(previous());
    const auto* html = dynamic_cast<HtmlExpr*>(body.get());
    if (html == nullptr) {
        throw ParseError("Component body must be an html block.", body->span);
    }
    skipSeparators();
    const Token& rightBrace = consume(TokenType::RightBrace, "Expected '}' after component body.");
    skipSeparators();
    return std::make_unique<ComponentDeclStmt>(keyword,
                                               std::move(name),
                                               html->source,
                                               SourceSpan {keyword.span.start, rightBrace.span.end});
}

std::unique_ptr<Stmt> Parser::variableDeclaration(const Token& keyword, bool expectTerminator) {
    const Token& name = consume(TokenType::Identifier, "Expected variable name.");
    std::unique_ptr<Expr> initializer;

    if (match({TokenType::Equal})) {
        initializer = expression();
    } else if (keyword.type == TokenType::Const) {
        throw ParseError("const variables must be initialized.", name.span);
    }

    const SourceSpan span = initializer ? combine(keyword.span, initializer->span) : combine(keyword.span, name.span);

    if (expectTerminator) {
        consumeStatementTerminator("Expected newline or ';' after variable declaration.");
    }

    return std::make_unique<VarDeclStmt>(name, std::move(initializer), keyword.type == TokenType::Const, span);
}

std::unique_ptr<Stmt> Parser::statement() {
    skipSeparators();

    if (match({TokenType::If})) {
        return ifStatement(previous());
    }

    if (match({TokenType::Loop})) {
        return loopStatement(previous());
    }

    if (match({TokenType::While})) {
        return whileStatement(previous());
    }

    if (match({TokenType::Return})) {
        return returnStatement(previous());
    }

    if (match({TokenType::Break})) {
        return breakStatement(previous());
    }

    if (match({TokenType::Continue})) {
        return continueStatement(previous());
    }

    if (match({TokenType::LeftBrace})) {
        return blockStatement(previous());
    }

    return expressionStatement();
}

std::unique_ptr<Stmt> Parser::ifStatement(const Token& keyword) {
    consume(TokenType::LeftParen, "Expected '(' after if.");
    auto condition = expression();
    consume(TokenType::RightParen, "Expected ')' after if condition.");

    auto thenBranch = statement();
    skipSeparators();

    std::unique_ptr<Stmt> elseBranch;
    if (match({TokenType::Else})) {
        elseBranch = statement();
    }

    const SourceSpan endSpan = elseBranch ? elseBranch->span : thenBranch->span;
    return std::make_unique<IfStmt>(std::move(condition),
                                    std::move(thenBranch),
                                    std::move(elseBranch),
                                    combine(keyword.span, endSpan));
}

std::unique_ptr<Stmt> Parser::loopStatement(const Token& keyword) {
    consume(TokenType::LeftParen, "Expected '(' after loop.");

    std::unique_ptr<Stmt> initializer;
    if (!check(TokenType::Semicolon)) {
        if (match({TokenType::Let, TokenType::Const})) {
            initializer = variableDeclaration(previous(), false);
        } else {
            initializer = expressionStatement(false);
        }
    }
    consume(TokenType::Semicolon, "Expected ';' after loop initializer.");

    std::unique_ptr<Expr> condition;
    if (!check(TokenType::Semicolon)) {
        condition = expression();
    }
    consume(TokenType::Semicolon, "Expected ';' after loop condition.");

    std::unique_ptr<Expr> increment;
    if (!check(TokenType::RightParen)) {
        increment = expression();
    }
    consume(TokenType::RightParen, "Expected ')' after loop header.");

    auto body = statement();
    const SourceSpan bodySpan = body->span;
    return std::make_unique<LoopStmt>(std::move(initializer),
                                      std::move(condition),
                                      std::move(increment),
                                      std::move(body),
                                      combine(keyword.span, bodySpan));
}

std::unique_ptr<Stmt> Parser::whileStatement(const Token& keyword) {
    consume(TokenType::LeftParen, "Expected '(' after while.");
    auto condition = expression();
    consume(TokenType::RightParen, "Expected ')' after while condition.");
    auto body = statement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body), combine(keyword.span, body->span));
}

std::unique_ptr<Stmt> Parser::returnStatement(const Token& keyword) {
    std::unique_ptr<Expr> value;
    if (!isStatementBoundary()) {
        value = expression();
    }

    consumeStatementTerminator("Expected newline or ';' after return statement.");
    const SourceSpan span = value ? combine(keyword.span, value->span) : keyword.span;
    return std::make_unique<ReturnStmt>(keyword, std::move(value), span);
}

std::unique_ptr<Stmt> Parser::breakStatement(const Token& keyword) {
    consumeStatementTerminator("Expected newline or ';' after break.");
    return std::make_unique<BreakStmt>(keyword, keyword.span);
}

std::unique_ptr<Stmt> Parser::continueStatement(const Token& keyword) {
    consumeStatementTerminator("Expected newline or ';' after continue.");
    return std::make_unique<ContinueStmt>(keyword, keyword.span);
}

std::unique_ptr<Stmt> Parser::expressionStatement(bool expectTerminator) {
    auto expr = expression();
    const SourceSpan span = expr->span;

    if (expectTerminator) {
        consumeStatementTerminator("Expected newline or ';' after expression.");
    }

    return std::make_unique<ExpressionStmt>(std::move(expr), span);
}

std::unique_ptr<BlockStmt> Parser::blockStatement(const Token& leftBrace) {
    std::vector<std::unique_ptr<Stmt>> statements;
    skipSeparators();

    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        statements.push_back(declaration());
        skipSeparators();
    }

    const Token& rightBrace = consume(TokenType::RightBrace, "Expected '}' after block.");
    return std::make_unique<BlockStmt>(std::move(statements), combine(leftBrace, rightBrace));
}

std::unique_ptr<Expr> Parser::expression() {
    return assignment();
}

std::unique_ptr<Expr> Parser::assignment() {
    auto expr = logicalOr();

    if (match({TokenType::Equal})) {
        const Token equals = previous();
        auto value = assignment();

        if (const auto* variable = dynamic_cast<VariableExpr*>(expr.get())) {
            const SourceLocation start = expr->span.start;
            const SourceLocation end = value->span.end;
            return std::make_unique<AssignExpr>(variable->name,
                                                std::move(value),
                                                SourceSpan {start, end});
        }

        throw ParseError("Invalid assignment target.", equals.span);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::logicalOr() {
    auto expr = logicalAnd();

    while (match({TokenType::OrOr})) {
        const Token op = previous();
        auto right = logicalAnd();
        const SourceLocation start = expr->span.start;
        const SourceLocation end = right->span.end;
        expr = std::make_unique<BinaryExpr>(std::move(expr),
                                            op,
                                            std::move(right),
                                            SourceSpan {start, end});
    }

    return expr;
}

std::unique_ptr<Expr> Parser::logicalAnd() {
    auto expr = equality();

    while (match({TokenType::AndAnd})) {
        const Token op = previous();
        auto right = equality();
        const SourceLocation start = expr->span.start;
        const SourceLocation end = right->span.end;
        expr = std::make_unique<BinaryExpr>(std::move(expr),
                                            op,
                                            std::move(right),
                                            SourceSpan {start, end});
    }

    return expr;
}

std::unique_ptr<Expr> Parser::equality() {
    auto expr = comparison();

    while (match({TokenType::EqualEqual, TokenType::BangEqual})) {
        const Token op = previous();
        auto right = comparison();
        const SourceLocation start = expr->span.start;
        const SourceLocation end = right->span.end;
        expr = std::make_unique<BinaryExpr>(std::move(expr),
                                            op,
                                            std::move(right),
                                            SourceSpan {start, end});
    }

    return expr;
}

std::unique_ptr<Expr> Parser::comparison() {
    auto expr = term();

    while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual})) {
        const Token op = previous();
        auto right = term();
        const SourceLocation start = expr->span.start;
        const SourceLocation end = right->span.end;
        expr = std::make_unique<BinaryExpr>(std::move(expr),
                                            op,
                                            std::move(right),
                                            SourceSpan {start, end});
    }

    return expr;
}

std::unique_ptr<Expr> Parser::term() {
    auto expr = factor();

    while (match({TokenType::Plus, TokenType::Minus})) {
        const Token op = previous();
        auto right = factor();
        const SourceLocation start = expr->span.start;
        const SourceLocation end = right->span.end;
        expr = std::make_unique<BinaryExpr>(std::move(expr),
                                            op,
                                            std::move(right),
                                            SourceSpan {start, end});
    }

    return expr;
}

std::unique_ptr<Expr> Parser::factor() {
    auto expr = unary();

    while (match({TokenType::Star, TokenType::Slash})) {
        const Token op = previous();
        auto right = unary();
        const SourceLocation start = expr->span.start;
        const SourceLocation end = right->span.end;
        expr = std::make_unique<BinaryExpr>(std::move(expr),
                                            op,
                                            std::move(right),
                                            SourceSpan {start, end});
    }

    return expr;
}

std::unique_ptr<Expr> Parser::unary() {
    if (match({TokenType::Bang, TokenType::Minus})) {
        const Token op = previous();
        auto right = unary();
        return std::make_unique<UnaryExpr>(op, std::move(right), SourceSpan {op.span.start, right->span.end});
    }

    return call();
}

std::unique_ptr<Expr> Parser::call() {
    auto expr = primary();

    while (true) {
        if (match({TokenType::LeftParen})) {
            expr = finishCall(std::move(expr));
            continue;
        }

        if (match({TokenType::LeftBracket})) {
            const Token bracket = previous();
            auto index = expression();
            const Token& rightBracket = consume(TokenType::RightBracket, "Expected ']' after index.");
            expr = std::make_unique<IndexExpr>(std::move(expr),
                                               std::move(index),
                                               bracket,
                                               SourceSpan {bracket.span.start, rightBracket.span.end});
            continue;
        }

        if (match({TokenType::Dot})) {
            const Token& name = consume(TokenType::Identifier, "Expected property name after '.'.");
            const SourceLocation start = expr->span.start;
            expr = std::make_unique<GetExpr>(std::move(expr), name, SourceSpan {start, name.span.end});
            continue;
        }

        break;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::finishCall(std::unique_ptr<Expr> callee) {
    const SourceLocation start = callee->span.start;
    std::vector<std::unique_ptr<Expr>> arguments;
    if (!check(TokenType::RightParen)) {
        do {
            arguments.push_back(expression());
        } while (match({TokenType::Comma}));
    }

    const Token& paren = consume(TokenType::RightParen, "Expected ')' after arguments.");
    return std::make_unique<CallExpr>(std::move(callee), paren, std::move(arguments), SourceSpan {start, paren.span.end});
}

std::unique_ptr<Expr> Parser::arrayLiteral(const Token& leftBracket) {
    std::vector<std::unique_ptr<Expr>> elements;
    skipSeparators();

    if (!check(TokenType::RightBracket)) {
        do {
            skipSeparators();
            elements.push_back(expression());
            skipSeparators();
        } while (match({TokenType::Comma}));
    }

    const Token& rightBracket = consume(TokenType::RightBracket, "Expected ']' after array literal.");
    return std::make_unique<ArrayExpr>(std::move(elements), combine(leftBracket, rightBracket));
}

std::unique_ptr<Expr> Parser::objectLiteral(const Token& leftBrace) {
    std::vector<ObjectField> fields;
    skipSeparators();

    if (!check(TokenType::RightBrace)) {
        do {
            skipSeparators();
            Token key;
            if (check(TokenType::Identifier) || check(TokenType::String)) {
                key = advance();
            } else {
                throw ParseError("Expected object key.", peek().span);
            }

            consume(TokenType::Colon, "Expected ':' after object key.");
            auto value = expression();
            fields.push_back(ObjectField {std::move(key), std::move(value)});
            skipSeparators();
        } while (match({TokenType::Comma}));
    }

    const Token& rightBrace = consume(TokenType::RightBrace, "Expected '}' after object literal.");
    return std::make_unique<ObjectExpr>(std::move(fields), combine(leftBrace, rightBrace));
}

std::unique_ptr<Expr> Parser::htmlBlock(const Token& keyword) {
    const Token& leftBrace = consume(TokenType::LeftBrace, "Expected '{' after html.");
    const std::size_t contentStart = leftBrace.span.end.index;
    int depth = 1;

    while (!isAtEnd()) {
        const Token& token = advance();
        if (token.type == TokenType::LeftBrace) {
            ++depth;
            continue;
        }

        if (token.type == TokenType::RightBrace) {
            --depth;
            if (depth == 0) {
                const std::size_t contentEnd = token.span.start.index;
                return std::make_unique<HtmlExpr>(keyword,
                                                  source_.substr(contentStart, contentEnd - contentStart),
                                                  SourceSpan {keyword.span.start, token.span.end});
            }
        }
    }

    throw ParseError("Expected '}' after html block.", keyword.span);
}

std::unique_ptr<Expr> Parser::primary() {
    if (match({TokenType::False})) {
        return std::make_unique<LiteralExpr>(Value(false), previous().span);
    }

    if (match({TokenType::True})) {
        return std::make_unique<LiteralExpr>(Value(true), previous().span);
    }

    if (match({TokenType::Integer})) {
        return std::make_unique<LiteralExpr>(Value(static_cast<std::int64_t>(std::stoll(previous().lexeme))),
                                             previous().span);
    }

    if (match({TokenType::String})) {
        return std::make_unique<LiteralExpr>(Value(previous().lexeme), previous().span);
    }

    if (match({TokenType::Identifier})) {
        return std::make_unique<VariableExpr>(previous(), previous().span);
    }

    if (match({TokenType::LeftBracket})) {
        return arrayLiteral(previous());
    }

    if (match({TokenType::LeftBrace})) {
        return objectLiteral(previous());
    }

    if (match({TokenType::Html})) {
        return htmlBlock(previous());
    }

    if (match({TokenType::LeftParen})) {
        const Token leftParen = previous();
        auto expr = expression();
        const Token& rightParen = consume(TokenType::RightParen, "Expected ')' after expression.");
        return std::make_unique<GroupingExpr>(std::move(expr), combine(leftParen, rightParen));
    }

    throw ParseError("Expected expression.", peek().span);
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

bool Parser::check(TokenType type) const {
    if (isAtEnd()) {
        return type == TokenType::Eof;
    }

    return peek().type == type;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }

    throw ParseError(message, peek().span);
}

void Parser::consumeStatementTerminator(const std::string& message) {
    if (match({TokenType::Semicolon, TokenType::Newline})) {
        skipSeparators();
        return;
    }

    if (check(TokenType::RightBrace) || check(TokenType::Eof)) {
        return;
    }

    throw ParseError(message, peek().span);
}

void Parser::skipSeparators() {
    while (match({TokenType::Newline, TokenType::Semicolon})) {
    }
}

bool Parser::isStatementBoundary() const {
    return check(TokenType::Semicolon) || check(TokenType::Newline) || check(TokenType::RightBrace) ||
           check(TokenType::Eof);
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::Eof;
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        ++current_;
    }

    return previous();
}

}  // namespace wevoaweb
