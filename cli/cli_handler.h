#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

#include "utils/defaults.h"

namespace wevoaweb {

class CLIUsageError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct ProjectCommandOptions {
    std::string appDirectory = "app";
    std::string viewsDirectory = "views";
    std::string publicDirectory = "public";
};

struct StartCommandOptions : ProjectCommandOptions {
    std::uint16_t port = kDefaultPort;
    bool debugAst = false;
    bool portSpecified = false;
};

struct BuildCommandOptions : ProjectCommandOptions {
    std::string outputDirectory = "build";
    bool staticExport = false;
};

struct ServeCommandOptions {
    std::uint16_t port = kDefaultPort;
    std::string buildDirectory = "build";
    bool portSpecified = false;
};

struct InspectCommandOptions : ProjectCommandOptions {
    bool globalMode = false;
};

struct InstallCommandOptions {
    bool globalMode = false;
};

struct ListCommandOptions {
    bool core = false;
};

struct SearchCommandOptions {
    std::string query;
};

class CLIHandler {
  public:
    enum class CommandType {
        Start,
        Create,
        Build,
        Serve,
        Migrate,
        MakeMigration,
        Install,
        RemovePackage,
        ListPackages,
        SearchPackages,
        Doctor,
        Info,
        Version,
        Help
    };

    struct Command {
        CommandType type = CommandType::Help;
        StartCommandOptions startOptions {};
        BuildCommandOptions buildOptions {};
        ServeCommandOptions serveOptions {};
        InspectCommandOptions inspectOptions {};
        std::string templateName = "app";
        std::string projectName;
        std::string migrationName;
        std::string packageName;
        InstallCommandOptions installOptions {};
        ListCommandOptions listOptions {};
        SearchCommandOptions searchOptions {};
    };

    Command parse(int argc, char** argv) const;
    void printHelp(const std::string& executableName) const;

  private:
    std::uint16_t parsePort(const std::string& value) const;
};

}  // namespace wevoaweb
