#include "server/web_app.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include "interpreter/callable.h"
#include "runtime/config_loader.h"
#include "runtime/sqlite_module.h"
#include "utils/security.h"
#include "utils/error.h"

namespace wevoaweb::server {

namespace {

constexpr const char* kSessionCookieName = "WEVOA_SESSION";

std::string readFileContents(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open source file: " + path.string());
    }

    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool shouldExecuteAsRouteScript(const std::string& contents) {
    std::istringstream stream(contents);
    std::string line;
    bool sawMeaningfulLine = false;
    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }

        const std::string trimmed = line.substr(first);
        if (trimmed.rfind("//", 0) == 0) {
            continue;
        }

        if (!sawMeaningfulLine &&
            (trimmed.front() == '<' || trimmed.rfind("extend ", 0) == 0 || trimmed.rfind("section ", 0) == 0)) {
            return false;
        }

        sawMeaningfulLine = true;
        if (trimmed.rfind("route ", 0) == 0) {
            return true;
        }
    }

    return false;
}

std::int64_t integerConfigValue(const Value& config, const std::string& key, std::int64_t fallback) {
    if (!config.isObject()) {
        return fallback;
    }

    const auto found = config.asObject().find(key);
    if (found == config.asObject().end() || !found->second.isInteger()) {
        return fallback;
    }

    return found->second.asInteger();
}

std::int64_t integerConfigValue(const Value& config,
                                const std::initializer_list<std::string>& keys,
                                std::int64_t fallback) {
    for (const auto& key : keys) {
        const auto value = integerConfigValue(config, key, fallback);
        if (value != fallback || (!config.isObject() ? false : config.asObject().find(key) != config.asObject().end())) {
            return value;
        }
    }

    return fallback;
}

bool booleanConfigValue(const Value& config, const std::string& key, bool fallback) {
    if (!config.isObject()) {
        return fallback;
    }

    const auto found = config.asObject().find(key);
    if (found == config.asObject().end() || !found->second.isBoolean()) {
        return fallback;
    }

    return found->second.asBoolean();
}

std::size_t sizeConfigValue(const Value& config,
                            const std::initializer_list<std::string>& keys,
                            std::size_t fallback) {
    if (!config.isObject()) {
        return fallback;
    }

    for (const auto& key : keys) {
        const auto found = config.asObject().find(key);
        if (found == config.asObject().end() || !found->second.isInteger()) {
            continue;
        }

        return found->second.asInteger() > 0 ? static_cast<std::size_t>(found->second.asInteger()) : fallback;
    }

    return fallback;
}

std::string stringConfigValue(const Value& config, const std::string& key, const std::string& fallback) {
    if (!config.isObject()) {
        return fallback;
    }

    const auto found = config.asObject().find(key);
    if (found == config.asObject().end() || !found->second.isString()) {
        return fallback;
    }

    return found->second.asString();
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string getHeaderValue(const std::vector<std::pair<std::string, std::string>>& headers, const std::string& name) {
    for (const auto& [headerName, headerValue] : headers) {
        if (headerName == name) {
            return headerValue;
        }
    }

    return "";
}

std::optional<std::string> parseCookieValue(const std::string& headerValue, const std::string& key) {
    if (headerValue.empty()) {
        return std::nullopt;
    }

    std::stringstream stream(headerValue);
    std::string segment;
    while (std::getline(stream, segment, ';')) {
        segment = trim(segment);
        const auto separator = segment.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string name = trim(segment.substr(0, separator));
        if (name != key) {
            continue;
        }

        return segment.substr(separator + 1);
    }

    return std::nullopt;
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
        case 405:
            return "Method Not Allowed";
        case 422:
            return "Unprocessable Entity";
        case 500:
            return "Internal Server Error";
        default:
            return "Response";
    }
}

Value::Object stringMapToObject(const std::unordered_map<std::string, std::string>& values) {
    Value::Object object;
    for (const auto& [key, value] : values) {
        object.insert_or_assign(key, Value(value));
    }
    return object;
}

