#include "server/http_server.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <limits>
#include <unordered_map>
#include <utility>

#include "runtime/config_loader.h"
#include "utils/defaults.h"
#include "utils/error.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace wevoaweb::server {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
using SocketLength = int;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

std::string lastSocketErrorMessage() {
    return "WSA error " + std::to_string(WSAGetLastError());
}

int closeNativeSocket(NativeSocket socket) {
    return closesocket(socket);
}

class SocketSystem final {
  public:
    SocketSystem() {
        WSADATA data {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed: " + lastSocketErrorMessage());
        }
    }

    ~SocketSystem() {
        WSACleanup();
    }
};
#else
using NativeSocket = int;
using SocketLength = socklen_t;
constexpr NativeSocket kInvalidSocket = -1;
constexpr int kSocketError = -1;

std::string lastSocketErrorMessage() {
    return std::strerror(errno);
}

int closeNativeSocket(NativeSocket socket) {
    return close(socket);
}

class SocketSystem final {
  public:
    SocketSystem() = default;
};
#endif

#ifdef _WIN32
constexpr int kSocketError = SOCKET_ERROR;
#endif

class Socket final {
  public:
    Socket() = default;
    explicit Socket(NativeSocket socket) : socket_(socket) {}

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : socket_(other.socket_) {
        other.socket_ = kInvalidSocket;
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            reset();
            socket_ = other.socket_;
            other.socket_ = kInvalidSocket;
        }
        return *this;
    }

    ~Socket() {
        reset();
    }

    [[nodiscard]] bool valid() const {
        return socket_ != kInvalidSocket;
    }

    [[nodiscard]] NativeSocket native() const {
        return socket_;
    }

    NativeSocket release() {
        NativeSocket released = socket_;
        socket_ = kInvalidSocket;
        return released;
    }

    void reset(NativeSocket replacement = kInvalidSocket) {
        if (valid()) {
            closeNativeSocket(socket_);
        }
        socket_ = replacement;
    }

  private:
    NativeSocket socket_ = kInvalidSocket;
};

class PortInUseError : public std::runtime_error {
  public:
    explicit PortInUseError(std::uint16_t port, const std::string& message)
        : std::runtime_error(message), port_(port) {}

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

  private:
    std::uint16_t port_;
};

struct ListeningSocketResult {
    Socket socket;
    std::uint16_t boundPort = 0;
    bool portAdjusted = false;
};

