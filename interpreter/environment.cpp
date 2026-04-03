#include "interpreter/environment.h"

#include <utility>

#include "utils/error.h"

namespace wevoaweb {

Environment::Environment(std::shared_ptr<Environment> enclosing) : enclosing_(std::move(enclosing)) {}

void Environment::define(const Token& name, Value value, bool isConstant) {
    define(name.lexeme, std::move(value), isConstant, name.span);
}

void Environment::define(const std::string& name,
                         Value value,
                         bool isConstant,
                         std::optional<SourceSpan> span) {
    if (values_.find(name) != values_.end()) {
        throw RuntimeError("Variable '" + name + "' is already defined in this scope.",
                           span.value_or(SourceSpan {}));
    }

    values_.insert_or_assign(name, Binding {std::move(value), isConstant});
}

Value Environment::get(const Token& name) const {
    const auto found = values_.find(name.lexeme);
    if (found != values_.end()) {
        return found->second.value;
    }

    if (enclosing_) {
        return enclosing_->get(name);
    }

    throw RuntimeError("Undefined variable '" + name.lexeme + "'.", name.span);
}

void Environment::assign(const Token& name, const Value& value) {
    const auto found = values_.find(name.lexeme);
    if (found != values_.end()) {
        if (found->second.isConstant) {
            throw RuntimeError("Cannot reassign constant '" + name.lexeme + "'.", name.span);
        }

        found->second.value = value;
        return;
    }

    if (enclosing_) {
        enclosing_->assign(name, value);
        return;
    }

    throw RuntimeError("Undefined variable '" + name.lexeme + "'.", name.span);
}

bool Environment::exists(const std::string& name) const {
    if (values_.find(name) != values_.end()) {
        return true;
    }

    return enclosing_ ? enclosing_->exists(name) : false;
}

bool Environment::existsLocal(const std::string& name) const {
    return values_.find(name) != values_.end();
}

const std::unordered_map<std::string, Environment::Binding>& Environment::bindings() const {
    return values_;
}

const std::shared_ptr<Environment>& Environment::enclosing() const {
    return enclosing_;
}

}  // namespace wevoaweb
