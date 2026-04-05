#pragma once

#include <filesystem>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "cli/cli_handler.h"
#include "utils/file_writer.h"

namespace wevoaweb {

struct BuildResult {
    std::filesystem::path outputRoot;
    std::filesystem::path staticOutputRoot;
    std::vector<std::string> routes;
    std::vector<std::string> staticRoutes;
    std::vector<std::string> views;
    std::vector<std::string> assets;
    std::vector<std::string> packages;
};

class BuildPipeline {
  public:
    explicit BuildPipeline(FileWriter writer = {});

    BuildResult build(const BuildCommandOptions& options, std::istream& input, std::ostream& output) const;

  private:
    void copyDirectoryContents(const std::filesystem::path& source, const std::filesystem::path& target) const;
    std::vector<std::string> enumeratePublicAssets(const std::filesystem::path& directory) const;
    std::vector<std::string> enumeratePackageDirectories(const std::filesystem::path& directory) const;

    FileWriter writer_;
};

}  // namespace wevoaweb
