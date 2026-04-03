#include "runtime/builtins.h"

#include "interpreter/interpreter.h"
#include "runtime/config_loader.h"
#include "runtime/sqlite_module.h"
#include "utils/error.h"
#include "utils/security.h"

namespace wevoaweb {

namespace {

Value makeResponseValue(int statusCode,
                        std::string reasonPhrase,
                        std::string contentType,
                        Value body,
                        Value::Object headers = {}) {
    Value::Object response;
    response.insert_or_assign("__wevoa_response", Value(true));
    response.insert_or_assign("status_code", Value(static_cast<std::int64_t>(statusCode)));
    response.insert_or_assign("reason_phrase", Value(std::move(reasonPhrase)));
    response.insert_or_assign("content_type", Value(std::move(contentType)));
    response.insert_or_assign("body", std::move(body));
    response.insert_or_assign("headers", Value(std::move(headers)));
    return Value(std::move(response));
}

std::string defaultReasonPhrase(int statusCode) {
    switch (statusCode) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 422:
            return "Unprocessable Entity";
        case 500:
            return "Internal Server Error";
        default:
            return "Response";
    }
}

}  // namespace

void registerBuiltins(Interpreter& interpreter) {
    interpreter.registerNative(
        "print",
        std::nullopt,
        [](Interpreter& runtime, const std::vector<Value>& arguments, const SourceSpan&) -> Value {
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                if (i > 0) {
                    runtime.output() << ' ';
                }
                runtime.output() << arguments[i].toString();
            }
            runtime.output() << '\n';
            return Value {};
        });

    interpreter.registerNative(
        "input",
        std::nullopt,
        [](Interpreter& runtime, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (arguments.size() > 1) {
                throw RuntimeError("input() accepts zero or one argument.", span);
            }

            if (!arguments.empty()) {
                runtime.output() << arguments.front().toString();
                runtime.output().flush();
            }

            std::string line;
            std::getline(runtime.input(), line);
            return Value(std::move(line));
        });

    interpreter.registerNative(
        "view",
        std::nullopt,
        [](Interpreter& runtime, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (arguments.empty() || arguments.size() > 2) {
                throw RuntimeError("view() accepts one or two arguments.", span);
            }

            if (!arguments[0].isString()) {
                throw RuntimeError("view() template path must be a string.", span);
            }

            const Value context = arguments.size() == 2 ? arguments[1] : Value(Value::Object {});
            return Value(runtime.renderView(arguments[0].asString(), context, span));
        });

    interpreter.registerNative(
        "len",
        1,
        [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            const Value& value = arguments.front();

            if (value.isString()) {
                return Value(static_cast<std::int64_t>(value.asString().size()));
            }
            if (value.isArray()) {
                return Value(static_cast<std::int64_t>(value.asArray().size()));
            }
            if (value.isObject()) {
                return Value(static_cast<std::int64_t>(value.asObject().size()));
            }

            throw RuntimeError("len() expects a string, array, or object.", span);
        });

    interpreter.registerNative(
        "append",
        2,
        [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (!arguments[0].isArray()) {
                throw RuntimeError("append() expects an array as its first argument.", span);
            }

            Value::Array result = arguments[0].asArray();
            result.push_back(arguments[1]);
            return Value(std::move(result));
        });

    interpreter.registerNative(
        "hash",
        1,
        [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (!arguments.front().isString()) {
                throw RuntimeError("hash() expects a password string.", span);
            }

            return Value(hashPassword(arguments.front().asString()));
        });

    interpreter.registerNative(
        "verify",
        2,
        [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (!arguments[0].isString() || !arguments[1].isString()) {
                throw RuntimeError("verify() expects a password string and a hash string.", span);
            }

            return Value(verifyPassword(arguments[0].asString(), arguments[1].asString()));
        });

    interpreter.registerNative(
        "verify_csrf",
        1,
        [](Interpreter& runtime, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (!arguments.front().isString()) {
                throw RuntimeError("verify_csrf() expects a token string.", span);
            }

            const std::string& expected = runtime.currentCsrfToken();
            if (expected.empty()) {
                return Value(false);
            }
            const std::string& candidate = arguments.front().asString();
            if (candidate.size() != expected.size()) {
                return Value(false);
            }

            unsigned char difference = 0;
            for (std::size_t index = 0; index < expected.size(); ++index) {
                difference |= static_cast<unsigned char>(expected[index] ^ candidate[index]);
            }

            return Value(difference == 0);
        });

    interpreter.registerNative(
        "json",
        1,
        [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            try {
                return makeResponseValue(200,
                                         "OK",
                                         "application/json; charset=utf-8",
                                         Value(serializeJson(arguments.front())));
            } catch (const std::exception& error) {
                throw RuntimeError(error.what(), span);
            }
        });

    interpreter.registerNative(
        "redirect",
        1,
        [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (!arguments.front().isString()) {
                throw RuntimeError("redirect() expects a target path string.", span);
            }

            Value::Object headers;
            headers.insert_or_assign("Location", arguments.front());
            return makeResponseValue(302,
                                     "Found",
                                     "text/html; charset=utf-8",
                                     Value("<p>Redirecting to " + arguments.front().asString() + "...</p>"),
                                     std::move(headers));
        });

    interpreter.registerNative(
        "status",
        std::nullopt,
        [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (arguments.size() < 2 || arguments.size() > 3) {
                throw RuntimeError("status() expects a status code, body, and optional content type.", span);
            }
            if (!arguments[0].isInteger()) {
                throw RuntimeError("status() expects an integer status code.", span);
            }
            if (arguments.size() == 3 && !arguments[2].isString()) {
                throw RuntimeError("status() optional content type must be a string.", span);
            }

            const int statusCode = static_cast<int>(arguments[0].asInteger());
            const std::string contentType =
                arguments.size() == 3 ? arguments[2].asString() : "text/html; charset=utf-8";
            return makeResponseValue(statusCode,
                                     defaultReasonPhrase(statusCode),
                                     contentType,
                                     arguments[1]);
        });

    registerSqliteModule(interpreter);
}

}  // namespace wevoaweb
