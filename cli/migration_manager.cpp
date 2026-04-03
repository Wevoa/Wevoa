#include "cli/migration_manager.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>

#include <sqlite3.h>

#include "runtime/config_loader.h"

namespace wevoaweb {

namespace {

std::string readFileContents(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to read migration file: " + path.string());
    }

    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void ensureSqliteOk(int code, sqlite3* database, const std::string& context) {
    if (code == SQLITE_OK || code == SQLITE_DONE || code == SQLITE_ROW) {
        return;
    }

    throw std::runtime_error(context + ": " + std::string(sqlite3_errmsg(database)));
}

std::string currentTimestampString() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

class SqliteConnection final {
  public:
    explicit SqliteConnection(const std::filesystem::path& path) {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                throw std::runtime_error("Unable to create database directory: " + parent.string());
            }
        }

        if (sqlite3_open(path.string().c_str(), &database_) != SQLITE_OK) {
            const std::string message = database_ != nullptr ? sqlite3_errmsg(database_) : "unknown SQLite error";
            if (database_ != nullptr) {
                sqlite3_close(database_);
                database_ = nullptr;
            }
            throw std::runtime_error("Unable to open SQLite database '" + path.string() + "': " + message);
        }
    }

    ~SqliteConnection() {
        if (database_ != nullptr) {
            sqlite3_close(database_);
        }
    }

    sqlite3* get() const {
        return database_;
    }

  private:
    sqlite3* database_ = nullptr;
};

}  // namespace

MigrationManager::MigrationManager(FileWriter writer) : writer_(std::move(writer)) {}

MigrationCreateResult MigrationManager::createMigration(const std::string& name) const {
    const std::string sanitizedName = sanitizeMigrationName(name);
    if (sanitizedName.empty()) {
        throw std::runtime_error("Migration name must contain letters or digits.");
    }

    const auto directory = migrationsDirectory();
    writer_.createDirectory(directory);

    const auto filePath = directory / (migrationTimestamp() + "_" + sanitizedName + ".sql");
    writer_.writeTextFile(filePath,
                          "-- Migration: " + sanitizedName + "\n"
                          "-- Created by WevoaWeb\n\n"
                          "BEGIN TRANSACTION;\n\n"
                          "-- Write your SQL here.\n\n"
                          "COMMIT;\n");

    return MigrationCreateResult {filePath};
}

