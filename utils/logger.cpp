#include "utils/logger.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace wevoaweb {

namespace {

constexpr const char* kColorReset = "\x1b[0m";
constexpr const char* kColorBlue = "\x1b[36m";
constexpr const char* kColorGreen = "\x1b[32m";
constexpr const char* kColorYellow = "\x1b[33m";
constexpr const char* kColorRed = "\x1b[31m";
constexpr const char* kColorWhite = "\x1b[37m";
constexpr const char* kColorMagenta = "\x1b[35m";

}  // namespace

Logger::Logger(std::ostream& stream) : stream_(stream) {}

void Logger::wevoa(const std::string& message) {
    log(LogLevel::Wevoa, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::Warn, message);
}

void Logger::perf(const std::string& message) {
    log(LogLevel::Perf, message);
}

void Logger::route(const std::string& message) {
    log(LogLevel::Route, message);
}

void Logger::build(const std::string& message) {
    log(LogLevel::Build, message);
}

void Logger::watch(const std::string& message) {
    log(LogLevel::Watch, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::success(const std::string& message) {
    log(LogLevel::Success, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (useColor()) {
        stream_ << color(level);
    }

    stream_ << '[' << tag(level) << "] " << message;

    if (useColor()) {
        stream_ << kColorReset;
    }

    stream_ << '\n';
    stream_.flush();
}

const char* Logger::tag(LogLevel level) const {
    switch (level) {
        case LogLevel::Wevoa:
            return "Wevoa";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Perf:
            return "PERF";
        case LogLevel::Route:
            return "ROUTE";
        case LogLevel::Build:
            return "BUILD";
        case LogLevel::Watch:
            return "WATCH";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Success:
            return "OK";
    }

    return "LOG";
}

const char* Logger::color(LogLevel level) const {
    switch (level) {
        case LogLevel::Wevoa:
            return kColorBlue;
        case LogLevel::Info:
            return kColorWhite;
        case LogLevel::Warn:
            return kColorYellow;
        case LogLevel::Perf:
            return kColorMagenta;
        case LogLevel::Route:
            return kColorBlue;
        case LogLevel::Build:
            return kColorMagenta;
        case LogLevel::Watch:
            return kColorYellow;
        case LogLevel::Error:
            return kColorRed;
        case LogLevel::Success:
            return kColorGreen;
    }

    return kColorReset;
}

bool Logger::useColor() const {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

}  // namespace wevoaweb
