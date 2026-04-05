#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

#include "cli/build_pipeline.h"
#include "cli/cli_handler.h"
#include "cli/migration_manager.h"
#include "cli/package_manager.h"
#include "cli/project_inspector.h"
#include "cli/project_creator.h"
#include "server/dev_server.h"
#include "server/serve_server.h"
#include "utils/logger.h"
#include "utils/version.h"

namespace {

std::atomic<bool>* gInterruptRequested = nullptr;

void handleInterruptSignal(int /*signalNumber*/) {
    if (gInterruptRequested != nullptr) {
        gInterruptRequested->store(true);
    }
}

}  // namespace

int main(int argc, char** argv) {
    wevoaweb::CLIHandler cli;
    wevoaweb::Logger logger(std::cout);

    try {
        const auto command = cli.parse(argc, argv);

        switch (command.type) {
            case wevoaweb::CLIHandler::CommandType::Help:
                cli.printHelp(argc > 0 ? argv[0] : "wevoa");
                return 0;

            case wevoaweb::CLIHandler::CommandType::Create: {
                const auto runtimeRoot =
                    argc > 0 ? std::filesystem::absolute(argv[0]).parent_path() : std::filesystem::current_path();
                wevoaweb::ProjectCreator creator(runtimeRoot);
                logger.wevoa("Creating project using template: " + command.templateName);
                const auto projectPath = creator.createProject(command.templateName, command.projectName);
                logger.wevoa("Project created: " + projectPath.filename().string());
                logger.wevoa("Run:");
                std::cout << "cd " << projectPath.filename().string() << '\n';
                std::cout << "wevoa start\n";
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::Build: {
                wevoaweb::BuildPipeline pipeline;
                const auto result = pipeline.build(command.buildOptions, std::cin, std::cout);
                logger.success("BUILD SUCCESS");
                logger.info("Output: " + result.outputRoot.string());
                logger.build("Routes: " + std::to_string(result.routes.size()));
                logger.build("Views: " + std::to_string(result.views.size()));
                logger.build("Assets: " + std::to_string(result.assets.size()));
                logger.build("Packages: " + std::to_string(result.packages.size()));
                logger.build("Snapshot: " + (result.outputRoot / "wevoa.snapshot.json").string());
                logger.build("Templates: " + (result.outputRoot / "wevoa.templates.json").string());
                if (command.buildOptions.staticExport) {
                    logger.build("Static output: " + result.staticOutputRoot.string());
                    logger.build("Static pages: " + std::to_string(result.staticRoutes.size()));
                }
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::Migrate: {
                wevoaweb::MigrationManager manager;
                const auto result = manager.runPendingMigrations();
                logger.success("MIGRATIONS COMPLETE");
                logger.info("Database: " + result.databasePath.string());
                logger.build("Applied: " + std::to_string(result.appliedMigrations.size()));
                for (const auto& migration : result.appliedMigrations) {
                    logger.build("Applied " + migration);
                }
                logger.info("Skipped: " + std::to_string(result.skippedMigrations.size()));
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::MakeMigration: {
                wevoaweb::MigrationManager manager;
                const auto result = manager.createMigration(command.migrationName);
                logger.success("Migration created");
                logger.info(result.filePath.string());
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::Install: {
                wevoaweb::PackageManager manager;
                logger.wevoa("Installing " + command.packageName + "...");
                const auto result = manager.install(command.packageName, command.installOptions.globalMode);
                logger.success("Package installed");
                logger.info("Name: " + result.packageName);
                logger.info("Version: " + result.version);
                logger.info("Path: " + result.destination.string());
                logger.info("Cache: " + result.cachePath.string());
                if (result.installedFromCore) {
                    logger.info("Source: official core package");
                }
                for (const auto& dependency : result.installedDependencies) {
                    logger.build("Dependency: " + dependency);
                }
                for (const auto& warning : result.warnings) {
                    logger.warn(warning);
                }
                if (result.createdPlaceholder) {
                    logger.watch("Installed a local placeholder package. Replace packages/" + result.packageName +
                                 "/main.wev with real package code.");
                }
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::RemovePackage: {
                wevoaweb::PackageManager manager;
                const auto result = manager.remove(command.packageName);
                logger.success("Package removed");
                logger.info("Name: " + result.packageName);
                logger.info("Path: " + result.destination.string());
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::ListPackages: {
                wevoaweb::PackageManager manager;
                const auto packages =
                    command.listOptions.core ? manager.listAvailableCore() : manager.listInstalled();
                if (packages.empty()) {
                    logger.info(command.listOptions.core ? "No core packages are registered."
                                                         : "No packages are installed in this project.");
                    return 0;
                }

                logger.info(command.listOptions.core ? "Available core packages:" : "Installed packages:");
                for (const auto& package : packages) {
                    std::string line = package.packageName + "@" + package.version;
                    if (!package.source.empty()) {
                        line += " [" + package.source + "]";
                    }
                    if (package.isOfficial) {
                        line += " [official]";
                    }
                    if (!package.description.empty()) {
                        line += " - " + package.description;
                    }
                    logger.info(line);
                }
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::SearchPackages: {
                wevoaweb::PackageManager manager;
                logger.wevoa("Searching registry for \"" + command.searchOptions.query + "\"...");
                const auto packages = manager.search(command.searchOptions.query);
                if (packages.empty()) {
                    logger.info("No packages matched \"" + command.searchOptions.query + "\".");
                    return 0;
                }

                logger.info("Search results:");
                for (const auto& package : packages) {
                    std::string line = package.packageName + "@" + package.version;
                    if (package.isOfficial) {
                        line += " [official]";
                    }
                    if (!package.description.empty()) {
                        line += " - " + package.description;
                    }
                    logger.info(line);
                }
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::Doctor: {
                wevoaweb::ProjectInspector inspector;
                const auto report = inspector.doctorCurrentProject(command.inspectOptions.appDirectory,
                                                                  command.inspectOptions.viewsDirectory,
                                                                  command.inspectOptions.publicDirectory);
                for (const auto& check : report.checks) {
                    logger.success(check);
                }
                for (const auto& warning : report.warnings) {
                    logger.watch(warning);
                }
                return report.healthy ? 0 : 1;
            }

            case wevoaweb::CLIHandler::CommandType::Info: {
                if (!command.packageName.empty()) {
                    wevoaweb::PackageManager manager;
                    const auto info = manager.packageInfo(command.packageName);
                    logger.info("Package: " + info.packageName);
                    logger.info("Version: " + info.version);
                    if (!info.source.empty()) {
                        logger.info("Source: " + info.source);
                    }
                    if (info.isOfficial) {
                        logger.info("Trust: official");
                    }
                    if (!info.repo.empty()) {
                        logger.info("Repository: " + info.repo);
                    }
                    if (!info.description.empty()) {
                        logger.info("Description: " + info.description);
                    }
                    if (!info.usageSnippet.empty()) {
                        logger.info("Usage:");
                        std::cout << info.usageSnippet << '\n';
                    }
                    if (!info.dependencies.empty()) {
                        logger.info("Dependencies: " + std::to_string(info.dependencies.size()));
                        for (const auto& dependency : info.dependencies) {
                            logger.build(dependency);
                        }
                    }
                    if (!info.path.empty()) {
                        logger.info("Path: " + info.path.string());
                    }
                    return 0;
                }

                wevoaweb::ProjectInspector inspector;
                const auto info = inspector.inspectCurrentProject(command.inspectOptions.appDirectory,
                                                                  command.inspectOptions.viewsDirectory,
                                                                  command.inspectOptions.publicDirectory);
                logger.info("Runtime: " + std::string(wevoaweb::kWevoaRuntimeName));
                logger.info("Version: " + std::string(wevoaweb::kWevoaVersion));
                logger.info("Project root: " + info.root.string());
                logger.info("Environment: " + info.env);
                if (info.port.has_value()) {
                    logger.info("Configured port: " + std::to_string(*info.port));
                }
                logger.info("Routes: " + std::to_string(info.routeCount));
                logger.info("Views: " + std::to_string(info.viewCount));
                logger.info("Assets: " + std::to_string(info.assetCount));
                logger.info("Packages: " + std::to_string(info.packageCount));
                logger.info("Migrations: " + std::to_string(info.migrationCount));
                if (command.inspectOptions.globalMode && argc > 0) {
                    logger.info("Executable: " + std::filesystem::absolute(argv[0]).string());
                }
                return 0;
            }

            case wevoaweb::CLIHandler::CommandType::Version:
                std::cout << wevoaweb::kWevoaRuntimeName << ' ' << wevoaweb::kWevoaVersion << '\n';
                return 0;

            case wevoaweb::CLIHandler::CommandType::Start: {
                std::atomic<bool> interruptRequested = false;
                gInterruptRequested = &interruptRequested;
                const auto previousHandler = std::signal(SIGINT, handleInterruptSignal);

                wevoaweb::server::DevServer server(command.startOptions,
                                                   logger,
                                                   interruptRequested,
                                                   std::cin,
                                                   std::cout);
                const int exitCode = server.run();

                std::signal(SIGINT, previousHandler);
                gInterruptRequested = nullptr;
                return exitCode;
            }

            case wevoaweb::CLIHandler::CommandType::Serve: {
                std::atomic<bool> interruptRequested = false;
                gInterruptRequested = &interruptRequested;
                const auto previousHandler = std::signal(SIGINT, handleInterruptSignal);

                wevoaweb::server::ServeServer server(command.serveOptions,
                                                     logger,
                                                     interruptRequested,
                                                     std::cin,
                                                     std::cout);
                const int exitCode = server.run();

                std::signal(SIGINT, previousHandler);
                gInterruptRequested = nullptr;
                return exitCode;
            }
        }
    } catch (const wevoaweb::CLIUsageError& error) {
        logger.error(error.what());
        std::cout << '\n';
        cli.printHelp(argc > 0 ? argv[0] : "wevoa");
        return 1;
    } catch (const std::exception& error) {
        logger.error(error.what());
        return 1;
    }

    return 0;
}