MigrationRunResult MigrationManager::runPendingMigrations() const {
    namespace fs = std::filesystem;

    const auto directory = migrationsDirectory();
    writer_.createDirectory(directory);
    writer_.createDirectory(fs::current_path() / "storage");

    MigrationRunResult result;
    result.databasePath = databasePath();

    SqliteConnection connection(result.databasePath);
    ensureSqliteOk(sqlite3_exec(connection.get(),
                                "CREATE TABLE IF NOT EXISTS wevoa_migrations ("
                                "name TEXT PRIMARY KEY,"
                                "executed_at TEXT NOT NULL"
                                ");",
                                nullptr,
                                nullptr,
                                nullptr),
                   connection.get(),
                   "Unable to initialize migration metadata");

    std::set<std::string> executed;
    sqlite3_stmt* selectStatement = nullptr;
    ensureSqliteOk(sqlite3_prepare_v2(connection.get(),
                                      "SELECT name FROM wevoa_migrations ORDER BY name;",
                                      -1,
                                      &selectStatement,
                                      nullptr),
                   connection.get(),
                   "Unable to read executed migrations");
    while (sqlite3_step(selectStatement) == SQLITE_ROW) {
        executed.insert(reinterpret_cast<const char*>(sqlite3_column_text(selectStatement, 0)));
    }
    sqlite3_finalize(selectStatement);

    std::vector<fs::path> migrationFiles;
    std::error_code error;
    if (fs::exists(directory, error) && fs::is_directory(directory, error) && !error) {
        for (fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
             iterator != end;
             iterator.increment(error)) {
            if (error) {
                throw std::runtime_error("Unable to inspect migrations directory: " + directory.string());
            }
            if (iterator->is_regular_file(error) && iterator->path().extension() == ".sql" && !error) {
                migrationFiles.push_back(iterator->path());
            }
        }
    }

    std::sort(migrationFiles.begin(), migrationFiles.end());
    const std::string executedAt = currentTimestampString();

    for (const auto& migrationFile : migrationFiles) {
        const std::string migrationName = migrationFile.filename().string();
        if (executed.find(migrationName) != executed.end()) {
            result.skippedMigrations.push_back(migrationName);
            continue;
        }

        char* errorMessage = nullptr;
        const std::string sql = readFileContents(migrationFile);
        if (sqlite3_exec(connection.get(), sql.c_str(), nullptr, nullptr, &errorMessage) != SQLITE_OK) {
            const std::string message = errorMessage != nullptr ? errorMessage : sqlite3_errmsg(connection.get());
            sqlite3_free(errorMessage);
            throw std::runtime_error("Migration '" + migrationName + "' failed: " + message);
        }

        sqlite3_stmt* insertStatement = nullptr;
        ensureSqliteOk(sqlite3_prepare_v2(connection.get(),
                                          "INSERT INTO wevoa_migrations (name, executed_at) VALUES (?, ?);",
                                          -1,
                                          &insertStatement,
                                          nullptr),
                       connection.get(),
                       "Unable to record executed migration");
        ensureSqliteOk(sqlite3_bind_text(insertStatement, 1, migrationName.c_str(), -1, SQLITE_TRANSIENT),
                       connection.get(),
                       "Unable to bind migration name");
        ensureSqliteOk(sqlite3_bind_text(insertStatement, 2, executedAt.c_str(), -1, SQLITE_TRANSIENT),
                       connection.get(),
                       "Unable to bind migration timestamp");
        ensureSqliteOk(sqlite3_step(insertStatement), connection.get(), "Unable to record executed migration");
        sqlite3_finalize(insertStatement);

        result.appliedMigrations.push_back(migrationName);
    }

    return result;
}

std::filesystem::path MigrationManager::migrationsDirectory() const {
    return std::filesystem::current_path() / "migrations";
}

std::filesystem::path MigrationManager::databasePath() const {
    const Value config = loadConfigFile(std::filesystem::current_path() / "wevoa.config.json");
    if (config.isObject()) {
        const auto& object = config.asObject();
        if (const auto found = object.find("database"); found != object.end()) {
            if (found->second.isString()) {
                return std::filesystem::current_path() / found->second.asString();
            }

            if (found->second.isObject()) {
                const auto& databaseObject = found->second.asObject();
                if (const auto pathFound = databaseObject.find("path");
                    pathFound != databaseObject.end() && pathFound->second.isString()) {
                    return std::filesystem::current_path() / pathFound->second.asString();
                }
            }
        }
    }

    return std::filesystem::current_path() / "storage" / "app.sqlite";
}

std::string MigrationManager::sanitizeMigrationName(const std::string& name) const {
    std::string sanitized;
    sanitized.reserve(name.size());

    bool previousUnderscore = false;
    for (const char ch : name) {
        const auto unsignedChar = static_cast<unsigned char>(ch);
        if (std::isalnum(unsignedChar) != 0) {
            sanitized.push_back(static_cast<char>(std::tolower(unsignedChar)));
            previousUnderscore = false;
            continue;
        }

        if (!previousUnderscore) {
            sanitized.push_back('_');
            previousUnderscore = true;
        }
    }

    while (!sanitized.empty() && sanitized.front() == '_') {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && sanitized.back() == '_') {
        sanitized.pop_back();
    }

    return sanitized;
}

std::string MigrationManager::migrationTimestamp() const {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d%H%M%S");
    return stream.str();
}

}  // namespace wevoaweb
