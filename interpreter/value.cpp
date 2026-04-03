#include "interpreter/value.h"

#include <sstream>
#include <utility>

#include "interpreter/callable.h"

namespace wevoaweb {

Value::Value(std::int64_t value) : storage_(value) {}

Value::Value(std::string value) : storage_(std::move(value)) {}

Value::Value(const char* value) : storage_(std::string(value)) {}

Value::Value(bool value) : storage_(value) {}

Value::Value(std::shared_ptr<Callable> value) : storage_(std::move(value)) {}

Value::Value(std::shared_ptr<Array> value) : storage_(std::move(value)) {}

Value::Value(std::shared_ptr<Object> value) : storage_(std::move(value)) {}

Value::Value(Array value) : storage_(std::make_shared<Array>(std::move(value))) {}

Value::Value(Object value) : storage_(std::make_shared<Object>(std::move(value))) {}

bool Value::isNil() const {
    return std::holds_alternative<std::monostate>(storage_);
}

bool Value::isInteger() const {
    return std::holds_alternative<std::int64_t>(storage_);
}

bool Value::isString() const {
    return std::holds_alternative<std::string>(storage_);
}

bool Value::isBoolean() const {
    return std::holds_alternative<bool>(storage_);
}

bool Value::isCallable() const {
    return std::holds_alternative<std::shared_ptr<Callable>>(storage_);
}

bool Value::isArray() const {
    return std::holds_alternative<std::shared_ptr<Array>>(storage_);
}

bool Value::isObject() const {
    return std::holds_alternative<std::shared_ptr<Object>>(storage_);
}

std::int64_t Value::asInteger() const {
    return std::get<std::int64_t>(storage_);
}

const std::string& Value::asString() const {
    return std::get<std::string>(storage_);
}

bool Value::asBoolean() const {
    return std::get<bool>(storage_);
}

const std::shared_ptr<Callable>& Value::asCallable() const {
    return std::get<std::shared_ptr<Callable>>(storage_);
}

const Value::Array& Value::asArray() const {
    return *std::get<std::shared_ptr<Array>>(storage_);
}

const Value::Object& Value::asObject() const {
    return *std::get<std::shared_ptr<Object>>(storage_);
}

Value::Array& Value::asArray() {
    return *std::get<std::shared_ptr<Array>>(storage_);
}

Value::Object& Value::asObject() {
    return *std::get<std::shared_ptr<Object>>(storage_);
}

std::string Value::typeName() const {
    if (isNil()) {
        return "nil";
    }
    if (isInteger()) {
        return "integer";
    }
    if (isString()) {
        return "string";
    }
    if (isBoolean()) {
        return "boolean";
    }
    if (isCallable()) {
        return "callable";
    }
    if (isArray()) {
        return "array";
    }
    if (isObject()) {
        return "object";
    }
    return "unknown";
}

std::string Value::toString() const {
    if (isNil()) {
        return "nil";
    }
    if (isInteger()) {
        return std::to_string(asInteger());
    }
    if (isString()) {
        return asString();
    }
    if (isBoolean()) {
        return asBoolean() ? "true" : "false";
    }
    if (isCallable()) {
        return asCallable()->debugName();
    }
    if (isArray()) {
        std::ostringstream stream;
        stream << '[';
        const auto& elements = asArray();
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (i > 0) {
                stream << ", ";
            }
            stream << elements[i].toString();
        }
        stream << ']';
        return stream.str();
    }
    if (isObject()) {
        std::ostringstream stream;
        stream << '{';
        bool first = true;
        for (const auto& [key, value] : asObject()) {
            if (!first) {
                stream << ", ";
            }
            first = false;
            stream << key << ": " << value.toString();
        }
        stream << '}';
        return stream.str();
    }
    return "<unknown>";
}

bool Value::isTruthy() const {
    if (isNil()) {
        return false;
    }
    if (isBoolean()) {
        return asBoolean();
    }
    if (isInteger()) {
        return asInteger() != 0;
    }
    if (isString()) {
        return !asString().empty();
    }
    if (isArray()) {
        return !asArray().empty();
    }
    if (isObject()) {
        return !asObject().empty();
    }
    return true;
}

bool Value::operator==(const Value& other) const {
    if (storage_.index() != other.storage_.index()) {
        return false;
    }

    if (isNil()) {
        return true;
    }
    if (isInteger()) {
        return asInteger() == other.asInteger();
    }
    if (isString()) {
        return asString() == other.asString();
    }
    if (isBoolean()) {
        return asBoolean() == other.asBoolean();
    }
    if (isCallable()) {
        return asCallable() == other.asCallable();
    }
    if (isArray()) {
        return asArray() == other.asArray();
    }
    if (isObject()) {
        return asObject() == other.asObject();
    }
    return false;
}

}  // namespace wevoaweb
