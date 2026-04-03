#pragma once

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "server/http_types.h"
#include "server/web_app.h"

namespace wevoaweb::server {

// HttpServer owns the socket accept loop and dispatches GET/POST requests to
// registered WevoaWeb routes, returning HTML and static assets to the browser.
class HttpServer {
  public:
    using RequestObserver = std::function<void(const HttpRequest&, const HttpResponse&)>;

    HttpServer(WebApplication& application,
               std::uint16_t port,
               RequestObserver observer = {},
               std::size_t workerCount = 0,
               std::size_t maxQueueSize = 0);

    void run();
    void stop();

  private:
    HttpResponse dispatch(const HttpRequest& request);
    void workerLoop();

    WebApplication& application_;
    std::uint16_t port_;
    std::size_t workerCount_ = 0;
    std::size_t maxQueueSize_ = 0;
    RequestObserver observer_;
    std::atomic<bool> stopRequested_ = false;
    std::mutex queueMutex_;
    std::condition_variable queueReady_;
    std::queue<std::function<void()>> queuedRequests_;
    std::vector<std::thread> workers_;
};

}  // namespace wevoaweb::server
