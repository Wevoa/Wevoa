#pragma once

#include <filesystem>
#include <string>

#include "utils/file_writer.h"

namespace wevoaweb {

struct PackageInstallResult {
    std::string packageName;
    std::filesystem::path destination;
    bool createdPlaceholder = false;
};

class PackageManager {
  public:
    explicit PackageManager(FileWriter writer = {});

    PackageInstallResult install(const std::string& packageSpec) const;

  private:
    std::string packageNameFromSpec(const std::string& packageSpec) const;
    void copyDirectoryContents(const std::filesystem::path& source, const std::filesystem::path& destination) const;
    void updateProjectConfig(const std::string& packageName) const;

    FileWriter writer_;
};

}  // namespace wevoaweb
