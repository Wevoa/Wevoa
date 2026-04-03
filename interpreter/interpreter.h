#pragma once

#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ast/ast.h"
#include "interpreter/callable.h"
#include "interpreter/environment.h"
#include "interpreter/route.h"

namespace wevoaweb {

class ReturnSignal final {
  public:
    ReturnSignal(Value value, SourceSpan span);

    Value value;
    SourceSpan span;
};

class BreakSignal final {
  public:
    explicit BreakSignal(SourceSpan span);

    SourceSpan span;
};

class ContinueSignal final {
  public:
    explicit ContinueSignal(SourceSpan span);

    SourceSpan span;
};

class Interpreter final : public ExprVisitor, public StmtVisitor {
  public:
    Interpreter(std::ostream& output, std::istream& input);

    void interpret(const std::vector<std::unique_ptr<Stmt>>& statements);
    void execute(const Stmt& stmt);
    Value evaluate(const Expr& expr);
    Value evaluateInEnvironment(const Expr& expr, std::shared_ptr<Environment> environment);
    void executeBlock(const std::vector<std::unique_ptr<Stmt>>& statements, std::shared_ptr<Environment> environment);

    void registerNative(std::string name, std::optional<std::size_t> arity, NativeCallback callback);
    void registerRoute(std::string method, std::string path, std::shared_ptr<RouteHandler> route, const SourceSpan& span);
    void setImportLoader(std::function<void(const std::string&, const SourceSpan&)> loader);
    void setTemplateRenderer(
        std::function<std::string(Interpreter&, const std::string&, const Value&, const SourceSpan&)> renderer);
    void setInlineTemplateRenderer(
        std::function<std::string(Interpreter&, const std::string&, const SourceSpan&)> renderer);
    void setCurrentSourceName(std::string sourceName);
    const std::string& currentSourceName() const;
    void setCurrentRequest(Value request);
    void clearCurrentRequest();
    void setCurrentRouteParams(Value params);
    void clearCurrentRouteParams();
    void setCurrentSession(Value session);
    void clearCurrentSession();
    void setCurrentCsrfToken(std::string token);
    const std::string& currentCsrfToken() const;
    void clearRequestContext();
    void defineGlobal(std::string name, Value value, bool isConstant);
    void registerComponent(std::string name, std::string source);
    bool hasComponent(const std::string& name) const;
    std::string componentSource(const std::string& name, const SourceSpan& span) const;
    std::string renderView(const std::string& path, const Value& data, const SourceSpan& span);
    std::string renderInlineTemplate(const std::string& source, const SourceSpan& span);
    void addResponseHeader(std::string name, std::string value);
    std::vector<std::pair<std::string, std::string>> consumeResponseHeaders();
    void copySnapshotTo(Interpreter& target) const;

    std::shared_ptr<Environment> globals() const;
    std::shared_ptr<Environment> currentEnvironment() const;
    const Value& currentRequest() const;
    const Value& currentRouteParams() const;
    const Value& currentSession() const;
    std::vector<std::string> componentSources() const;
    bool hasRoute(const std::string& path, const std::string& method = "GET") const;
    Value renderRoute(const std::string& path,
                      const std::string& method = "GET",
                      const SourceSpan& span = SourceSpan {});
    std::vector<std::string> routePaths() const;
    std::ostream& output();
    std::istream& input();

    Value visitLiteralExpr(const LiteralExpr& expr) override;
    Value visitVariableExpr(const VariableExpr& expr) override;
    Value visitAssignExpr(const AssignExpr& expr) override;
    Value visitUnaryExpr(const UnaryExpr& expr) override;
    Value visitBinaryExpr(const BinaryExpr& expr) override;
    Value visitGroupingExpr(const GroupingExpr& expr) override;
    Value visitCallExpr(const CallExpr& expr) override;
    Value visitArrayExpr(const ArrayExpr& expr) override;
    Value visitObjectExpr(const ObjectExpr& expr) override;
    Value visitHtmlExpr(const HtmlExpr& expr) override;
    Value visitGetExpr(const GetExpr& expr) override;
    Value visitIndexExpr(const IndexExpr& expr) override;

    void visitExpressionStmt(const ExpressionStmt& stmt) override;
    void visitVarDeclStmt(const VarDeclStmt& stmt) override;
    void visitBlockStmt(const BlockStmt& stmt) override;
    void visitIfStmt(const IfStmt& stmt) override;
    void visitLoopStmt(const LoopStmt& stmt) override;
    void visitWhileStmt(const WhileStmt& stmt) override;
    void visitFuncDeclStmt(const FuncDeclStmt& stmt) override;
    void visitRouteDeclStmt(const RouteDeclStmt& stmt) override;
    void visitComponentDeclStmt(const ComponentDeclStmt& stmt) override;
    void visitImportStmt(const ImportStmt& stmt) override;
    void visitReturnStmt(const ReturnStmt& stmt) override;
    void visitBreakStmt(const BreakStmt& stmt) override;
    void visitContinueStmt(const ContinueStmt& stmt) override;

  private:
    std::int64_t expectInteger(const Value& value, const SourceSpan& span, std::string_view context) const;
    const Value::Array& expectArray(const Value& value, const SourceSpan& span, std::string_view context) const;
    const Value::Object& expectObject(const Value& value, const SourceSpan& span, std::string_view context) const;
    const std::shared_ptr<Callable>& expectCallable(const Value& value,
                                                    const SourceSpan& span,
                                                    std::string_view context) const;
    void prepareLoopInitializer(const Stmt& initializer);

    std::ostream& output_;
    std::istream& input_;
    std::shared_ptr<Environment> globals_;
    std::shared_ptr<Environment> environment_;
    RouteRegistry routes_;
    std::function<void(const std::string&, const SourceSpan&)> importLoader_;
    std::function<std::string(Interpreter&, const std::string&, const Value&, const SourceSpan&)> templateRenderer_;
    std::function<std::string(Interpreter&, const std::string&, const SourceSpan&)> inlineTemplateRenderer_;
    Value currentRequest_;
    Value currentRouteParams_;
    Value currentSession_;
    std::vector<std::pair<std::string, std::string>> responseHeaders_;
    std::unordered_map<std::string, std::string> components_;
    std::string currentCsrfToken_;
    std::string currentSourceName_;
};

}  // namespace wevoaweb
