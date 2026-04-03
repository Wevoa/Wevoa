#include "server/serve_server.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>

#include "runtime/config_loader.h"
#include "utils/project_layout.h"
#include "utils/version.h"

namespace wevoaweb::server {

namespace {

constexpr auto kControlLoopInterval = std::chrono::milliseconds(50);
constexpr auto kStartupProbeDelay = std::chrono::milliseconds(100);

std::optional<std::uint16_t> portFromConfig(const Value& config) {
    if (!config.isObject()) {
        return std::nullopt;
    }

    const auto& object = config.asObject();
    const auto found = object.find("port");
    if (found == object.end()) {
        return std::nullopt;
    }

    if (!found->second.isInteger()) {
        throw std::runtime_error("Config field 'port' must be an integer.");
    }

    const auto configuredPort = found->second.asInteger();
    if (configuredPort < 1 || configuredPort > static_cast<std::int64_t>(std::numeric_limits<std::uint16_t>::max())) {
        throw std::runtime_error("Config field 'port' is out of range.");
    }

    return static_cast<std::uint16_t>(configuredPort);
}

Value withProductionEnvironment(Value config) {
    if (!config.isObject()) {
        config = Value(Value::Object {});
    }

    config.asObject().insert_or_assign("env", Value("production"));
    return config;
}

void logApplicationSummary(Logger& logger, const WebApplication& application) {
    const auto routes = application.routePaths();
    const auto views = application.viewPaths();
    logger.info("Loaded routes: " + std::to_string(routes.size()));
    logger.info("Loaded views: " + std::to_string(views.size()));
    logger.info("Worker threads: " + std::to_string(application.workerThreads()));
    logger.info("Max queue size: " + std::to_string(application.maxQueueSize()));
    logger.info("Template cache: " + std::to_string(application.templateCacheLimit()));
    logger.info("Fragment cache: " + std::to_string(application.fragmentCacheLimit()));
    logger.info("Session TTL: " + std::to_string(application.sessionTimeToLiveSeconds()) + "s");
    logger.info("Max sessions: " + std::to_string(application.maxSessionCount()));
    for (const auto& route : routes) {
        logger.route("Serving " + route);
    }
}

void logPerformanceSummary(Logger& logger, std::uint64_t totalRequests, std::uint64_t totalMicros, std::uint64_t maxMicros) {
    if (totalRequests == 0) {
        return;
    }

    const double averageMs = static_cast<double>(totalMicros) / static_cast<double>(totalRequests) / 1000.0;
    const double maxMs = static_cast<double>(maxMicros) / 1000.0;
    logger.perf("avg: " + std::to_string(averageMs) + "ms | max: " + std::to_string(maxMs) +
                "ms | requests: " + std::to_string(totalRequests));
}

std::string formatMetrics(const HttpResponse& response) {
    return " [" + std::to_string(response.requestMicros / 1000.0) + "ms total, " +
           std::to_string(response.routeMicros / 1000.0) + "ms route, " +
           std::to_string(response.templateMicros / 1000.0) + "ms templates, " +
           std::to_string(response.dbMicros / 1000.0) + "ms db]";
}

}  // namespace

ServeServer::ServeServer(ServeCommandOptions options,
                         Logger& logger,
                         std::atomic<bool>& interruptRequested,
                         std::istream& input,
                         std::ostream& output)
    : options_(std::move(options)),
      logger_(logger),
      interruptRequested_(interruptRequested),
      input_(input),
      output_(output) {}

ServeServer::~ServeServer() {
    stopServer();
}

int ServeServer::run() {
    logger_.wevoa("Starting production server...");
    logger_.wevoa(std::string(kWevoaRuntimeName) + " v" + kWevoaVersion);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        application_ = loadApplication();
        launchServerLocked();
    }

    logger_.wevoa("Serving built app");
    logger_.wevoa("Running at: http://localhost:" + std::to_string(options_.port));
    logger_.info("Mode: production");

    while (!shutdownRequested_ && !interruptRequested_) {
        if (threadFailed_.exchange(false)) {
            logger_.error(consumeThreadFailure());
            shutdownRequested_ = true;
        }

        std::this_thread::sleep_for(kControlLoopInterval);
    }

