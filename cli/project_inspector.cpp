#include "cli/project_inspector.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

#include "runtime/config_loader.h"
#include "server/web_app.h"
#include "utils/project_layout.h"

namespace wevoaweb {

namespace {

std::size_t countFiles(const std::filesystem::path& directory, const std::string& extension = {}) {
    namespace fs = std::filesystem;

    std::size_t count = 0;
    std::error_code error;
    if (!fs::exists(directory, error) || !fs::is_directory(directory, error) || error) {
        return 0;
    }

    for (fs::recursive_directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            throw std::runtime_error("Unable to inspect directory: " + directory.string());
        }

        if (!iterator->is_regular_file(error) || error) {
            continue;
        }

        if (extension.empty() || iterator->path().extension() == extension) {
            ++count;
        }
    }

    return count;
}

}  // namespace

ProjectInfo ProjectInspector::inspectCurrentProject(const std::string& appDirectory,
                                                    const std::string& viewsDirectory,
                                                    const std::string& publicDirectory) const {
    const auto layout =
        detectSourceProjectLayout(std::filesystem::current_path(), appDirectory, viewsDirectory, publicDirectory);

    ProjectInfo info;
    info.root = layout.root;
    info.configFile = layout.configFile;
    info.appDirectory = layout.appDirectory;
    info.viewsDirectory = layout.viewsDirectory;
    info.publicDirectory = layout.publicDirectory;

    const Value config = loadConfigFile(layout.configFile);
    if (config.isObject()) {
        const auto& object = config.asObject();
        if (const auto found = object.find("env"); found != object.end() && found->second.isString()) {
            info.env = found->second.asString();
        }
        if (const auto found = object.find("port"); found != object.end() && found->second.isInteger()) {
            info.port = found->second.asInteger();
        }
    }

    info.viewCount = countFiles(layout.viewsDirectory, ".wev");
    info.assetCount = countFiles(layout.publicDirectory);
    info.packageCount = countFiles(layout.root / "packages", ".wev");
    info.migrationCount = countFiles(layout.root / "migrations", ".sql");

    std::istringstream input;
    std::ostringstream output;
    server::WebApplication application(layout.appDirectory.string(),
                                       layout.viewsDirectory.string(),
                                       layout.publicDirectory.string(),
                                       input,
                                       output,
                                       false,
                                       config);
    application.loadViews();
    info.routeCount = application.routePaths().size();

    return info;
}

DoctorReport ProjectInspector::doctorCurrentProject(const std::string& appDirectory,
                                                    const std::string& viewsDirectory,
                                                    const std::string& publicDirectory) const {
    DoctorReport report;
    const auto layout =
        detectSourceProjectLayout(std::filesystem::current_path(), appDirectory, viewsDirectory, publicDirectory);

    report.checks.push_back("Found app directory: " + layout.appDirectory.string());
    report.checks.push_back("Found views directory: " + layout.viewsDirectory.string());

    std::error_code error;
    if (!std::filesystem::exists(layout.publicDirectory, error) || error) {
        report.warnings.push_back("Public directory is missing: " + layout.publicDirectory.string());
    } else {
        report.checks.push_back("Found public directory: " + layout.publicDirectory.string());
    }

    if (!std::filesystem::exists(layout.configFile, error) || error) {
        report.warnings.push_back("Config file is missing; defaults will be used.");
    } else {
        report.checks.push_back("Found config file: " + layout.configFile.string());
        static_cast<void>(loadConfigFile(layout.configFile));
        report.checks.push_back("Config file parsed successfully");
    }

    const auto packagesDirectory = layout.root / "packages";
    if (!std::filesystem::exists(packagesDirectory, error) || error) {
        report.warnings.push_back("Packages directory is missing: " + packagesDirectory.string());
    } else {
        report.checks.push_back("Packages detected: " + std::to_string(countFiles(packagesDirectory, ".wev")));
    }

    const auto migrationsDirectory = layout.root / "migrations";
    if (!std::filesystem::exists(migrationsDirectory, error) || error) {
        report.warnings.push_back("Migrations directory is missing: " + migrationsDirectory.string());
    } else {
        report.checks.push_back("Migrations detected: " + std::to_string(countFiles(migrationsDirectory, ".sql")));
    }

    std::istringstream input;
    std::ostringstream output;
    server::WebApplication application(layout.appDirectory.string(),
                                       layout.viewsDirectory.string(),
                                       layout.publicDirectory.string(),
                                       input,
                                       output,
                                       false,
                                       loadConfigFile(layout.configFile));
    application.loadViews();
    report.checks.push_back("Routes validated: " + std::to_string(application.routePaths().size()));
    report.checks.push_back("Templates validated: " + std::to_string(application.viewPaths().size()));

    report.healthy = true;
    return report;
}

}  // namespace wevoaweb