Value uploadedFileToValue(const UploadedFile& file) {
    Value::Object object;
    object.insert_or_assign("field", Value(file.fieldName));
    object.insert_or_assign("name", Value(file.fileName));
    object.insert_or_assign("content_type", Value(file.contentType));
    object.insert_or_assign("path", Value(file.tempPath));
    object.insert_or_assign("size", Value(static_cast<std::int64_t>(file.size)));
    return Value(std::move(object));
}

Value makeLookupFunctionValue(std::string name, std::unordered_map<std::string, std::string> values) {
    return Value(std::make_shared<NativeFunction>(
        std::move(name),
        1,
        [values = std::move(values)](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (arguments.size() != 1 || !arguments.front().isString()) {
                throw RuntimeError("Request lookup expects one string key.", span);
            }

            const auto found = values.find(arguments.front().asString());
            if (found == values.end()) {
                return Value(std::string());
            }

            return Value(found->second);
        }));
}

Value makeFileLookupValue(const std::vector<UploadedFile>& files) {
    return Value(std::make_shared<NativeFunction>(
        "request.file",
        1,
        [files](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
            if (arguments.size() != 1 || !arguments.front().isString()) {
                throw RuntimeError("request.file() expects one string field name.", span);
            }

            for (const auto& file : files) {
                if (file.fieldName == arguments.front().asString()) {
                    return uploadedFileToValue(file);
                }
            }

            return Value {};
        }));
}

Value makeJsonLookupValue(const HttpRequest& request) {
    return Value(std::make_shared<NativeFunction>(
        "request.json",
        0,
        [request](Interpreter&, const std::vector<Value>&, const SourceSpan& span) -> Value {
            if (!request.jsonBody.has_value()) {
                return Value(Value::Object {});
            }

            if (!request.jsonBody->isObject()) {
                throw RuntimeError("request.json() expects the JSON body root to be an object.", span);
            }

            return *request.jsonBody;
        }));
}

Value makeCsrfValue(const std::string& csrfToken) {
    return Value(std::make_shared<NativeFunction>(
        "request.csrf",
        0,
        [csrfToken](Interpreter&, const std::vector<Value>&, const SourceSpan&) -> Value { return Value(csrfToken); }));
}

Value makeRequestValue(const HttpRequest& request, const std::string& csrfToken) {
    Value::Object object;
    Value::Array files;
    files.reserve(request.files.size());
    for (const auto& file : request.files) {
        files.emplace_back(uploadedFileToValue(file));
    }

    object.insert_or_assign("__wevoa_request", Value(true));
    object.insert_or_assign("method", Value(request.method));
    object.insert_or_assign("path", Value(request.path));
    object.insert_or_assign("target", Value(request.target));
    object.insert_or_assign("body", Value(request.body));
    object.insert_or_assign("query", makeLookupFunctionValue("request.query", request.queryParameters));
    object.insert_or_assign("form", makeLookupFunctionValue("request.form", request.formParameters));
    object.insert_or_assign("file", makeFileLookupValue(request.files));
    object.insert_or_assign("json", makeJsonLookupValue(request));
    object.insert_or_assign("csrf", makeCsrfValue(csrfToken));
    object.insert_or_assign("csrf_token", Value(csrfToken));
    object.insert_or_assign("query_data", Value(stringMapToObject(request.queryParameters)));
    object.insert_or_assign("form_data", Value(stringMapToObject(request.formParameters)));
    object.insert_or_assign("files_data", Value(std::move(files)));
    object.insert_or_assign("json_valid", Value(!request.jsonError.has_value()));
    object.insert_or_assign("json_error", Value(request.jsonError.value_or("")));
    object.insert_or_assign("json_data",
                            request.jsonBody.has_value() && request.jsonBody->isObject() ? *request.jsonBody : Value {});
    return Value(std::move(object));
}

