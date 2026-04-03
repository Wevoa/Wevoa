#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/http_types.h"
#include "server/session_store.h"
#include "interpreter/value.h"
#include "runtime/session.h"
#include "runtime/template_engine.h"

namespace wevoaweb::server {

// WebApplication loads backend source files into one runtime session and
// renders templates from the views directory for the HTTP server.
class WebApplication {
  public:
    struct StaticAsset {
        std::string contentType;
        std::string body;
    };

    WebApplication(std::string scriptsDirectory,
                   std::string viewsDirectory,
                   std::string publicDirectory,
                   std::istream& input,
                   std::ostream& output,
                   bool debugAst = false,
                   Value config = Value(Value::Object {}));

    void loadViews();
    void validateTemplates() const;
    bool hasRoute(const std::string& path, const std::string& method = "GET") const;
    HttpResponse render(const HttpRequest& request);
    std::optional<StaticAsset> loadStaticAsset(const std::string& path) const;
    std::vector<std::string> routePaths() const;
    std::vector<std::string> viewPaths() const;
    Value::Array compiledTemplateManifest() const;
    Value snapshotManifest() const;
    std::size_t workerThreads() const;
    std::size_t maxQueueSize() const;
    std::size_t templateCacheLimit() const;
    std::size_t fragmentCacheLimit() const;
    std::size_t maxSessionCount() const;
    std::int64_t sessionTimeToLiveSeconds() const;
    bool performanceMetricsEnabled() const;
    const std::string& scriptsDirectory() const;
    const std::string& viewsDirectory() const;
    const std::string& publicDirectory() const;

  private:
    struct ScriptSource {
        std::string path;
        std::string contents;
    };

    static std::string contentTypeForPath(const std::filesystem::path& path);
    std::optional<std::filesystem::path> resolveStaticPath(const std::string& requestPath) const;
    std::unique_ptr<RuntimeSession> makeRequestSession() const;
    bool productionMode() const;
    bool secureCookies() const;
    std::string sessionCookieAttributes() const;

    std::string scriptsDirectory_;
    std::string viewsDirectory_;
    std::string publicDirectory_;
    TemplateEngine templateEngine_;
    Value config_;
    std::istream* input_ = nullptr;
    std::ostream* output_ = nullptr;
    bool debugAst_ = false;
    RuntimeSession session_;
    SessionStore sessionStore_;
    std::vector<ScriptSource> scripts_;
    std::vector<std::string> routePaths_;
    std::vector<std::string> viewPaths_;
};

}  // namespace wevoaweb::server