Socket createListeningSocket(std::uint16_t port) {
    Socket listenSocket(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listenSocket.valid()) {
        throw std::runtime_error("Unable to create socket: " + lastSocketErrorMessage());
    }

    int reuseAddress = 1;
    if (::setsockopt(listenSocket.native(),
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuseAddress),
                     sizeof(reuseAddress)) < 0) {
        throw std::runtime_error("Unable to configure socket: " + lastSocketErrorMessage());
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(listenSocket.native(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        const std::string message =
            "Unable to bind socket on port " + std::to_string(port) + ": " + lastSocketErrorMessage();
#ifdef _WIN32
        if (WSAGetLastError() == WSAEADDRINUSE) {
            throw PortInUseError(port, message);
        }
        if (WSAGetLastError() == WSAEACCES) {
            throw PortInUseError(port, message);
        }
#else
        if (errno == EADDRINUSE) {
            throw PortInUseError(port, message);
        }
#endif

        throw std::runtime_error(message);
    }

    if (::listen(listenSocket.native(), 16) < 0) {
        throw std::runtime_error("Unable to listen on socket: " + lastSocketErrorMessage());
    }

    return listenSocket;
}

ListeningSocketResult createListeningSocketWithFallback(std::uint16_t preferredPort) {
    std::uint32_t candidatePort = preferredPort;
    std::size_t attempts = 0;

    while (candidatePort <= std::numeric_limits<std::uint16_t>::max() && attempts < kPortBindRetryAttempts) {
        try {
            auto socket = createListeningSocket(static_cast<std::uint16_t>(candidatePort));
            return ListeningSocketResult {
                std::move(socket),
                static_cast<std::uint16_t>(candidatePort),
                static_cast<std::uint16_t>(candidatePort) != preferredPort,
            };
        } catch (const PortInUseError&) {
            ++candidatePort;
            ++attempts;
            continue;
        }
    }

    throw std::runtime_error("Unable to bind socket on port " + std::to_string(preferredPort) +
                             " or the next " + std::to_string(kPortBindRetryAttempts - 1) + " ports.");
}

void configureClientSocket(NativeSocket socket) {
#ifdef _WIN32
    DWORD timeoutMs = 5000;
    ::setsockopt(socket,
                 SOL_SOCKET,
                 SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeoutMs),
                 sizeof(timeoutMs));
#else
    timeval timeout {};
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

std::string trimLineEnd(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

std::string stripUtf8Bom(std::string value) {
    if (value.size() >= 3 && static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB && static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }

    return value;
}

void sendImmediateResponse(NativeSocket socket, const std::string& payload) {
    std::size_t totalSent = 0;
    while (totalSent < payload.size()) {
        const auto remaining = payload.size() - totalSent;
#ifdef _WIN32
        const int sent = ::send(socket, payload.data() + totalSent, static_cast<int>(remaining), 0);
#else
        const int sent = static_cast<int>(::send(socket, payload.data() + totalSent, remaining, 0));
#endif
        if (sent <= 0) {
            throw std::runtime_error("Failed to send HTTP response: " + lastSocketErrorMessage());
        }

        totalSent += static_cast<std::size_t>(sent);
    }
}

std::string readRequest(NativeSocket clientSocket) {
    std::string request;
    std::array<char, 4096> buffer {};
    std::size_t contentLength = 0;
    std::size_t headerEnd = std::string::npos;
    bool expectContinue = false;
    bool continueSent = false;

    while (request.size() < 65536) {
#ifdef _WIN32
        const int received = ::recv(clientSocket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const int received = static_cast<int>(::recv(clientSocket, buffer.data(), buffer.size(), 0));
#endif
        if (received <= 0) {
            break;
        }

        request.append(buffer.data(), static_cast<std::size_t>(received));

        if (headerEnd == std::string::npos) {
            headerEnd = request.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                std::istringstream headerStream(request.substr(0, headerEnd));
                std::string line;
                std::getline(headerStream, line);

                while (std::getline(headerStream, line)) {
                    line = trimLineEnd(line);
                    const auto separator = line.find(':');
                    if (separator == std::string::npos) {
                        continue;
                    }

                    std::string name = line.substr(0, separator);
                    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });

                    if (name == "content-length") {
                        const std::string value = line.substr(separator + 1);
                        contentLength = static_cast<std::size_t>(std::stoul(value));
                    } else if (name == "expect") {
                        std::string value = line.substr(separator + 1);
                        if (!value.empty() && value.front() == ' ') {
                            value.erase(value.begin());
                        }

                        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                            return static_cast<char>(std::tolower(ch));
                        });

                        expectContinue = (value == "100-continue");
                    }
                }
            }
        }

        if (headerEnd != std::string::npos && expectContinue && !continueSent && request.size() == headerEnd + 4) {
            sendImmediateResponse(clientSocket, "HTTP/1.1 100 Continue\r\n\r\n");
            continueSent = true;
        }

        if (headerEnd != std::string::npos && request.size() >= headerEnd + 4 + contentLength) {
            break;
        }
    }

    return request;
}

std::string normalizePath(const std::string& target) {
    const auto queryPos = target.find_first_of("?#");
    std::string path = target.substr(0, queryPos);
    if (path.empty()) {
        path = "/";
    }
    return path;
}

std::string decodeUrlComponent(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            decoded.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
            i += 2;
            continue;
        }

        decoded.push_back(value[i]);
    }

    return decoded;
}

std::unordered_map<std::string, std::string> parseUrlEncoded(const std::string& value) {
    std::unordered_map<std::string, std::string> pairs;
    std::size_t cursor = 0;

    while (cursor <= value.size()) {
        const auto separator = value.find('&', cursor);
        const std::string segment =
            separator == std::string::npos ? value.substr(cursor) : value.substr(cursor, separator - cursor);

        if (!segment.empty()) {
            const auto equals = segment.find('=');
            if (equals == std::string::npos) {
                pairs.insert_or_assign(stripUtf8Bom(decodeUrlComponent(segment)), "");
            } else {
                pairs.insert_or_assign(stripUtf8Bom(decodeUrlComponent(segment.substr(0, equals))),
                                       decodeUrlComponent(segment.substr(equals + 1)));
            }
        }

        if (separator == std::string::npos) {
            break;
        }

        cursor = separator + 1;
    }

    return pairs;
}