Value makeSessionValue(SessionStore& store,
                       const std::string& sessionId,
                       Interpreter& interpreter,
                       const std::string& cookieAttributes) {
    Value::Object sessionObject;
    sessionObject.insert_or_assign("id", Value(sessionId));
    sessionObject.insert_or_assign(
        "get",
        Value(std::make_shared<NativeFunction>(
            "session.get",
            1,
            [&store, sessionId](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
                if (!arguments.front().isString()) {
                    throw RuntimeError("session.get() expects a string key.", span);
                }

                return store.get(sessionId, arguments.front().asString());
            })));
    sessionObject.insert_or_assign(
        "set",
        Value(std::make_shared<NativeFunction>(
            "session.set",
            2,
            [&store, sessionId, &interpreter, cookieAttributes](Interpreter&,
                                                                const std::vector<Value>& arguments,
                                                                const SourceSpan& span) -> Value {
                if (!arguments.front().isString()) {
                    throw RuntimeError("session.set() expects a string key.", span);
                }

                store.set(sessionId, arguments.front().asString(), arguments[1]);
                interpreter.addResponseHeader("Set-Cookie",
                                              std::string(kSessionCookieName) + "=" + sessionId + cookieAttributes);
                return arguments[1];
            })));
    sessionObject.insert_or_assign(
        "delete",
        Value(std::make_shared<NativeFunction>(
            "session.delete",
            1,
            [&store, sessionId, &interpreter, cookieAttributes](Interpreter&,
                                                                const std::vector<Value>& arguments,
                                                                const SourceSpan& span) -> Value {
                if (!arguments.front().isString()) {
                    throw RuntimeError("session.delete() expects a string key.", span);
                }

                const bool removed = store.erase(sessionId, arguments.front().asString());
                if (removed) {
                    interpreter.addResponseHeader("Set-Cookie",
                                                  std::string(kSessionCookieName) + "=" + sessionId + cookieAttributes);
                }
                return Value(removed);
            })));
    return Value(std::move(sessionObject));
}

bool isResponseValue(const Value& value) {
    if (!value.isObject()) {
        return false;
    }

    const auto& object = value.asObject();
    const auto marker = object.find("__wevoa_response");
    return marker != object.end() && marker->second.isBoolean() && marker->second.asBoolean();
}

HttpResponse responseFromValue(const Value& value) {
    if (!isResponseValue(value)) {
        return HttpResponse {200, "OK", "text/html; charset=utf-8", value.isNil() ? "" : value.toString(), {},
                             0,   0,    0,                        0,                                       ""};
    }

    const auto& object = value.asObject();
    HttpResponse response;

    if (const auto found = object.find("status_code"); found != object.end() && found->second.isInteger()) {
        response.statusCode = static_cast<int>(found->second.asInteger());
        response.reasonPhrase = defaultReasonPhrase(response.statusCode);
    }

    if (const auto found = object.find("reason_phrase"); found != object.end() && found->second.isString()) {
        response.reasonPhrase = found->second.asString();
    }

    if (const auto found = object.find("content_type"); found != object.end() && found->second.isString()) {
        response.contentType = found->second.asString();
    }

    if (const auto found = object.find("body"); found != object.end()) {
        response.body = found->second.isNil() ? "" : found->second.toString();
    }

    if (const auto found = object.find("headers"); found != object.end() && found->second.isObject()) {
        for (const auto& [name, headerValue] : found->second.asObject()) {
            response.headers.emplace_back(name, headerValue.toString());
        }
    }

    return response;
}

bool requiresCsrfValidation(const std::string& method) {
    return method == "POST" || method == "PUT" || method == "PATCH" || method == "DELETE";
}

std::string extractCsrfToken(const HttpRequest& request) {
    const std::string headerToken = getHeaderValue(request.headers, "x-csrf-token");
    if (!headerToken.empty()) {
        return headerToken;
    }

    const auto formToken = request.formParameters.find("_csrf");
    if (formToken != request.formParameters.end()) {
        return formToken->second;
    }

    if (request.jsonBody.has_value() && request.jsonBody->isObject()) {
        const auto found = request.jsonBody->asObject().find("_csrf");
        if (found != request.jsonBody->asObject().end() && found->second.isString()) {
            return found->second.asString();
        }
    }

    return "";
}

