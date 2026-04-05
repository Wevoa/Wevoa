#include "server/session_store.h"

#include <chrono>
#include <random>

namespace wevoaweb::server {

SessionStore::SessionStore() : cleanupThread_([this]() { cleanupLoop(); }) {}

SessionStore::~SessionStore() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    cleanupWake_.notify_all();
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }
}

void SessionStore::setTimeToLive(std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lock(mutex_);
    ttl_ = ttl.count() > 0 ? ttl : std::chrono::hours(1);
    cleanupWake_.notify_all();
}

void SessionStore::setCleanupInterval(std::chrono::seconds interval) {
    std::lock_guard<std::mutex> lock(mutex_);
    cleanupInterval_ = interval.count() > 0 ? interval : std::chrono::seconds(30);
    cleanupWake_.notify_all();
}

void SessionStore::setMaxSessions(std::size_t maxSessions) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxSessions_ = maxSessions == 0 ? 1 : maxSessions;
    enforceMaxSessionsLocked();
}

std::string SessionStore::ensureSession(const std::optional<std::string>& existingSessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredLocked();

    if (existingSessionId.has_value() && !existingSessionId->empty()) {
        auto [iterator, inserted] = sessions_.try_emplace(*existingSessionId,
                                                          SessionEntry {Value::Object {},
                                                                        std::chrono::steady_clock::now(),
                                                                        std::chrono::steady_clock::now()});
        static_cast<void>(inserted);
        touchLocked(iterator->second);
        enforceMaxSessionsLocked();
        return *existingSessionId;
    }

    while (true) {
        const std::string generatedId = generateSessionId();
        const auto [iterator, inserted] =
            sessions_.try_emplace(generatedId,
                                  SessionEntry {Value::Object {},
                                                std::chrono::steady_clock::now(),
                                                std::chrono::steady_clock::now()});
        if (inserted) {
            touchLocked(iterator->second);
            enforceMaxSessionsLocked();
            return generatedId;
        }
    }
}

Value SessionStore::get(const std::string& sessionId, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredLocked();
    const auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return Value {};
    }

    touchLocked(session->second);

    const auto found = session->second.values.find(key);
    if (found == session->second.values.end()) {
        return Value {};
    }

    return found->second;
}

void SessionStore::set(const std::string& sessionId, const std::string& key, const Value& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredLocked();
    auto& session = sessions_[sessionId];
    touchLocked(session);
    session.values.insert_or_assign(key, value);
}

bool SessionStore::erase(const std::string& sessionId, const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredLocked();
    const auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return false;
    }

    touchLocked(session->second);
    return session->second.values.erase(key) > 0;
}

std::size_t SessionStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredLocked();
    return sessions_.size();
}

void SessionStore::cleanupLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        // FIXED: Using wait_for's boolean return avoids static analysis dead-code warnings
        if (cleanupWake_.wait_for(lock, cleanupInterval_, [this]() { return stopRequested_; })) {
            break;
        }

        purgeExpiredLocked();
        enforceMaxSessionsLocked();
    }
}

void SessionStore::purgeExpiredLocked() const {
    const auto now = std::chrono::steady_clock::now();
    for (auto iterator = sessions_.begin(); iterator != sessions_.end();) {
        if (iterator->second.expiresAt <= now) {
            iterator = sessions_.erase(iterator);
            continue;
        }
        ++iterator;
    }
}

void SessionStore::enforceMaxSessionsLocked() const {
    while (sessions_.size() > maxSessions_) {
        auto oldest = sessions_.begin();
        for (auto iterator = sessions_.begin(); iterator != sessions_.end(); ++iterator) {
            if (iterator->second.lastAccessedAt < oldest->second.lastAccessedAt) {
                oldest = iterator;
            }
        }
        sessions_.erase(oldest);
    }
}

void SessionStore::touchLocked(SessionEntry& session) const {
    const auto now = std::chrono::steady_clock::now();
    session.lastAccessedAt = now;
    session.expiresAt = now + ttl_;
}

std::string SessionStore::generateSessionId() const {
    static constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    thread_local std::mt19937_64 generator(std::random_device {}());
    std::uniform_int_distribution<std::size_t> distribution(0, sizeof(kAlphabet) - 2);

    std::string value;
    value.reserve(32);
    for (int index = 0; index < 32; ++index) {
        value.push_back(kAlphabet[distribution(generator)]);
    }

    return value;
}

}  // namespace wevoaweb::server
