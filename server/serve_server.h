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

namespace wevoaweb::server {

class ServeServer {
  public:
    ServeServer(ServeCommandOptions options,
                Logger& logger,
                std::atomic<bool>& interruptRequested,
                std::istream& input,
                std::ostream& output);
    ~ServeServer();

    ServeServer(const ServeServer&) = delete;
    ServeServer& operator=(const ServeServer&) = delete;

    int run();

  private:
    std::unique_ptr<WebApplication> loadApplication();
    void handleRequest(const HttpRequest& request, const HttpResponse& response);
    void stopServer();
    void stopServerLocked();
    void launchServerLocked();
    void reportThreadFailure(const std::string& message);
    std::string consumeThreadFailure();

    ServeCommandOptions options_;
    Logger& logger_;
    std::atomic<bool>& interruptRequested_;
    std::istream& input_;
    std::ostream& output_;

    std::unique_ptr<WebApplication> application_;
    std::unique_ptr<HttpServer> httpServer_;
    std::thread serverThread_;
    std::mutex stateMutex_;
    std::mutex threadFailureMutex_;
    std::mutex perfMutex_;
    std::string threadFailureMessage_;
    std::atomic<bool> shutdownRequested_ = false;
    std::atomic<bool> threadFailed_ = false;
    std::uint64_t totalRequests_ = 0;
    std::uint64_t totalRequestMicros_ = 0;
    std::uint64_t maxRequestMicros_ = 0;
};

}  // namespace wevoaweb::server
