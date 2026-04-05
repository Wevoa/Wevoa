#include "ast/ast.h"

#include <utility>

namespace wevoaweb {

Expr::Expr(const SourceSpan& span) : span(span) {}

Stmt::Stmt(const SourceSpan& span) : span(span) {}

LiteralExpr::LiteralExpr(Value value, const SourceSpan& span) : Expr(span), value(std::move(value)) {}

Value LiteralExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitLiteralExpr(*this);
}

VariableExpr::VariableExpr(Token name, const SourceSpan& span) : Expr(span), name(std::move(name)) {}

Value VariableExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitVariableExpr(*this);
}

AssignExpr::AssignExpr(Token name, std::unique_ptr<Expr> value, const SourceSpan& span)
    : Expr(span), name(std::move(name)), value(std::move(value)) {}

Value AssignExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitAssignExpr(*this);
}

UnaryExpr::UnaryExpr(Token op, std::unique_ptr<Expr> right, const SourceSpan& span)
    : Expr(span), op(std::move(op)), right(std::move(right)) {}

Value UnaryExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitUnaryExpr(*this);
}

BinaryExpr::BinaryExpr(std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right, const SourceSpan& span)
    : Expr(span), left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

Value BinaryExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitBinaryExpr(*this);
}

GroupingExpr::GroupingExpr(std::unique_ptr<Expr> expression, const SourceSpan& span)
    : Expr(span), expression(std::move(expression)) {}

Value GroupingExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitGroupingExpr(*this);
}

CallExpr::CallExpr(std::unique_ptr<Expr> callee,
                   Token paren,
                   std::vector<std::unique_ptr<Expr>> arguments,
                   const SourceSpan& span)
    : Expr(span), callee(std::move(callee)), paren(std::move(paren)), arguments(std::move(arguments)) {}

Value CallExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitCallExpr(*this);
}

ArrayExpr::ArrayExpr(std::vector<std::unique_ptr<Expr>> elements, const SourceSpan& span)
    : Expr(span), elements(std::move(elements)) {}

Value ArrayExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitArrayExpr(*this);
}

ObjectExpr::ObjectExpr(std::vector<ObjectField> fields, const SourceSpan& span) : Expr(span), fields(std::move(fields)) {}

Value ObjectExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitObjectExpr(*this);
}

HtmlExpr::HtmlExpr(Token keyword, std::string source, const SourceSpan& span)
    : Expr(span), keyword(std::move(keyword)), source(std::move(source)) {}

Value HtmlExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitHtmlExpr(*this);
}

GetExpr::GetExpr(std::unique_ptr<Expr> object, Token name, const SourceSpan& span)
    : Expr(span), object(std::move(object)), name(std::move(name)) {}

Value GetExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitGetExpr(*this);
}

IndexExpr::IndexExpr(std::unique_ptr<Expr> object, std::unique_ptr<Expr> index, Token bracket, const SourceSpan& span)
    : Expr(span), object(std::move(object)), index(std::move(index)), bracket(std::move(bracket)) {}

Value IndexExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitIndexExpr(*this);
}

ExpressionStmt::ExpressionStmt(std::unique_ptr<Expr> expression, const SourceSpan& span)
    : Stmt(span), expression(std::move(expression)) {}

void ExpressionStmt::accept(StmtVisitor& visitor) const {
    visitor.visitExpressionStmt(*this);
}

VarDeclStmt::VarDeclStmt(Token name, std::unique_ptr<Expr> initializer, bool isConstant, const SourceSpan& span)
    : Stmt(span), name(std::move(name)), initializer(std::move(initializer)), isConstant(isConstant) {}

void VarDeclStmt::accept(StmtVisitor& visitor) const {
    visitor.visitVarDeclStmt(*this);
}

BlockStmt::BlockStmt(std::vector<std::unique_ptr<Stmt>> statements, const SourceSpan& span)
    : Stmt(span), statements(std::move(statements)) {}