std::vector<std::filesystem::path> listWevFiles(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;

    std::vector<fs::path> files;
    std::error_code error;
    if (!fs::exists(directory, error) || !fs::is_directory(directory, error) || error) {
        return files;
    }

    for (fs::recursive_directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            throw std::runtime_error("Unable to inspect templates in: " + directory.string());
        }

        if (iterator->is_regular_file(error) && iterator->path().extension() == ".wev" && !error) {
            files.push_back(iterator->path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

WebApplication::WebApplication(std::string scriptsDirectory,
                               std::string viewsDirectory,
                               std::string publicDirectory,
                               std::istream& input,
                               std::ostream& output,
                               bool debugAst,
                               Value config)
    : scriptsDirectory_(std::move(scriptsDirectory)),
      viewsDirectory_(std::move(viewsDirectory)),
      publicDirectory_(std::move(publicDirectory)),
      templateEngine_(viewsDirectory_),
      config_(std::move(config)),
      input_(&input),
      output_(&output),
      debugAst_(debugAst),
      session_(input, output, debugAst) {
    session_.interpreter().setTemplateRenderer(
        [this](Interpreter& interpreter, const std::string& path, const Value& data, const SourceSpan& span) {
            return templateEngine_.render(path, data, interpreter, span);
        });
    session_.interpreter().setInlineTemplateRenderer(
        [this](Interpreter& interpreter, const std::string& source, const SourceSpan&) {
            return templateEngine_.renderInline(source, interpreter);
        });
    const auto projectRoot = std::filesystem::path(scriptsDirectory_).parent_path();
    session_.addPackageRoot(projectRoot / "packages");
    session_.defineGlobal("config", config_, true);

    const auto ttlSeconds = std::max<std::int64_t>(1, sessionTimeToLiveSeconds());
    sessionStore_.setTimeToLive(std::chrono::seconds(ttlSeconds));
    sessionStore_.setCleanupInterval(
        std::chrono::seconds(std::max<std::int64_t>(1, integerConfigValue(config_,
                                                                          {"session_cleanup_interval", "session_cleanup_seconds"},
                                                                          30))));
    sessionStore_.setMaxSessions(maxSessionCount());
    templateEngine_.setStrictCache(productionMode());
    templateEngine_.setCacheLimits(templateCacheLimit(), fragmentCacheLimit());
}

void WebApplication::loadViews() {
    namespace fs = std::filesystem;

    const fs::path templatesRoot(viewsDirectory_);
    if (!fs::exists(templatesRoot) || !fs::is_directory(templatesRoot)) {
        throw std::runtime_error("Views directory not found: " + viewsDirectory_);
    }

    fs::path scriptsRoot(scriptsDirectory_);
    if (!fs::exists(scriptsRoot) || !fs::is_directory(scriptsRoot)) {
        scriptsRoot = templatesRoot;
    }

    const auto scriptFiles = listWevFiles(scriptsRoot);
    scripts_.clear();
    for (const auto& scriptFile : scriptFiles) {
        const std::string contents = readFileContents(scriptFile);
        if (!shouldExecuteAsRouteScript(contents)) {
            continue;
        }

        scripts_.push_back(ScriptSource {scriptFile.string(), contents});
        session_.runSource(scriptFile.string(), contents);
    }

    if (scriptFiles.empty()) {
        throw std::runtime_error("No .wev source files were found in: " + scriptsRoot.string());
    }

    if (session_.routePaths().empty()) {
        throw std::runtime_error("No routes were registered from source files in: " + scriptsRoot.string());
    }

    routePaths_ = session_.routePaths();
    viewPaths_ = viewPaths();
    validateTemplates();
    if (productionMode()) {
        templateEngine_.preloadTemplates(viewPaths_);
        templateEngine_.preloadTemplateFragments(session_.inlineTemplateSources());
        templateEngine_.preloadTemplateFragments(session_.componentSources(), viewPaths_);
    }
}

void WebApplication::validateTemplates() const {
    for (const auto& viewPath : viewPaths_) {
        templateEngine_.validateTemplate(viewPath);
    }
}

bool WebApplication::hasRoute(const std::string& path, const std::string& method) const {
    return session_.hasRoute(path, method);
}

HttpResponse WebApplication::render(const HttpRequest& request) {
    const auto requestStarted = std::chrono::steady_clock::now();
    const auto existingSessionId = parseCookieValue(getHeaderValue(request.headers, "cookie"), kSessionCookieName);
    const std::string sessionId = sessionStore_.ensureSession(existingSessionId);
    Value csrfTokenValue = sessionStore_.get(sessionId, "_csrf_token");
    if (!csrfTokenValue.isString() || csrfTokenValue.asString().empty()) {
        csrfTokenValue = Value(generateSecureToken(16));
        sessionStore_.set(sessionId, "_csrf_token", csrfTokenValue);
    }
    const std::string csrfToken = csrfTokenValue.asString();

    auto requestSession = makeRequestSession();
    requestSession->interpreter().clearRequestContext();
    requestSession->setCurrentRequest(makeRequestValue(request, csrfToken));
    requestSession->interpreter().setCurrentSession(
        makeSessionValue(sessionStore_, sessionId, requestSession->interpreter(), sessionCookieAttributes()));
    requestSession->interpreter().setCurrentCsrfToken(csrfToken);

    if (!existingSessionId.has_value()) {
        requestSession->interpreter().addResponseHeader("Set-Cookie",
                                                        std::string(kSessionCookieName) + "=" + sessionId +
                                                            sessionCookieAttributes());
    }

    if (requiresCsrfValidation(request.method) && extractCsrfToken(request) != csrfToken) {
        HttpResponse forbidden;
        forbidden.statusCode = 403;
        forbidden.reasonPhrase = "Forbidden";
        forbidden.body = "<h1>403 Forbidden</h1><p>Invalid or missing CSRF token.</p>";
        forbidden.requestMicros =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - requestStarted)
                                           .count());
        const auto pendingHeaders = requestSession->interpreter().consumeResponseHeaders();
        forbidden.headers.insert(forbidden.headers.end(), pendingHeaders.begin(), pendingHeaders.end());
        requestSession->interpreter().clearRequestContext();
        return forbidden;
    }

    try {
        TemplateEngine::resetThreadMetrics();
        resetSqliteThreadMetrics();
        const auto routeStarted = std::chrono::steady_clock::now();
        HttpResponse response = responseFromValue(requestSession->renderRoute(request.path, request.method));
        response.routeMicros =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - routeStarted)
                                           .count());
        response.templateMicros = TemplateEngine::consumeThreadRenderMicros();
        response.dbMicros = consumeSqliteThreadMetrics();
        response.requestMicros =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - requestStarted)
                                           .count());
        const auto pendingHeaders = requestSession->interpreter().consumeResponseHeaders();
        response.headers.insert(response.headers.end(), pendingHeaders.begin(), pendingHeaders.end());
        requestSession->interpreter().clearRequestContext();
        return response;
    } catch (...) {
        requestSession->interpreter().clearRequestContext();
        throw;
    }
}

