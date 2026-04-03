#pragma once

#include <memory>
#include <optional>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

#include "interpreter/value.h"
#include "utils/source.h"

namespace wevoaweb {

class Environment;
class Interpreter;
class RouteDeclStmt;

// RouteHandler keeps a route body plus its captured lexical scope so the
// server can render HTML later without re-parsing the view source.
class RouteHandler final {
  public:
    RouteHandler(std::string method,
                 std::string path,
                 const RouteDeclStmt* declaration,
                 std::shared_ptr<Environment> closure,
                 std::string sourceName);

    const std::string& method() const;
    const std::string& path() const;
    Value render(Interpreter& interpreter) const;
    std::shared_ptr<RouteHandler> clone(
        const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const;

  private:
    std::string method_;
    std::string path_;
    const RouteDeclStmt* declaration_;
    std::shared_ptr<Environment> closure_;
    std::string sourceName_;
};

class RouteRegistry final {
  public:
    void addRoute(std::shared_ptr<RouteHandler> route, const SourceSpan& span);
    bool hasRoute(const std::string& method, const std::string& path) const;
    Value render(const std::string& method,
                 const std::string& path,
                 Interpreter& interpreter,
                 const SourceSpan& span) const;
    std::vector<std::string> routePaths() const;
    RouteRegistry clone(
        const std::function<std::shared_ptr<Environment>(const std::shared_ptr<Environment>&)>& cloneEnvironment) const;

  private:
    std::unordered_map<std::string, std::shared_ptr<RouteHandler>> routes_;
    std::vector<std::shared_ptr<RouteHandler>> orderedRoutes_;
};

}  // namespace wevoaweb
