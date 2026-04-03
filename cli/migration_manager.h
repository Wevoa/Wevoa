#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "utils/file_writer.h"

namespace wevoaweb {

struct MigrationCreateResult {
    std::filesystem::path filePath;
};

struct MigrationRunResult {
    std::filesystem::path databasePath;
    std::vector<std::string> appliedMigrations;
    std::vector<std::string> skippedMigrations;
};

class MigrationManager {
  public:
    explicit MigrationManager(FileWriter writer = {});

    MigrationCreateResult createMigration(const std::string& name) const;
    MigrationRunResult runPendingMigrations() const;

  private:
    std::filesystem::path migrationsDirectory() const;
    std::filesystem::path databasePath() const;
    std::string sanitizeMigrationName(const std::string& name) const;
    std::string migrationTimestamp() const;

    FileWriter writer_;
};

}  // namespace wevoaweb