std::string getHeaderValue(const std::vector<std::pair<std::string, std::string>>& headers, const std::string& name) {
    for (const auto& [headerName, headerValue] : headers) {
        if (headerName == name) {
            return headerValue;
        }
    }

    return "";
}

std::optional<std::string> extractMultipartBoundary(const std::string& contentType) {
    const auto marker = contentType.find("boundary=");
    if (marker == std::string::npos) {
        return std::nullopt;
    }

    std::string boundary = contentType.substr(marker + 9);
    const auto separator = boundary.find(';');
    if (separator != std::string::npos) {
        boundary = boundary.substr(0, separator);
    }

    boundary = trimLineEnd(boundary);
    if (!boundary.empty() && boundary.front() == '"') {
        boundary.erase(boundary.begin());
    }
    if (!boundary.empty() && boundary.back() == '"') {
        boundary.pop_back();
    }

    return boundary.empty() ? std::nullopt : std::make_optional(boundary);
}

std::unordered_map<std::string, std::string> parseDispositionParameters(const std::string& headerValue) {
    std::unordered_map<std::string, std::string> parameters;
    std::stringstream stream(headerValue);
    std::string segment;
    bool first = true;

    while (std::getline(stream, segment, ';')) {
        if (first) {
            first = false;
            continue;
        }

        auto separator = segment.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        std::string name = segment.substr(0, separator);
        std::string value = segment.substr(separator + 1);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())) != 0) {
            name.erase(name.begin());
        }
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())) != 0) {
            name.pop_back();
        }
        if (!value.empty() && value.front() == '"') {
            value.erase(value.begin());
        }
        if (!value.empty() && value.back() == '"') {
            value.pop_back();
        }

        parameters.insert_or_assign(name, value);
    }

    return parameters;
}

std::string sanitizeUploadName(const std::string& fileName) {
    std::string sanitized;
    sanitized.reserve(fileName.size());
    for (const char ch : fileName) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == '-' || ch == '_') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }

    return sanitized.empty() ? "upload.bin" : sanitized;
}

std::filesystem::path writeUploadFile(const std::string& fileName, const std::string& contents) {
    namespace fs = std::filesystem;

    std::error_code error;
    fs::path uploadsDirectory = fs::current_path() / "storage" / "uploads";
    fs::create_directories(uploadsDirectory, error);
    if (error) {
        throw std::runtime_error("Unable to prepare uploads directory: " + uploadsDirectory.string());
    }

    thread_local std::mt19937_64 generator(std::random_device {}());
    std::uniform_int_distribution<unsigned long long> distribution;
    const auto filePath =
        uploadsDirectory / (std::to_string(distribution(generator)) + "_" + sanitizeUploadName(fileName));

    std::ofstream stream(filePath, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to write uploaded file: " + filePath.string());
    }

    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        throw std::runtime_error("Unable to finish uploaded file: " + filePath.string());
    }

    return filePath;
}

