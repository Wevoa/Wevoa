#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include "interpreter/value.h"

namespace wevoaweb::server {

struct UploadedFile {
    std::string fieldName;
    std::string fileName;
    std::string contentType;
    std::string tempPath;
    std::size_t size = 0;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string version;
    std::vector<std::pair<std::string, std::string>> headers;
    std::unordered_map<std::string, std::string> queryParameters;
    std::unordered_map<std::string, std::string> formParameters;
    std::vector<UploadedFile> files;
    std::optional<wevoaweb::Value> jsonBody;
    std::optional<std::string> jsonError;
    std::string body;
};

struct HttpResponse {
    int statusCode = 200;
    std::string reasonPhrase = "OK";
    std::string contentType = "text/html; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::uint64_t requestMicros = 0;
    std::uint64_t routeMicros = 0;
    std::uint64_t templateMicros = 0;
    std::uint64_t dbMicros = 0;
    std::string debugMessage;

    std::string serialize() const;
};

}  // namespace wevoaweb::server
