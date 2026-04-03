#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "interpreter/value.h"
#include "runtime/token.h"

namespace wevoaweb {

class Environment {
  public:
    struct Binding {
        Value value;
        bool isConstant = false;
    };

    explicit Environment(std::shared_ptr<Environment> enclosing = nullptr);

    void define(const Token& name, Value value, bool isConstant);
    void define(const std::string& name,
                Value value,
                bool isConstant,
                std::optional<SourceSpan> span = std::nullopt);

    Value get(const Token& name) const;
    void assign(const Token& name, const Value& value);

    bool exists(const std::string& name) const;
    bool existsLocal(const std::string& name) const;
    const std::unordered_map<std::string, Binding>& bindings() const;
    const std::shared_ptr<Environment>& enclosing() const;

  private:
    std::unordered_map<std::string, Binding> values_;
    std::shared_ptr<Environment> enclosing_;
};

}  // namespace wevoaweb
