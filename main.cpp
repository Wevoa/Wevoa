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
                wevoaweb::ProjectCreator creator;
                const auto projectPath = creator.createProject(command.projectName);
                logger.wevoa("Project created successfully!");
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
                logger.build("Snapshot: " + (result.outputRoot / "wevoa.snapshot.json").string());
                logger.build("Templates: " + (result.outputRoot / "wevoa.templates.json").string());
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
                const auto result = manager.install(command.packageName);
                logger.success("Package installed");
                logger.info("Name: " + result.packageName);
                logger.info("Path: " + result.destination.string());
                if (result.createdPlaceholder) {
                    logger.watch("Installed a local placeholder package. Replace packages/" + result.packageName +
                                 "/main.wev with real package code.");
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