void parseMultipartFormData(HttpRequest& request, const std::string& boundary) {
    const std::string delimiter = "--" + boundary;
    std::size_t cursor = 0;

    while (true) {
        const auto boundaryStart = request.body.find(delimiter, cursor);
        if (boundaryStart == std::string::npos) {
            break;
        }

        cursor = boundaryStart + delimiter.size();
        if (cursor + 1 < request.body.size() && request.body.compare(cursor, 2, "--") == 0) {
            break;
        }

        if (request.body.compare(cursor, 2, "\r\n") == 0) {
            cursor += 2;
        }

        const auto headerEnd = request.body.find("\r\n\r\n", cursor);
        if (headerEnd == std::string::npos) {
            break;
        }

        const std::string headerBlock = request.body.substr(cursor, headerEnd - cursor);
        const auto nextBoundary = request.body.find(delimiter, headerEnd + 4);
        if (nextBoundary == std::string::npos) {
            break;
        }

        std::string partBody = request.body.substr(headerEnd + 4, nextBoundary - (headerEnd + 4));
        if (partBody.size() >= 2 && partBody.substr(partBody.size() - 2) == "\r\n") {
            partBody.erase(partBody.size() - 2);
        }
        cursor = nextBoundary;

        std::unordered_map<std::string, std::string> partHeaders;
        std::istringstream headerStream(headerBlock);
        std::string line;
        while (std::getline(headerStream, line)) {
            line = trimLineEnd(line);
            const auto separator = line.find(':');
            if (separator == std::string::npos) {
                continue;
            }

            std::string name = line.substr(0, separator);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            std::string value = line.substr(separator + 1);
            if (!value.empty() && value.front() == ' ') {
                value.erase(value.begin());
            }
            partHeaders.insert_or_assign(std::move(name), std::move(value));
        }

        const auto disposition = partHeaders.find("content-disposition");
        if (disposition == partHeaders.end()) {
            continue;
        }

        const auto parameters = parseDispositionParameters(disposition->second);
        const auto fieldName = parameters.find("name");
        if (fieldName == parameters.end()) {
            continue;
        }

        const auto fileName = parameters.find("filename");
        if (fileName == parameters.end() || fileName->second.empty()) {
            request.formParameters.insert_or_assign(stripUtf8Bom(fieldName->second), partBody);
            continue;
        }

        UploadedFile file;
        file.fieldName = fieldName->second;
        file.fileName = fileName->second;
        if (const auto contentType = partHeaders.find("content-type"); contentType != partHeaders.end()) {
            file.contentType = contentType->second;
        }
        file.size = partBody.size();
        file.tempPath = writeUploadFile(file.fileName, partBody).string();
        request.files.push_back(std::move(file));
    }
}

HttpRequest parseRequest(const std::string& rawRequest) {
    const auto headerEnd = rawRequest.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        throw std::runtime_error("Malformed HTTP request.");
    }

    std::istringstream stream(rawRequest.substr(0, headerEnd));
    std::string requestLine;
    if (!std::getline(stream, requestLine)) {
        throw std::runtime_error("Empty HTTP request.");
    }

    requestLine = trimLineEnd(requestLine);

    std::istringstream requestLineStream(requestLine);
    HttpRequest request;
    if (!(requestLineStream >> request.method >> request.target >> request.version)) {
        throw std::runtime_error("Malformed HTTP request line.");
    }

    std::transform(request.method.begin(), request.method.end(), request.method.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    request.path = normalizePath(request.target);
    request.body = stripUtf8Bom(rawRequest.substr(headerEnd + 4));

    std::string headerLine;
    while (std::getline(stream, headerLine)) {
        headerLine = trimLineEnd(headerLine);
        if (headerLine.empty()) {
            continue;
        }

        const auto separator = headerLine.find(':');
        if (separator == std::string::npos) {
            throw std::runtime_error("Malformed HTTP header.");
        }

        std::string name = headerLine.substr(0, separator);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        std::string value = headerLine.substr(separator + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }

        request.headers.emplace_back(std::move(name), std::move(value));
    }

    const auto queryPos = request.target.find('?');
    if (queryPos != std::string::npos) {
        const auto fragmentPos = request.target.find('#', queryPos);
        const auto queryString = request.target.substr(queryPos + 1, fragmentPos - queryPos - 1);
        request.queryParameters = parseUrlEncoded(queryString);
    }

    const std::string contentType = getHeaderValue(request.headers, "content-type");
    if (!request.body.empty() && contentType.find("application/x-www-form-urlencoded") != std::string::npos) {
        request.formParameters = parseUrlEncoded(request.body);
    }
    if (!request.body.empty() && contentType.find("multipart/form-data") != std::string::npos) {
        const auto boundary = extractMultipartBoundary(contentType);
        if (!boundary.has_value()) {
            throw std::runtime_error("multipart/form-data request is missing a boundary.");
        }
        parseMultipartFormData(request, *boundary);
    }
    if (contentType.find("application/json") != std::string::npos && !request.body.empty()) {
        try {
            request.jsonBody = parseJsonString(request.body);
        } catch (const std::exception& error) {
            request.jsonError = error.what();
        }
    }

    return request;
}

