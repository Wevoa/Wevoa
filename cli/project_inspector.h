#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wevoaweb {

struct ProjectInfo {
    std::filesystem::path root;
    std::filesystem::path configFile;
    std::filesystem::path appDirectory;
    std::filesystem::path viewsDirectory;
    std::filesystem::path publicDirectory;
    std::string env = "dev";
    std::optional<std::int64_t> port;
    std::size_t routeCount = 0;
    std::size_t viewCount = 0;
    std::size_t assetCount = 0;
    std::size_t packageCount = 0;
    std::size_t migrationCount = 0;
};

struct DoctorReport {
    bool healthy = true;
    std::vector<std::string> checks;
    std::vector<std::string> warnings;
};

class ProjectInspector {
  public:
    ProjectInfo inspectCurrentProject(const std::string& appDirectory = "app",
                                      const std::string& viewsDirectory = "views",
                                      const std::string& publicDirectory = "public") const;
    DoctorReport doctorCurrentProject(const std::string& appDirectory = "app",
                                      const std::string& viewsDirectory = "views",
                                      const std::string& publicDirectory = "public") const;
};

}  // namespace wevoaweb
