#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "interpreter/value.h"

namespace wevoaweb::server {

class SessionStore {
  public:
    SessionStore();
    ~SessionStore();

    SessionStore(const SessionStore&) = delete;
    SessionStore& operator=(const SessionStore&) = delete;

    void setTimeToLive(std::chrono::seconds ttl);
    void setCleanupInterval(std::chrono::seconds interval);
    void setMaxSessions(std::size_t maxSessions);
    std::string ensureSession(const std::optional<std::string>& existingSessionId);
    Value get(const std::string& sessionId, const std::string& key) const;
    void set(const std::string& sessionId, const std::string& key, const Value& value);
    bool erase(const std::string& sessionId, const std::string& key);
    std::size_t size() const;

  private:
    struct SessionEntry {
        Value::Object values;
        std::chrono::steady_clock::time_point expiresAt;
        std::chrono::steady_clock::time_point lastAccessedAt;
    };

    void cleanupLoop();
    void purgeExpiredLocked() const;
    void enforceMaxSessionsLocked() const;
    void touchLocked(SessionEntry& session) const;
    std::string generateSessionId() const;

    mutable std::mutex mutex_;
    mutable std::condition_variable cleanupWake_;
    mutable std::unordered_map<std::string, SessionEntry> sessions_;
    mutable std::chrono::seconds ttl_ = std::chrono::hours(1);
    mutable std::chrono::seconds cleanupInterval_ = std::chrono::seconds(30);
    mutable std::size_t maxSessions_ = 4096;
    std::thread cleanupThread_;
    bool stopRequested_ = false;
};

}  // namespace wevoaweb::server
