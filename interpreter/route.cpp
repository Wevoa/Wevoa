#include "interpreter/route.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "ast/ast.h"
#include "interpreter/environment.h"
#include "interpreter/interpreter.h"
#include "utils/error.h"

namespace wevoaweb {

namespace {

std::string routeKey(const std::string& method, const std::string& path) {
    return method + " " + path;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> segments;
    std::stringstream stream(path);
    std::string segment;

    while (std::getline(stream, segment, '/')) {
        if (!segment.empty()) {
            segments.push_back(segment);
        }
    }

    return segments;
}

bool matchRoutePath(const std::string& pattern, const std::string& path, Value::Object& params) {
    if (pattern == path) {
        return true;
    }

    const auto patternSegments = splitPath(pattern);
    const auto pathSegments = splitPath(path);
    if (patternSegments.size() != pathSegments.size()) {
        return false;
    }

    for (std::size_t index = 0; index < patternSegments.size(); ++index) {
        const auto& patternSegment = patternSegments[index];
        const auto& pathSegment = pathSegments[index];

        if (!patternSegment.empty() && patternSegment.front() == ':' && patternSegment.size() > 1) {
            params.insert_or_assign(patternSegment.substr(1), Value(pathSegment));
            continue;
        }

        if (patternSegment != pathSegment) {
            return false;
        }
    }

    return true;
}

}  // namespace

RouteHandler::RouteHandler(std::string method,
                           std::string path,
                           const RouteDeclStmt* declaration,
                           std::shared_ptr<Environment> closure,
                           std::string sourceName)
    : method_(std::move(method)),
      path_(std::move(path)),
      declaration_(declaration),
      closure_(std::move(closure)),
      sourceName_(std::move(sourceName)) {}

const std::string& RouteHandler::method() const {
    return method_;
}

const std::string& RouteHandler::path() const {
    return path_;
}

Value RouteHandler::render(Interpreter& interpreter) const {
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

    for (const auto& middlewareName : declaration_->middleware) {
        const Value middleware = environment->get(middlewareName);
        if (!middleware.isCallable()) {
            throw RuntimeError("Middleware '" + middlewareName.lexeme + "' must be a callable value.",
                               middlewareName.span);
        }

        const Value result = middleware.asCallable()->call(interpreter, {}, middlewareName.span);
        if (result.isNil()) {
            continue;
        }

        if (result.isBoolean() && result.asBoolean()) {
            continue;
        }

        return result;
    }

    try {
        interpreter.executeBlock(declaration_->body->statements, environment);
    } catch (const ReturnSignal& signal) {
        return signal.value;
    } catch (const WevoaError& error) {
        throw std::runtime_error("[ERROR] Route: " + path_ + "\nFile: " + sourceName_ + ":" +
                                 std::to_string(error.span().start.line) + "\nMessage: " + error.what());
    }

    return Value {};
}

std::shared_ptr<RouteHandler> RouteHandler::clone(
    const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const {
    return std::make_shared<RouteHandler>(method_, path_, declaration_, cloneEnvironment(closure_), sourceName_);
}

void RouteRegistry::addRoute(std::shared_ptr<RouteHandler> route, const SourceSpan& span) {
    const auto [found, inserted] = routes_.emplace(routeKey(route->method(), route->path()), route);
    if (!inserted) {
        throw RuntimeError("Route '" + route->method() + " " + route->path() + "' is already defined.", span);
    }

    orderedRoutes_.push_back(std::move(route));
}

bool RouteRegistry::hasRoute(const std::string& method, const std::string& path) const {
    for (const auto& route : orderedRoutes_) {
        if (route->method() != method) {
            continue;
        }

        Value::Object params;
        if (matchRoutePath(route->path(), path, params)) {
            return true;
        }
    }

    return false;
}

Value RouteRegistry::render(const std::string& method,
                            const std::string& path,
                            Interpreter& interpreter,
                            const SourceSpan& span) const {
    for (const auto& route : orderedRoutes_) {
        if (route->method() != method) {
            continue;
        }

        Value::Object params;
        if (!matchRoutePath(route->path(), path, params)) {
            continue;
        }

        interpreter.setCurrentRouteParams(Value(std::move(params)));
        try {
            Value result = route->render(interpreter);
            interpreter.clearCurrentRouteParams();
            return result;
        } catch (...) {
            interpreter.clearCurrentRouteParams();
            throw;
        }
    }

    throw RuntimeError("No route matched '" + method + " " + path + "'.", span);
}

std::vector<std::string> RouteRegistry::routePaths() const {
    std::vector<std::string> paths;
    paths.reserve(routes_.size());
    for (const auto& entry : routes_) {
        paths.push_back(entry.first);
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

RouteRegistry RouteRegistry::clone(
    const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const {
    RouteRegistry cloned;
    for (const auto& route : orderedRoutes_) {
        cloned.orderedRoutes_.push_back(route->clone(cloneEnvironment));
    }

    for (const auto& route : cloned.orderedRoutes_) {
        cloned.routes_.insert_or_assign(routeKey(route->method(), route->path()), route);
    }

    return cloned;
}

}  // namespace wevoaweb