void sendAll(NativeSocket socket, const std::string& payload) {
    sendImmediateResponse(socket, payload);
}

}  // namespace

HttpServer::HttpServer(WebApplication& application,
                       std::uint16_t port,
                       RequestObserver observer,
                       std::size_t workerCount,
                       std::size_t maxQueueSize)
    : application_(application),
      port_(port),
      workerCount_(workerCount == 0 ? 4 : workerCount),
      maxQueueSize_(maxQueueSize == 0 ? workerCount_ * 16 : maxQueueSize),
      observer_(std::move(observer)) {}

void HttpServer::run() {
    stopRequested_ = false;
    {
        std::lock_guard<std::mutex> lock(startupMutex_);
        startupComplete_ = false;
        startupErrorMessage_.clear();
        portAdjusted_ = false;
        boundPort_ = 0;
    }

    Socket listenSocket;

    try {
        SocketSystem socketSystem;
        auto listeningSocket = createListeningSocketWithFallback(port_);
        port_ = listeningSocket.boundPort;
        markStartupSuccess(listeningSocket.boundPort, listeningSocket.portAdjusted);
        listenSocket = std::move(listeningSocket.socket);

        workers_.clear();
        workers_.reserve(workerCount_);
        for (std::size_t index = 0; index < workerCount_; ++index) {
            workers_.emplace_back([this]() { workerLoop(); });
        }

        while (!stopRequested_) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listenSocket.native(), &readSet);

            timeval timeout {};
            timeout.tv_sec = 0;
            timeout.tv_usec = 250000;

#ifdef _WIN32
            const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
#else
            const int ready = ::select(listenSocket.native() + 1, &readSet, nullptr, nullptr, &timeout);
#endif

            if (stopRequested_) {
                break;
            }

            if (ready == 0) {
                continue;
            }

            if (ready == kSocketError) {
#ifndef _WIN32
                if (errno == EINTR) {
                    continue;
                }
#endif
                throw std::runtime_error("Socket wait failed: " + lastSocketErrorMessage());
            }

            sockaddr_in clientAddress {};
            SocketLength addressLength = sizeof(clientAddress);
            Socket clientSocket(::accept(listenSocket.native(),
                                         reinterpret_cast<sockaddr*>(&clientAddress),
                                         &addressLength));

            if (!clientSocket.valid()) {
                if (stopRequested_) {
                    break;
                }
                throw std::runtime_error("Unable to accept client connection: " + lastSocketErrorMessage());
            }

            configureClientSocket(clientSocket.native());

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (queuedRequests_.size() >= maxQueueSize_) {
                    HttpRequest overflowRequest;
                    overflowRequest.method = "QUEUE";
                    overflowRequest.path = "<overflow>";

                    HttpResponse overflowResponse {
                        503,
                        "Service Unavailable",
                        "text/html; charset=utf-8",
                        "<h1>503 Service Unavailable</h1><p>The WevoaWeb request queue is full.</p>",
                        {},
                        0,
                        0,
                        0,
                        0,
                        "",
                    };
                    overflowResponse.debugMessage = "[WARN] queue overflow";
                    sendAll(clientSocket.native(), overflowResponse.serialize());
                    if (observer_) {
                        observer_(overflowRequest, overflowResponse);
                    }
                    continue;
                }
            }

            NativeSocket queuedSocket = clientSocket.release();
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                queuedRequests_.push([this, queuedSocket]() mutable {
                    Socket clientSocket(queuedSocket);
                    HttpResponse response;
                    HttpRequest request;

                    try {
                        const std::string rawRequest = readRequest(clientSocket.native());
                        if (!rawRequest.empty()) {
                            request = parseRequest(rawRequest);
                            response = dispatch(request);
                            sendAll(clientSocket.native(), response.serialize());
                            // FIXED: Removed redundant hasRequest constraint.
                            // Successful parseRequest logically guarantees we have a request.
                            if (observer_) {
                                observer_(request, response);
                            }
                        }
                    } catch (const WevoaError& error) {
                        response.statusCode = 500;
                        response.reasonPhrase = "Internal Server Error";
                        response.body = "<h1>500 Internal Server Error</h1><p>" + std::string(error.what()) + "</p>";
                        try {
                            sendAll(clientSocket.native(), response.serialize());
                        } catch (...) {
                        }
                    } catch (const std::exception& error) {
                        response.statusCode = 400;
                        response.reasonPhrase = "Bad Request";
                        response.body = "<h1>400 Bad Request</h1><p>" + std::string(error.what()) + "</p>";
                        try {
                            sendAll(clientSocket.native(), response.serialize());
                        } catch (...) {
                        }
                    }
                });
            }
            queueReady_.notify_one();
        }
    } catch (const std::exception& error) {
        if (!startupComplete_) {
            markStartupFailure(error.what());
        }
        stopRequested_ = true;
        queueReady_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
        throw;
    } catch (...) {
        if (!startupComplete_) {
            markStartupFailure("HTTP server failed to start.");
        }
        stopRequested_ = true;
        queueReady_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
        throw;
    }

    stopRequested_ = true;
    queueReady_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void HttpServer::stop() {
    stopRequested_ = true;
    queueReady_.notify_all();
}

