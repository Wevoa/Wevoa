#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wevoaweb {

class Callable;

class Value {
  public:
    using Array = std::vector<Value>;
    using Object = std::unordered_map<std::string, Value>;
    using Storage = std::variant<std::monostate,
                                 std::int64_t,
                                 std::string,
                                 bool,
                                 std::shared_ptr<Callable>,
                                 std::shared_ptr<Array>,
                                 std::shared_ptr<Object>>;

    Value() = default;
    explicit Value(std::int64_t value);
    explicit Value(std::string value);
    explicit Value(const char* value);
    explicit Value(bool value);
    explicit Value(std::shared_ptr<Callable> value);
    explicit Value(std::shared_ptr<Array> value);
    explicit Value(std::shared_ptr<Object> value);
    explicit Value(Array value);
    explicit Value(Object value);

    bool isNil() const;
    bool isInteger() const;
    bool isString() const;
    bool isBoolean() const;
    bool isCallable() const;
    bool isArray() const;
    bool isObject() const;

    std::int64_t asInteger() const;
    const std::string& asString() const;
    bool asBoolean() const;
    const std::shared_ptr<Callable>& asCallable() const;
    const Array& asArray() const;
    const Object& asObject() const;
    Array& asArray();
    Object& asObject();

    std::string typeName() const;
    std::string toString() const;
    bool isTruthy() const;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const {
        return !(*this == other);
    }

  private:
    Storage storage_ {};
};

}  // namespace wevoaweb
