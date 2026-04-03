#pragma once

#include <atomic>
#include <istream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>

#include "cli/cli_handler.h"
#include "server/http_server.h"
#include "server/web_app.h"
#include "utils/logger.h"
#include "watcher/file_watcher.h"

namespace wevoaweb::server {

class DevServer {
  public:
    DevServer(StartCommandOptions options,
              Logger& logger,
              std::atomic<bool>& interruptRequested,
              std::istream& input,
              std::ostream& output);
    ~DevServer();

    DevServer(const DevServer&) = delete;
    DevServer& operator=(const DevServer&) = delete;

    int run();

  private:
    void requestReload(const std::string& reason);
    void requestShutdown();
    void handleKey(char key);
    void handleFileChange(const FileChangeEvent& event);
    void handleRequest(const HttpRequest& request, const HttpResponse& response);

    void reload(const std::string& reason);
    std::unique_ptr<WebApplication> loadApplication();
    void installApplication(std::unique_ptr<WebApplication> application);
    void stopServer();
    void stopServerLocked();
    void launchServerLocked();
    void reportThreadFailure(const std::string& message);
    std::string consumePendingReloadReason();
    std::string consumeThreadFailure();
    std::string displayPath(const std::filesystem::path& path) const;

    StartCommandOptions options_;
    Logger& logger_;
    std::atomic<bool>& interruptRequested_;
    std::istream& input_;
    std::ostream& output_;

    std::unique_ptr<WebApplication> application_;
    std::unique_ptr<HttpServer> httpServer_;
    std::unique_ptr<FileWatcher> watcher_;
    std::thread serverThread_;

    std::mutex stateMutex_;
    std::mutex reloadMutex_;
    std::mutex threadFailureMutex_;
    std::mutex perfMutex_;

    std::string pendingReloadReason_;
    std::string threadFailureMessage_;

    std::atomic<bool> shutdownRequested_ = false;
    std::atomic<bool> reloadRequested_ = false;
    std::atomic<bool> threadFailed_ = false;
    std::uint64_t totalRequests_ = 0;
    std::uint64_t totalRequestMicros_ = 0;
    std::uint64_t maxRequestMicros_ = 0;
    bool openBrowserOnStart_ = true;
};

}  // namespace wevoaweb::server
