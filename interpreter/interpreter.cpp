#include "interpreter/interpreter.h"

#include <unordered_map>
#include <utility>

#include "runtime/builtins.h"
#include "utils/error.h"

namespace wevoaweb {

ReturnSignal::ReturnSignal(Value value, SourceSpan span) : value(std::move(value)), span(span) {}

BreakSignal::BreakSignal(SourceSpan span) : span(span) {}

ContinueSignal::ContinueSignal(SourceSpan span) : span(span) {}

namespace {

struct SnapshotCloneState {
    std::unordered_map<const Environment*, std::shared_ptr<Environment>> environments;
    std::unordered_map<const Value::Array*, std::shared_ptr<Value::Array>> arrays;
    std::unordered_map<const Value::Object*, std::shared_ptr<Value::Object>> objects;
    std::unordered_map<const Callable*, std::shared_ptr<Callable>> callables;
};

Value cloneValue(const Value& value, SnapshotCloneState& state);

std::shared_ptr<Environment> cloneEnvironment(const std::shared_ptr<Environment>& environment,
                                             SnapshotCloneState& state) {
    if (!environment) {
        return nullptr;
    }

    if (const auto found = state.environments.find(environment.get()); found != state.environments.end()) {
        return found->second;
    }

    auto cloned = std::make_shared<Environment>(cloneEnvironment(environment->enclosing(), state));
    state.environments.insert_or_assign(environment.get(), cloned);

    for (const auto& [name, binding] : environment->bindings()) {
        cloned->define(name, cloneValue(binding.value, state), binding.isConstant);
    }

    return cloned;
}

Value cloneValue(const Value& value, SnapshotCloneState& state) {
    if (value.isNil()) {
        return Value {};
    }
    if (value.isInteger()) {
        return Value(value.asInteger());
    }
    if (value.isString()) {
        return Value(value.asString());
    }
    if (value.isBoolean()) {
        return Value(value.asBoolean());
    }
    if (value.isCallable()) {
        const auto& callable = value.asCallable();
        if (const auto found = state.callables.find(callable.get()); found != state.callables.end()) {
            return Value(found->second);
        }

        auto cloned = callable->clone([&state](const std::shared_ptr<Environment>& env) {
            return cloneEnvironment(env, state);
        });
        state.callables.insert_or_assign(callable.get(), cloned);
        return Value(std::move(cloned));
    }
    if (value.isArray()) {
        const auto* key = &value.asArray();
        if (const auto found = state.arrays.find(key); found != state.arrays.end()) {
            return Value(found->second);
        }

        auto cloned = std::make_shared<Value::Array>();
        state.arrays.insert_or_assign(key, cloned);
        cloned->reserve(value.asArray().size());
        for (const auto& element : value.asArray()) {
            cloned->push_back(cloneValue(element, state));
        }
        return Value(std::move(cloned));
    }
    if (value.isObject()) {
        const auto* key = &value.asObject();
        if (const auto found = state.objects.find(key); found != state.objects.end()) {
            return Value(found->second);
        }

        auto cloned = std::make_shared<Value::Object>();
        state.objects.insert_or_assign(key, cloned);
        for (const auto& [name, child] : value.asObject()) {
            cloned->insert_or_assign(name, cloneValue(child, state));
        }
        return Value(std::move(cloned));
    }

    return Value {};
}

}  // namespace

Interpreter::Interpreter(std::ostream& output, std::istream& input)
    : output_(output),
      input_(input),
      globals_(std::make_shared<Environment>()),
      environment_(globals_) {
    registerBuiltins(*this);
}

void Interpreter::interpret(const std::vector<std::unique_ptr<Stmt>>& statements) {
    for (const auto& statement : statements) {
        try {
            execute(*statement);
        } catch (const ReturnSignal& signal) {
            throw RuntimeError("return may only be used inside a function or route.", signal.span);
        } catch (const BreakSignal& signal) {
            throw RuntimeError("break may only be used inside a loop.", signal.span);
        } catch (const ContinueSignal& signal) {
            throw RuntimeError("continue may only be used inside a loop.", signal.span);
        }
    }
}

void Interpreter::execute(const Stmt& stmt) {
    stmt.accept(*this);
}

Value Interpreter::evaluate(const Expr& expr) {
    return expr.accept(*this);
}

Value Interpreter::evaluateInEnvironment(const Expr& expr, std::shared_ptr<Environment> environment) {
    const auto previous = environment_;
    environment_ = std::move(environment);

    try {
        Value value = evaluate(expr);
        environment_ = previous;
        return value;
    } catch (...) {
        environment_ = previous;
        throw;
    }
}

void Interpreter::executeBlock(const std::vector<std::unique_ptr<Stmt>>& statements,
                               std::shared_ptr<Environment> environment) {
    const auto previous = environment_;
    environment_ = std::move(environment);

    try {
        for (const auto& statement : statements) {
            execute(*statement);
        }
    } catch (...) {
        environment_ = previous;
        throw;
    }

    environment_ = previous;
}

void Interpreter::registerNative(std::string name, std::optional<std::size_t> arity, NativeCallback callback) {
    const std::string bindingName = name;
    auto nativeFunction = std::make_shared<NativeFunction>(std::move(name), arity, std::move(callback));
    globals_->define(bindingName, Value(std::move(nativeFunction)), true);
}

void Interpreter::registerRoute(std::string method,
                                std::string path,
                                std::shared_ptr<RouteHandler> route,
                                const SourceSpan& span) {
    if (method.empty()) {
        throw RuntimeError("Route method must not be empty.", span);
    }

    if (path.empty() || path.front() != '/') {
        throw RuntimeError("Route paths must start with '/'.", span);
    }

    routes_.addRoute(std::move(route), span);
}

void Interpreter::setImportLoader(std::function<void(const std::string&, const SourceSpan&)> loader) {
    importLoader_ = std::move(loader);
}

void Interpreter::setTemplateRenderer(
    std::function<std::string(Interpreter&, const std::string&, const Value&, const SourceSpan&)> renderer) {
    templateRenderer_ = std::move(renderer);
}

void Interpreter::setInlineTemplateRenderer(
    std::function<std::string(Interpreter&, const std::string&, const SourceSpan&)> renderer) {
    inlineTemplateRenderer_ = std::move(renderer);
}

void Interpreter::setCurrentSourceName(std::string sourceName) {
    currentSourceName_ = std::move(sourceName);
}

const std::string& Interpreter::currentSourceName() const {
    return currentSourceName_;
}

void Interpreter::setCurrentRequest(Value request) {
    currentRequest_ = std::move(request);
}

void Interpreter::clearCurrentRequest() {
    currentRequest_ = Value {};
}

void Interpreter::setCurrentRouteParams(Value params) {
    currentRouteParams_ = std::move(params);
}

void Interpreter::clearCurrentRouteParams() {
    currentRouteParams_ = Value {};
}

void Interpreter::setCurrentSession(Value session) {
    currentSession_ = std::move(session);
}

void Interpreter::clearCurrentSession() {
    currentSession_ = Value {};
}

void Interpreter::setCurrentCsrfToken(std::string token) {
    currentCsrfToken_ = std::move(token);
}

const std::string& Interpreter::currentCsrfToken() const {
    return currentCsrfToken_;
}

void Interpreter::clearRequestContext() {
    clearCurrentRequest();
    clearCurrentRouteParams();
    clearCurrentSession();
    responseHeaders_.clear();
    currentCsrfToken_.clear();
}

void Interpreter::defineGlobal(std::string name, Value value, bool isConstant) {
    globals_->define(std::move(name), std::move(value), isConstant);
}

void Interpreter::registerComponent(std::string name, std::string source) {
    components_.insert_or_assign(std::move(name), std::move(source));
}

bool Interpreter::hasComponent(const std::string& name) const {
    return components_.find(name) != components_.end();
}

std::string Interpreter::componentSource(const std::string& name, const SourceSpan& span) const {
    const auto found = components_.find(name);
    if (found == components_.end()) {
        throw RuntimeError("Unknown component '" + name + "'.", span);
    }
    return found->second;
}

std::string Interpreter::renderView(const std::string& path, const Value& data, const SourceSpan& span) {
    if (!templateRenderer_) {
        throw RuntimeError("Template renderer is not configured.", span);
    }

    return templateRenderer_(*this, path, data, span);
}

std::string Interpreter::renderInlineTemplate(const std::string& source, const SourceSpan& span) {
    if (!inlineTemplateRenderer_) {
        throw RuntimeError("Inline template renderer is not configured.", span);
    }

    return inlineTemplateRenderer_(*this, source, span);
}

std::shared_ptr<Environment> Interpreter::globals() const {
    return globals_;
}

std::shared_ptr<Environment> Interpreter::currentEnvironment() const {
    return environment_;
}

const Value& Interpreter::currentRequest() const {
    return currentRequest_;
}

const Value& Interpreter::currentRouteParams() const {
    return currentRouteParams_;
}

const Value& Interpreter::currentSession() const {
    return currentSession_;
}

std::vector<std::string> Interpreter::componentSources() const {
    std::vector<std::string> sources;
    sources.reserve(components_.size());
    for (const auto& [name, source] : components_) {
        static_cast<void>(name);
        sources.push_back(source);
    }
    return sources;
}

bool Interpreter::hasRoute(const std::string& path, const std::string& method) const {
    return routes_.hasRoute(method, path);
}

Value Interpreter::renderRoute(const std::string& path, const std::string& method, const SourceSpan& span) {
    return routes_.render(method, path, *this, span);
}

std::vector<std::string> Interpreter::routePaths() const {
    return routes_.routePaths();
}

std::ostream& Interpreter::output() {
    return output_;
}

std::istream& Interpreter::input() {
    return input_;
}

void Interpreter::addResponseHeader(std::string name, std::string value) {
    responseHeaders_.emplace_back(std::move(name), std::move(value));
}

std::vector<std::pair<std::string, std::string>> Interpreter::consumeResponseHeaders() {
    auto headers = std::move(responseHeaders_);
    responseHeaders_.clear();
    return headers;
}

void Interpreter::copySnapshotTo(Interpreter& target) const {
    SnapshotCloneState state;
    auto cloneEnvironmentFn = [&state](const std::shared_ptr<Environment>& environment) {
        return cloneEnvironment(environment, state);
    };

    target.globals_ = cloneEnvironmentFn(globals_);
    target.environment_ = target.globals_;
    target.routes_ = routes_.clone(cloneEnvironmentFn);
    target.components_ = components_;
    target.currentRequest_ = Value {};
    target.currentRouteParams_ = Value {};
    target.currentSession_ = Value {};
    target.responseHeaders_.clear();
    target.currentCsrfToken_.clear();
    target.currentSourceName_ = currentSourceName_;
}

Value Interpreter::visitLiteralExpr(const LiteralExpr& expr) {
    return expr.value;
}

Value Interpreter::visitVariableExpr(const VariableExpr& expr) {
    return environment_->get(expr.name);
}

Value Interpreter::visitAssignExpr(const AssignExpr& expr) {
    const Value value = evaluate(*expr.value);
    environment_->assign(expr.name, value);
    return value;
}

Value Interpreter::visitUnaryExpr(const UnaryExpr& expr) {
    const Value right = evaluate(*expr.right);

    switch (expr.op.type) {
        case TokenType::Minus:
            return Value(-expectInteger(right, expr.op.span, "Unary '-'"));
        case TokenType::Bang:
            return Value(!right.isTruthy());
        default:
            throw RuntimeError("Unsupported unary operator '" + expr.op.lexeme + "'.", expr.op.span);
    }
}

Value Interpreter::visitBinaryExpr(const BinaryExpr& expr) {
    const Value left = evaluate(*expr.left);

    switch (expr.op.type) {
        case TokenType::AndAnd:
            if (!left.isTruthy()) {
                return Value(false);
            }
            return Value(evaluate(*expr.right).isTruthy());
        case TokenType::OrOr:
            if (left.isTruthy()) {
                return Value(true);
            }
            return Value(evaluate(*expr.right).isTruthy());
        default:
            break;
    }

    const Value right = evaluate(*expr.right);

    switch (expr.op.type) {
        case TokenType::Plus:
            if (left.isInteger() && right.isInteger()) {
                return Value(left.asInteger() + right.asInteger());
            }
            if (left.isString() || right.isString()) {
                return Value(left.toString() + right.toString());
            }
            throw RuntimeError("Operator '+' expects integers or at least one string operand.", expr.op.span);
        case TokenType::Minus:
            return Value(expectInteger(left, expr.op.span, "Left operand of '-'") -
                         expectInteger(right, expr.op.span, "Right operand of '-'"));
        case TokenType::Star:
            return Value(expectInteger(left, expr.op.span, "Left operand of '*'") *
                         expectInteger(right, expr.op.span, "Right operand of '*'"));
        case TokenType::Slash: {
            const auto divisor = expectInteger(right, expr.op.span, "Right operand of '/'");
            if (divisor == 0) {
                throw RuntimeError("Division by zero.", expr.op.span);
            }
            return Value(expectInteger(left, expr.op.span, "Left operand of '/'") / divisor);
        }
        case TokenType::Greater:
            return Value(expectInteger(left, expr.op.span, "Left operand of '>'") >
                         expectInteger(right, expr.op.span, "Right operand of '>'"));
        case TokenType::GreaterEqual:
            return Value(expectInteger(left, expr.op.span, "Left operand of '>='") >=
                         expectInteger(right, expr.op.span, "Right operand of '>='"));
        case TokenType::Less:
            return Value(expectInteger(left, expr.op.span, "Left operand of '<'") <
                         expectInteger(right, expr.op.span, "Right operand of '<'"));
        case TokenType::LessEqual:
            return Value(expectInteger(left, expr.op.span, "Left operand of '<='") <=
                         expectInteger(right, expr.op.span, "Right operand of '<='"));
        case TokenType::EqualEqual:
            return Value(left == right);
        case TokenType::BangEqual:
            return Value(left != right);
        default:
            throw RuntimeError("Unsupported binary operator '" + expr.op.lexeme + "'.", expr.op.span);
    }
}

Value Interpreter::visitGroupingExpr(const GroupingExpr& expr) {
    return evaluate(*expr.expression);
}

Value Interpreter::visitCallExpr(const CallExpr& expr) {
    if (const auto* property = dynamic_cast<const GetExpr*>(expr.callee.get())) {
        const Value object = evaluate(*property->object);
        if (object.isObject()) {
            const auto& map = object.asObject();
            const auto marker = map.find("__wevoa_request");
            if (marker != map.end() && marker->second.isBoolean() && marker->second.asBoolean() &&
                property->name.lexeme == "method") {
                if (!expr.arguments.empty()) {
                    throw RuntimeError("request.method() does not accept arguments.", expr.paren.span);
                }

                const auto methodValue = map.find("method");
                if (methodValue == map.end() || !methodValue->second.isString()) {
                    throw RuntimeError("Request object is missing its method value.", property->name.span);
                }

                return methodValue->second;
            }
        }
    }

    const Value callee = evaluate(*expr.callee);
    const auto& callable = expectCallable(callee, expr.paren.span, "Function call");

    std::vector<Value> arguments;
    arguments.reserve(expr.arguments.size());
    for (const auto& argument : expr.arguments) {
        arguments.push_back(evaluate(*argument));
    }

    if (const auto arity = callable->arity(); arity.has_value() && arguments.size() != *arity) {
        throw RuntimeError("Expected " + std::to_string(*arity) + " argument(s) but got " +
                               std::to_string(arguments.size()) + ".",
                           expr.paren.span);
    }

    return callable->call(*this, arguments, expr.paren.span);
}

Value Interpreter::visitArrayExpr(const ArrayExpr& expr) {
    Value::Array values;
    values.reserve(expr.elements.size());
    for (const auto& element : expr.elements) {
        values.push_back(evaluate(*element));
    }
    return Value(std::move(values));
}

Value Interpreter::visitObjectExpr(const ObjectExpr& expr) {
    Value::Object values;
    for (const auto& field : expr.fields) {
        values.insert_or_assign(field.key.lexeme, evaluate(*field.value));
    }
    return Value(std::move(values));
}

Value Interpreter::visitHtmlExpr(const HtmlExpr& expr) {
    return Value(renderInlineTemplate(expr.source, expr.span));
}

Value Interpreter::visitGetExpr(const GetExpr& expr) {
    const Value object = evaluate(*expr.object);
    const auto& map = expectObject(object, expr.name.span, "Property access");
    const auto found = map.find(expr.name.lexeme);
    if (found == map.end()) {
        throw RuntimeError("Object has no property '" + expr.name.lexeme + "'.", expr.name.span);
    }
    return found->second;
}

Value Interpreter::visitIndexExpr(const IndexExpr& expr) {
    const Value object = evaluate(*expr.object);
    const Value index = evaluate(*expr.index);

    if (object.isArray()) {
        const auto& array = object.asArray();
        const auto rawIndex = expectInteger(index, expr.bracket.span, "Array index");
        if (rawIndex < 0 || static_cast<std::size_t>(rawIndex) >= array.size()) {
            throw RuntimeError("Array index out of range.", expr.bracket.span);
        }
        return array[static_cast<std::size_t>(rawIndex)];
    }

    if (object.isString()) {
        const auto rawIndex = expectInteger(index, expr.bracket.span, "String index");
        const auto& value = object.asString();
        if (rawIndex < 0 || static_cast<std::size_t>(rawIndex) >= value.size()) {
            throw RuntimeError("String index out of range.", expr.bracket.span);
        }
        return Value(std::string(1, value[static_cast<std::size_t>(rawIndex)]));
    }

    if (object.isObject()) {
        if (!index.isString()) {
            throw RuntimeError("Object index must be a string.", expr.bracket.span);
        }

        const auto& map = object.asObject();
        const auto found = map.find(index.asString());
        if (found == map.end()) {
            throw RuntimeError("Object has no key '" + index.asString() + "'.", expr.bracket.span);
        }
        return found->second;
    }

    throw RuntimeError("Indexing requires an array, string, or object.", expr.bracket.span);
}

void Interpreter::visitExpressionStmt(const ExpressionStmt& stmt) {
    static_cast<void>(evaluate(*stmt.expression));
}

void Interpreter::visitVarDeclStmt(const VarDeclStmt& stmt) {
    Value value;
    if (stmt.initializer) {
        value = evaluate(*stmt.initializer);
    }

    environment_->define(stmt.name, std::move(value), stmt.isConstant);
}

void Interpreter::visitBlockStmt(const BlockStmt& stmt) {
    executeBlock(stmt.statements, std::make_shared<Environment>(environment_));
}

void Interpreter::visitIfStmt(const IfStmt& stmt) {
    if (evaluate(*stmt.condition).isTruthy()) {
        execute(*stmt.thenBranch);
    } else if (stmt.elseBranch) {
        execute(*stmt.elseBranch);
    }
}

void Interpreter::visitLoopStmt(const LoopStmt& stmt) {
    const auto previous = environment_;
    environment_ = std::make_shared<Environment>(environment_);

    try {
        if (stmt.initializer) {
            prepareLoopInitializer(*stmt.initializer);
            execute(*stmt.initializer);
        }

        while (!stmt.condition || evaluate(*stmt.condition).isTruthy()) {
            bool shouldBreak = false;

            try {
                execute(*stmt.body);
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                shouldBreak = true;
            }

            if (shouldBreak) {
                break;
            }

            if (stmt.increment) {
                static_cast<void>(evaluate(*stmt.increment));
            }
        }
    } catch (...) {
        environment_ = previous;
        throw;
    }

    environment_ = previous;
}

void Interpreter::visitWhileStmt(const WhileStmt& stmt) {
    while (evaluate(*stmt.condition).isTruthy()) {
        try {
            execute(*stmt.body);
        } catch (const ContinueSignal&) {
            continue;
        } catch (const BreakSignal&) {
            break;
        }
    }
}

void Interpreter::visitFuncDeclStmt(const FuncDeclStmt& stmt) {
    environment_->define(stmt.name, Value(std::make_shared<WevoaFunction>(&stmt, environment_)), true);
}

void Interpreter::visitRouteDeclStmt(const RouteDeclStmt& stmt) {
    const Value pathValue = evaluate(*stmt.path);
    if (!pathValue.isString()) {
        throw RuntimeError("Route path must evaluate to a string.", stmt.path->span);
    }

    const std::string& path = pathValue.asString();
    registerRoute(stmt.method,
                  path,
                  std::make_shared<RouteHandler>(stmt.method, path, &stmt, environment_, currentSourceName_),
                  stmt.span);
}

void Interpreter::visitComponentDeclStmt(const ComponentDeclStmt& stmt) {
    registerComponent(stmt.name.lexeme, stmt.source);
}

void Interpreter::visitImportStmt(const ImportStmt& stmt) {
    if (!importLoader_) {
        throw RuntimeError("Import loader is not configured.", stmt.keyword.span);
    }

    const Value pathValue = evaluate(*stmt.path);
    if (!pathValue.isString()) {
        throw RuntimeError("Import path must evaluate to a string.", stmt.path->span);
    }

    importLoader_(pathValue.asString(), stmt.span);
}

void Interpreter::visitReturnStmt(const ReturnStmt& stmt) {
    Value value;
    if (stmt.value) {
        value = evaluate(*stmt.value);
    }

    throw ReturnSignal(std::move(value), stmt.keyword.span);
}

void Interpreter::visitBreakStmt(const BreakStmt& stmt) {
    throw BreakSignal(stmt.keyword.span);
}

void Interpreter::visitContinueStmt(const ContinueStmt& stmt) {
    throw ContinueSignal(stmt.keyword.span);
}

std::int64_t Interpreter::expectInteger(const Value& value,
                                        const SourceSpan& span,
                                        std::string_view context) const {
    if (!value.isInteger()) {
        throw RuntimeError(std::string(context) + " must be an integer, got " + value.typeName() + ".", span);
    }

    return value.asInteger();
}

const Value::Array& Interpreter::expectArray(const Value& value,
                                             const SourceSpan& span,
                                             std::string_view context) const {
    if (!value.isArray()) {
        throw RuntimeError(std::string(context) + " requires an array, got " + value.typeName() + ".", span);
    }

    return value.asArray();
}

const Value::Object& Interpreter::expectObject(const Value& value,
                                               const SourceSpan& span,
                                               std::string_view context) const {
    if (!value.isObject()) {
        throw RuntimeError(std::string(context) + " requires an object, got " + value.typeName() + ".", span);
    }

    return value.asObject();
}

const std::shared_ptr<Callable>& Interpreter::expectCallable(const Value& value,
                                                             const SourceSpan& span,
                                                             std::string_view context) const {
    if (!value.isCallable()) {
        throw RuntimeError(std::string(context) + " requires a callable value, got " + value.typeName() + ".", span);
    }

    return value.asCallable();
}

void Interpreter::prepareLoopInitializer(const Stmt& initializer) {
    const auto* expressionStmt = dynamic_cast<const ExpressionStmt*>(&initializer);
    if (expressionStmt == nullptr) {
        return;
    }

    const auto* assignExpr = dynamic_cast<const AssignExpr*>(expressionStmt->expression.get());
    if (assignExpr == nullptr) {
        return;
    }

    if (!environment_->exists(assignExpr->name.lexeme)) {
        environment_->define(assignExpr->name, Value {}, false);
    }
}

}  // namespace wevoaweb