void BlockStmt::accept(StmtVisitor& visitor) const {
    visitor.visitBlockStmt(*this);
}

IfStmt::IfStmt(std::unique_ptr<Expr> condition,
               std::unique_ptr<Stmt> thenBranch,
               std::unique_ptr<Stmt> elseBranch,
               const SourceSpan& span)
    : Stmt(span),
      condition(std::move(condition)),
      thenBranch(std::move(thenBranch)),
      elseBranch(std::move(elseBranch)) {}

void IfStmt::accept(StmtVisitor& visitor) const {
    visitor.visitIfStmt(*this);
}

LoopStmt::LoopStmt(std::unique_ptr<Stmt> initializer,
                   std::unique_ptr<Expr> condition,
                   std::unique_ptr<Expr> increment,
                   std::unique_ptr<Stmt> body,
                   const SourceSpan& span)
    : Stmt(span),
      initializer(std::move(initializer)),
      condition(std::move(condition)),
      increment(std::move(increment)),
      body(std::move(body)) {}

void LoopStmt::accept(StmtVisitor& visitor) const {
    visitor.visitLoopStmt(*this);
}

WhileStmt::WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body, const SourceSpan& span)
    : Stmt(span), condition(std::move(condition)), body(std::move(body)) {}

void WhileStmt::accept(StmtVisitor& visitor) const {
    visitor.visitWhileStmt(*this);
}

FuncDeclStmt::FuncDeclStmt(Token name,
                           std::vector<Token> params,
                           std::unique_ptr<BlockStmt> body,
                           const SourceSpan& span)
    : Stmt(span), name(std::move(name)), params(std::move(params)), body(std::move(body)) {}

void FuncDeclStmt::accept(StmtVisitor& visitor) const {
    visitor.visitFuncDeclStmt(*this);
}

ReturnStmt::ReturnStmt(Token keyword, std::unique_ptr<Expr> value, const SourceSpan& span)
    : Stmt(span), keyword(std::move(keyword)), value(std::move(value)) {}

void ReturnStmt::accept(StmtVisitor& visitor) const {
    visitor.visitReturnStmt(*this);
}

RouteDeclStmt::RouteDeclStmt(Token keyword,
                             std::unique_ptr<Expr> path,
                             std::string method,
                             std::vector<Token> middleware,
                             std::unique_ptr<BlockStmt> body,
                             const SourceSpan& span)
    : Stmt(span),
      keyword(std::move(keyword)),
      path(std::move(path)),
      method(std::move(method)),
      middleware(std::move(middleware)),
      body(std::move(body)) {}

void RouteDeclStmt::accept(StmtVisitor& visitor) const {
    visitor.visitRouteDeclStmt(*this);
}

ComponentDeclStmt::ComponentDeclStmt(Token keyword, Token name, std::string source, const SourceSpan& span)
    : Stmt(span), keyword(std::move(keyword)), name(std::move(name)), source(std::move(source)) {}

void ComponentDeclStmt::accept(StmtVisitor& visitor) const {
    visitor.visitComponentDeclStmt(*this);
}

ImportStmt::ImportStmt(Token keyword, std::unique_ptr<Expr> path, const SourceSpan& span)
    : Stmt(span), keyword(std::move(keyword)), path(std::move(path)) {}

void ImportStmt::accept(StmtVisitor& visitor) const {
    visitor.visitImportStmt(*this);
}

BreakStmt::BreakStmt(Token keyword, const SourceSpan& span) : Stmt(span), keyword(std::move(keyword)) {}

void BreakStmt::accept(StmtVisitor& visitor) const {
    visitor.visitBreakStmt(*this);
}

ContinueStmt::ContinueStmt(Token keyword, const SourceSpan& span) : Stmt(span), keyword(std::move(keyword)) {}

void ContinueStmt::accept(StmtVisitor& visitor) const {
    visitor.visitContinueStmt(*this);
}

}  // namespace wevoaweb