std::optional<WebApplication::StaticAsset> WebApplication::loadStaticAsset(const std::string& path) const {
    const auto assetPath = resolveStaticPath(path);
    if (!assetPath.has_value()) {
        return std::nullopt;
    }

    std::error_code error;
    if (!std::filesystem::exists(*assetPath, error) || !std::filesystem::is_regular_file(*assetPath, error) || error) {
        return std::nullopt;
    }

    return StaticAsset {contentTypeForPath(*assetPath), readFileContents(*assetPath)};
}

std::vector<std::string> WebApplication::routePaths() const {
    return routePaths_;
}

std::vector<std::string> WebApplication::viewPaths() const {
    if (!viewPaths_.empty()) {
        return viewPaths_;
    }

    std::vector<std::string> views;
    for (const auto& viewPath : listWevFiles(viewsDirectory_)) {
        std::error_code error;
        const auto relative = std::filesystem::relative(viewPath, viewsDirectory_, error);
        views.push_back((error ? viewPath.filename() : relative).generic_string());
    }

    std::sort(views.begin(), views.end());
    return views;
}

Value::Array WebApplication::compiledTemplateManifest() const {
    return templateEngine_.compiledTemplateManifest();
}

Value WebApplication::snapshotManifest() const {
    Value::Object manifest;
    manifest.insert_or_assign("route_count", Value(static_cast<std::int64_t>(routePaths_.size())));
    manifest.insert_or_assign("view_count", Value(static_cast<std::int64_t>(viewPaths_.size())));
    manifest.insert_or_assign("component_count", Value(static_cast<std::int64_t>(session_.componentSources().size())));
    manifest.insert_or_assign("inline_template_count",
                              Value(static_cast<std::int64_t>(session_.inlineTemplateSources().size())));
    manifest.insert_or_assign("worker_threads", Value(static_cast<std::int64_t>(workerThreads())));
    manifest.insert_or_assign("max_queue_size", Value(static_cast<std::int64_t>(maxQueueSize())));
    manifest.insert_or_assign("template_cache_limit", Value(static_cast<std::int64_t>(templateCacheLimit())));
    manifest.insert_or_assign("fragment_cache_limit", Value(static_cast<std::int64_t>(fragmentCacheLimit())));
    manifest.insert_or_assign("max_session_count", Value(static_cast<std::int64_t>(maxSessionCount())));
    manifest.insert_or_assign("session_ttl", Value(sessionTimeToLiveSeconds()));
    manifest.insert_or_assign("mode", Value(productionMode() ? "production" : "development"));
    return Value(std::move(manifest));
}