    logger_.wevoa("Shutting down...");
    if (application_ != nullptr && application_->performanceMetricsEnabled()) {
        std::lock_guard<std::mutex> lock(perfMutex_);
        logPerformanceSummary(logger_, totalRequests_, totalRequestMicros_, maxRequestMicros_);
    }
    stopServer();
    logger_.wevoa("Server stopped");
    return 0;
}

std::unique_ptr<WebApplication> ServeServer::loadApplication() {
    const auto layout = detectBuiltProjectLayout(std::filesystem::current_path(), options_.buildDirectory);
    Value config = withProductionEnvironment(loadConfigFile(layout.configFile));

    if (!options_.portSpecified) {
        if (const auto configuredPort = portFromConfig(config); configuredPort.has_value()) {
            options_.port = *configuredPort;
        }
    }

    auto application = std::make_unique<WebApplication>(layout.appDirectory.string(),
                                                        layout.viewsDirectory.string(),
                                                        layout.publicDirectory.string(),
                                                        input_,
                                                        output_,
                                                        false,
                                                        config);
    application->loadViews();
    return application;
}

void ServeServer::handleRequest(const HttpRequest& request, const HttpResponse& response) {
    const std::string timing = application_ != nullptr && application_->performanceMetricsEnabled()
                                   ? formatMetrics(response)
                                   : "";
    if (application_ != nullptr && application_->performanceMetricsEnabled() && request.method != "QUEUE") {
        std::lock_guard<std::mutex> lock(perfMutex_);
        ++totalRequests_;
        totalRequestMicros_ += response.requestMicros;
        maxRequestMicros_ = std::max(maxRequestMicros_, response.requestMicros);
    }
    if (response.statusCode == 503) {
        logger_.warn("queue overflow" + timing);
        return;
    }
    if (response.statusCode == 404) {
        logger_.error("Route not found: " + request.method + " " + request.path + timing);
        return;
    }

    if (response.statusCode >= 500) {
        std::string message = "Request failed: " + request.method + " " + request.path + " -> " +
                              std::to_string(response.statusCode) + timing;
        if (!response.debugMessage.empty()) {
            message += "\n" + response.debugMessage;
        }
        logger_.error(message);
        return;
    }

    logger_.route(request.method + " " + request.path + " -> " + std::to_string(response.statusCode) + timing);
}

void ServeServer::stopServer() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    stopServerLocked();
}

void ServeServer::stopServerLocked() {
    if (httpServer_) {
        httpServer_->stop();
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    httpServer_.reset();
    application_.reset();
}

void ServeServer::launchServerLocked() {
    threadFailed_ = false;
    {
        std::lock_guard<std::mutex> lock(threadFailureMutex_);
        threadFailureMessage_.clear();
    }

    httpServer_ = std::make_unique<HttpServer>(
        *application_,
        options_.port,
        [this](const HttpRequest& request, const HttpResponse& response) { handleRequest(request, response); },
        application_->workerThreads(),
        application_->maxQueueSize());

    HttpServer* server = httpServer_.get();
    serverThread_ = std::thread([this, server]() {
        try {
            server->run();
        } catch (const std::exception& error) {
            if (!shutdownRequested_) {
                reportThreadFailure(error.what());
            }
        }
    });

    std::this_thread::sleep_for(kStartupProbeDelay);
    if (threadFailed_.exchange(false)) {
        const std::string failure = consumeThreadFailure();
        stopServerLocked();
        throw std::runtime_error(failure);
    }

    logApplicationSummary(logger_, *application_);
}

void ServeServer::reportThreadFailure(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(threadFailureMutex_);
        threadFailureMessage_ = message;
    }
    threadFailed_ = true;
}

std::string ServeServer::consumeThreadFailure() {
    std::lock_guard<std::mutex> lock(threadFailureMutex_);
    std::string message = threadFailureMessage_;
    threadFailureMessage_.clear();
    return message;
}

}  // namespace wevoaweb::server
