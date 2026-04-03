#include "runtime/config_loader.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

namespace wevoaweb {

namespace {

class JsonParser {
  public:
    explicit JsonParser(std::string source) : source_(std::move(source)) {}

    Value parse() {
        skipWhitespace();
        Value value = parseValue();
        skipWhitespace();
        if (!isAtEnd()) {
            throw std::runtime_error("Unexpected trailing characters in JSON.");
        }
        return value;
    }

  private:
    Value parseValue() {
        skipWhitespace();
        if (isAtEnd()) {
            throw std::runtime_error("Unexpected end of JSON.");
        }

        const char ch = peek();
        if (ch == '{') {
            return parseObject();
        }
        if (ch == '[') {
            return parseArray();
        }
        if (ch == '"') {
            return Value(parseString());
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '-') {
            return Value(parseInteger());
        }
        if (matchLiteral("true")) {
            return Value(true);
        }
        if (matchLiteral("false")) {
            return Value(false);
        }
        if (matchLiteral("null")) {
            return Value {};
        }

        throw std::runtime_error(std::string("Unexpected JSON token starting with '") + ch + "'.");
    }

    Value parseObject() {
        consume('{');
        Value::Object object;
        skipWhitespace();

        if (peek() == '}') {
            advance();
            return Value(std::move(object));
        }

        while (true) {
            skipWhitespace();
            const std::string key = parseString();
            skipWhitespace();
            consume(':');
            object.insert_or_assign(key, parseValue());
            skipWhitespace();

            if (peek() == '}') {
                advance();
                break;
            }

            consume(',');
        }

        return Value(std::move(object));
    }

    Value parseArray() {
        consume('[');
        Value::Array array;
        skipWhitespace();

        if (peek() == ']') {
            advance();
            return Value(std::move(array));
        }

        while (true) {
            array.push_back(parseValue());
            skipWhitespace();

            if (peek() == ']') {
                advance();
                break;
            }

            consume(',');
        }

        return Value(std::move(array));
    }

    std::string parseString() {
        consume('"');
        std::string value;

        while (!isAtEnd() && peek() != '"') {
            if (peek() == '\\') {
                advance();
                if (isAtEnd()) {
                    throw std::runtime_error("Unterminated JSON escape sequence.");
                }

                const char escaped = advance();
                switch (escaped) {
                    case '"':
                        value.push_back('"');
                        break;
                    case '\\':
                        value.push_back('\\');
                        break;
                    case '/':
                        value.push_back('/');
                        break;
                    case 'b':
                        value.push_back('\b');
                        break;
                    case 'f':
                        value.push_back('\f');
                        break;
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 'r':
                        value.push_back('\r');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    default:
                        throw std::runtime_error("Unsupported JSON escape sequence.");
                }
            } else {
                value.push_back(advance());
            }
        }

        consume('"');
        return value;
    }

    std::int64_t parseInteger() {
        std::size_t start = current_;
        if (peek() == '-') {
            advance();
        }

        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            advance();
        }

        return std::stoll(source_.substr(start, current_ - start));
    }

    bool matchLiteral(const std::string& literal) {
        if (source_.substr(current_, literal.size()) != literal) {
            return false;
        }

        current_ += literal.size();
        return true;
    }

    void skipWhitespace() {
        while (!isAtEnd() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            ++current_;
        }
    }

    void consume(char expected) {
        if (isAtEnd() || peek() != expected) {
            throw std::runtime_error(std::string("Expected '") + expected + "' in JSON.");
        }
        advance();
    }

    char peek() const {
        return isAtEnd() ? '\0' : source_[current_];
    }

    char advance() {
        return source_[current_++];
    }

    bool isAtEnd() const {
        return current_ >= source_.size();
    }

    std::string source_;
    std::size_t current_ = 0;
};

std::string escapeJsonString(const std::string& value) {
    std::ostringstream escaped;
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped << "\\\\";
                break;
            case '"':
                escaped << "\\\"";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                escaped << ch;
                break;
        }
    }

    return escaped.str();
}

void writeJsonValue(std::ostringstream& stream, const Value& value, int indentLevel) {
    const std::string indent(static_cast<std::size_t>(indentLevel) * 2, ' ');
    const std::string childIndent(static_cast<std::size_t>(indentLevel + 1) * 2, ' ');

    if (value.isNil()) {
        stream << "null";
        return;
    }

    if (value.isInteger()) {
        stream << value.asInteger();
        return;
    }

    if (value.isBoolean()) {
        stream << (value.asBoolean() ? "true" : "false");
        return;
    }

    if (value.isString()) {
        stream << '"' << escapeJsonString(value.asString()) << '"';
        return;
    }

    if (value.isArray()) {
        const auto& values = value.asArray();
        stream << '[';
        if (!values.empty()) {
            stream << '\n';
            for (std::size_t i = 0; i < values.size(); ++i) {
                stream << childIndent;
                writeJsonValue(stream, values[i], indentLevel + 1);
                if (i + 1 < values.size()) {
                    stream << ',';
                }
                stream << '\n';
            }
            stream << indent;
        }
        stream << ']';
        return;
    }

    if (value.isObject()) {
        const auto& object = value.asObject();
        stream << '{';
        if (!object.empty()) {
            stream << '\n';
            std::size_t index = 0;
            for (const auto& [key, entry] : object) {
                stream << childIndent << '"' << escapeJsonString(key) << "\": ";
                writeJsonValue(stream, entry, indentLevel + 1);
                if (++index < object.size()) {
                    stream << ',';
                }
                stream << '\n';
            }
            stream << indent;
        }
        stream << '}';
        return;
    }

    throw std::runtime_error("Cannot serialize value of type '" + value.typeName() + "' to JSON.");
}

}  // namespace

Value loadConfigFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return Value(Value::Object {});
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open config file: " + path.string());
    }

    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    Value value = JsonParser(contents).parse();
    if (!value.isObject()) {
        throw std::runtime_error("Configuration root must be a JSON object.");
    }

    return value;
}

Value parseJsonString(const std::string& source) {
    return JsonParser(source).parse();
}

std::string serializeJson(const Value& value) {
    std::ostringstream stream;
    writeJsonValue(stream, value, 0);
    stream << '\n';
    return stream.str();
}

}  // namespace wevoaweb