std::size_t WebApplication::workerThreads() const {
    const auto configured = sizeConfigValue(config_, {"worker_threads"}, 0);
    if (configured > 0) {
        return configured;
    }

    const auto hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 4 : static_cast<std::size_t>(hardware);
}

std::size_t WebApplication::maxQueueSize() const {
    return sizeConfigValue(config_, {"max_queue_size"}, workerThreads() * 16);
}

std::size_t WebApplication::templateCacheLimit() const {
    return sizeConfigValue(config_, {"max_template_cache_size", "template_cache_limit"}, 256);
}

std::size_t WebApplication::fragmentCacheLimit() const {
    return sizeConfigValue(config_, {"max_fragment_cache_size", "fragment_cache_limit"}, 512);
}

std::size_t WebApplication::maxSessionCount() const {
    return sizeConfigValue(config_, {"max_session_count"}, 4096);
}

std::int64_t WebApplication::sessionTimeToLiveSeconds() const {
    return integerConfigValue(config_, {"session_ttl", "session_ttl_seconds"}, 3600);
}

bool WebApplication::performanceMetricsEnabled() const {
    return booleanConfigValue(config_, "performance_metrics", false);
}

const std::string& WebApplication::scriptsDirectory() const {
    return scriptsDirectory_;
}

const std::string& WebApplication::viewsDirectory() const {
    return viewsDirectory_;
}

const std::string& WebApplication::publicDirectory() const {
    return publicDirectory_;
}

std::unique_ptr<RuntimeSession> WebApplication::makeRequestSession() const {
    auto requestSession = session_.cloneSnapshot();
    requestSession->interpreter().setTemplateRenderer(
        [this](Interpreter& interpreter, const std::string& path, const Value& data, const SourceSpan& span) {
            return templateEngine_.render(path, data, interpreter, span);
        });
    requestSession->interpreter().setInlineTemplateRenderer(
        [this](Interpreter& interpreter, const std::string& source, const SourceSpan&) {
            return templateEngine_.renderInline(source, interpreter);
        });
    return requestSession;
}

bool WebApplication::productionMode() const {
    return stringConfigValue(config_, "env", "dev") == "production";
}

bool WebApplication::secureCookies() const {
    return booleanConfigValue(config_, "secure_cookies", false);
}

std::string WebApplication::sessionCookieAttributes() const {
    std::string attributes = "; Path=/; HttpOnly; SameSite=Lax";
    if (secureCookies()) {
        attributes += "; Secure";
    }
    return attributes;
}

std::string WebApplication::contentTypeForPath(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();

    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".txt") {
        return "text/plain; charset=utf-8";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".ico") {
        return "image/x-icon";
    }
    if (extension == ".json") {
        return "application/json; charset=utf-8";
    }

    return "application/octet-stream";
}

std::optional<std::filesystem::path> WebApplication::resolveStaticPath(const std::string& requestPath) const {
    if (requestPath.empty() || requestPath == "/") {
        return std::nullopt;
    }

    std::filesystem::path relativePath(requestPath.substr(1));
    relativePath = relativePath.lexically_normal();

    if (relativePath.empty() || relativePath.is_absolute()) {
        return std::nullopt;
    }

    for (const auto& part : relativePath) {
        if (part == "..") {
            return std::nullopt;
        }
    }

    return std::filesystem::path(publicDirectory_) / relativePath;
}

}  // namespace wevoaweb::server
