#include "interpreter/callable.h"

#include <utility>

#include "ast/ast.h"
#include "interpreter/environment.h"
#include "interpreter/interpreter.h"

namespace wevoaweb {

NativeFunction::NativeFunction(std::string name, std::optional<std::size_t> arity, NativeCallback callback)
    : name_(std::move(name)), arity_(arity), callback_(std::move(callback)) {}

std::optional<std::size_t> NativeFunction::arity() const {
    return arity_;
}

Value NativeFunction::call(Interpreter& interpreter,
                           const std::vector<Value>& arguments,
                           const SourceSpan& span) const {
    return callback_(interpreter, arguments, span);
}

std::string NativeFunction::debugName() const {
    return "<native " + name_ + ">";
}

std::shared_ptr<Callable> NativeFunction::clone(
    const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& /*cloneEnvironment*/) const {
    return std::make_shared<NativeFunction>(name_, arity_, callback_);
}

WevoaFunction::WevoaFunction(const FuncDeclStmt* declaration, std::shared_ptr<Environment> closure)
    : declaration_(declaration), closure_(std::move(closure)) {}

std::optional<std::size_t> WevoaFunction::arity() const {
    return declaration_->params.size();
}

Value WevoaFunction::call(Interpreter& interpreter,
                          const std::vector<Value>& arguments,
                          const SourceSpan& /*span*/) const {
    auto environment = std::make_shared<Environment>(closure_);
    if (!interpreter.currentRequest().isNil()) {
        environment->define("request", interpreter.currentRequest(), true);
    }
    if (!interpreter.currentRouteParams().isNil()) {
        environment->define("params", interpreter.currentRouteParams(), true);
    }
    if (!interpreter.currentSession().isNil()) {
        environment->define("session", interpreter.currentSession(), true);
    }

    for (std::size_t i = 0; i < declaration_->params.size(); ++i) {
        environment->define(declaration_->params[i], arguments[i], false);
    }

    try {
        interpreter.executeBlock(declaration_->body->statements, environment);
    } catch (const ReturnSignal& signal) {
        return signal.value;
    }

    return Value {};
}

std::string WevoaFunction::debugName() const {
    return "<func " + declaration_->name.lexeme + ">";
}

std::shared_ptr<Callable> WevoaFunction::clone(
    const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const {
    return std::make_shared<WevoaFunction>(declaration_, cloneEnvironment(closure_));
}

}  // namespace wevoaweb
