#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "interpreter/value.h"
#include "utils/source.h"

namespace wevoaweb {

class Environment;
class FuncDeclStmt;
class Interpreter;

using NativeCallback = std::function<Value(Interpreter&, const std::vector<Value>&, const SourceSpan&)>;

class Callable {
  public:
    virtual ~Callable() = default;

    virtual std::optional<std::size_t> arity() const = 0;
    virtual Value call(Interpreter& interpreter, const std::vector<Value>& arguments, const SourceSpan& span) const = 0;
    virtual std::string debugName() const = 0;
    virtual std::shared_ptr<Callable> clone(
        const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const = 0;
};

class NativeFunction final : public Callable {
  public:
    NativeFunction(std::string name, std::optional<std::size_t> arity, NativeCallback callback);

    std::optional<std::size_t> arity() const override;
    Value call(Interpreter& interpreter, const std::vector<Value>& arguments, const SourceSpan& span) const override;
    std::string debugName() const override;
    std::shared_ptr<Callable> clone(
        const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const override;

  private:
    std::string name_;
    std::optional<std::size_t> arity_;
    NativeCallback callback_;
};

class WevoaFunction final : public Callable {
  public:
    WevoaFunction(const FuncDeclStmt* declaration, std::shared_ptr<Environment> closure);

    std::optional<std::size_t> arity() const override;
    Value call(Interpreter& interpreter, const std::vector<Value>& arguments, const SourceSpan& span) const override;
    std::string debugName() const override;
    std::shared_ptr<Callable> clone(
        const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const override;

  private:
    const FuncDeclStmt* declaration_;
    std::shared_ptr<Environment> closure_;
};

}  // namespace wevoaweb