bool HttpServer::waitForStartup(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(startupMutex_);
    return startupReady_.wait_for(lock, timeout, [this]() { return startupComplete_; });
}

std::uint16_t HttpServer::boundPort() const {
    std::lock_guard<std::mutex> lock(startupMutex_);
    return boundPort_;
}

bool HttpServer::portAdjusted() const {
    std::lock_guard<std::mutex> lock(startupMutex_);
    return portAdjusted_;
}

std::string HttpServer::startupError() const {
    std::lock_guard<std::mutex> lock(startupMutex_);
    return startupErrorMessage_;
}

void HttpServer::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueReady_.wait(lock, [this]() { return stopRequested_ || !queuedRequests_.empty(); });
            if (queuedRequests_.empty()) {
                if (stopRequested_) {
                    return;
                }
                continue;
            }

            task = std::move(queuedRequests_.front());
            queuedRequests_.pop();
        }

        task();
    }
}

void HttpServer::markStartupSuccess(std::uint16_t boundPort, bool portAdjusted) {
    {
        std::lock_guard<std::mutex> lock(startupMutex_);
        startupComplete_ = true;
        startupErrorMessage_.clear();
        portAdjusted_ = portAdjusted;
        boundPort_ = boundPort;
    }
    startupReady_.notify_all();
}

void HttpServer::markStartupFailure(const std::string& errorMessage) {
    {
        std::lock_guard<std::mutex> lock(startupMutex_);
        startupComplete_ = true;
        startupErrorMessage_ = errorMessage;
        portAdjusted_ = false;
        boundPort_ = 0;
    }
    startupReady_.notify_all();
}

HttpResponse HttpServer::dispatch(const HttpRequest& request) {
    static const std::string allowedMethods = "GET, POST, PUT, PATCH, DELETE";
    if (request.method != "GET" && request.method != "POST" && request.method != "PUT" &&
        request.method != "PATCH" && request.method != "DELETE") {
        return HttpResponse {
            405,
            "Method Not Allowed",
            "text/html; charset=utf-8",
            "<h1>405 Method Not Allowed</h1><p>Supported methods: " + allowedMethods + ".</p>",
            {{"Allow", allowedMethods}},
            0,
            0,
            0,
            0,
            "",
        };
    }

    if (request.method == "GET") {
        if (const auto asset = application_.loadStaticAsset(request.path); asset.has_value()) {
            return HttpResponse {200, "OK", asset->contentType, asset->body, {}, 0, 0, 0, 0, ""};
        }
    }

    if (!application_.hasRoute(request.path, request.method)) {
        return HttpResponse {
            404,
            "Not Found",
            "text/html; charset=utf-8",
            "<h1>404 Not Found</h1><p>No WevoaWeb route matched " + request.method + " " + request.path + ".</p>",
            {},
            0,
            0,
            0,
            0,
            "",
        };
    }

    try {
        return application_.render(request);
    } catch (const std::exception& error) {
        HttpResponse response {
            500,
            "Internal Server Error",
            "text/html; charset=utf-8",
            "<h1>500 Internal Server Error</h1><p>" + std::string(error.what()) + "</p>",
            {},
            0,
            0,
            0,
            0,
            "",
        };
        response.debugMessage = error.what();
        return response;
    }
}

}  // namespace wevoaweb::server
