#include "cli/build_pipeline.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include "runtime/config_loader.h"
#include "server/http_types.h"
#include "server/web_app.h"
#include "utils/project_layout.h"
#include "utils/version.h"

namespace wevoaweb {

namespace {

Value withProductionEnvironment(Value config) {
    if (!config.isObject()) {
        config = Value(Value::Object {});
    }

    config.asObject().insert_or_assign("env", Value("production"));
    return config;
}

struct RouteDescriptor {
    std::string method;
    std::string path;
};

RouteDescriptor parseRouteDescriptor(const std::string& descriptor) {
    const auto separator = descriptor.find(' ');
    if (separator == std::string::npos) {
        return {"", descriptor};
    }

    return {descriptor.substr(0, separator), descriptor.substr(separator + 1)};
}

bool isStaticExportablePath(const std::string& path) {
    return !path.empty() && path.front() == '/' && path.find(':') == std::string::npos;
}

std::filesystem::path staticRouteOutputPath(const std::filesystem::path& staticRoot, const std::string& routePath) {
    if (routePath == "/") {
        return staticRoot / "index.html";
    }

    const auto relative = std::filesystem::path(routePath.substr(1));
    if (relative.filename().string().find('.') != std::string::npos) {
        return staticRoot / relative;
    }

    return staticRoot / relative / "index.html";
}

}  // namespace

BuildPipeline::BuildPipeline(FileWriter writer) : writer_(std::move(writer)) {}

BuildResult BuildPipeline::build(const BuildCommandOptions& options, std::istream& input, std::ostream& output) const {
    namespace fs = std::filesystem;

    const auto sourceLayout = detectSourceProjectLayout(fs::current_path(),
                                                        options.appDirectory,
                                                        options.viewsDirectory,
                                                        options.publicDirectory);

    const fs::path outputRoot = sourceLayout.root / options.outputDirectory;
    std::error_code error;
    if (fs::exists(outputRoot, error) && !error) {
        fs::remove_all(outputRoot, error);
        if (error) {
            throw std::runtime_error("Unable to clear build output: " + outputRoot.string());
        }
    }

    writer_.createDirectory(outputRoot);
    writer_.createDirectory(outputRoot / "app");
    writer_.createDirectory(outputRoot / "views");
    writer_.createDirectory(outputRoot / "public");
    writer_.createDirectory(outputRoot / "packages");

    copyDirectoryContents(sourceLayout.appDirectory, outputRoot / "app");
    copyDirectoryContents(sourceLayout.viewsDirectory, outputRoot / "views");
    copyDirectoryContents(sourceLayout.publicDirectory, outputRoot / "public");
    copyDirectoryContents(sourceLayout.packagesDirectory, outputRoot / "packages");

    Value config = withProductionEnvironment(loadConfigFile(sourceLayout.configFile));
    writer_.writeTextFile(outputRoot / "wevoa.config.json", serializeJson(config));

    server::WebApplication application((outputRoot / "app").string(),
                                       (outputRoot / "views").string(),
                                       (outputRoot / "public").string(),
                                       input,
                                       output,
                                       false,
                                       config);
    application.loadViews();
    application.validateTemplates();

    BuildResult result;
    result.outputRoot = outputRoot;
    result.routes = application.routePaths();
    result.views = application.viewPaths();
    result.assets = enumeratePublicAssets(outputRoot / "public");
    result.packages = enumeratePackageDirectories(outputRoot / "packages");

    if (options.staticExport) {
        result.staticOutputRoot = outputRoot / "static";
        writer_.createDirectory(result.staticOutputRoot);
        copyDirectoryContents(outputRoot / "public", result.staticOutputRoot);

        for (const auto& descriptorText : result.routes) {
            const auto descriptor = parseRouteDescriptor(descriptorText);
            if (descriptor.method != "GET" || !isStaticExportablePath(descriptor.path)) {
                continue;
            }

            server::HttpRequest request;
            request.method = descriptor.method;
            request.target = descriptor.path;
            request.path = descriptor.path;
            request.version = "HTTP/1.1";

            const auto response = application.render(request);
            if (response.statusCode != 200 || response.contentType.find("text/html") != 0) {
                continue;
            }

            writer_.writeTextFile(staticRouteOutputPath(result.staticOutputRoot, descriptor.path), response.body);
            result.staticRoutes.push_back(descriptor.path);
        }
    }

    Value::Array routeValues;
    routeValues.reserve(result.routes.size());
    for (const auto& route : result.routes) {
        routeValues.emplace_back(route);
    }

    Value::Array assetValues;
    assetValues.reserve(result.assets.size());
    for (const auto& asset : result.assets) {
        assetValues.emplace_back(asset);
    }

    Value::Array viewValues;
    viewValues.reserve(result.views.size());
    for (const auto& view : result.views) {
        viewValues.emplace_back(view);
    }

    Value::Array packageValues;
    packageValues.reserve(result.packages.size());
    for (const auto& package : result.packages) {
        packageValues.emplace_back(package);
    }

    Value::Object manifest;
    manifest.insert_or_assign("runtime", Value(kWevoaRuntimeName));
    manifest.insert_or_assign("version", Value(kWevoaVersion));
    manifest.insert_or_assign("mode", Value("production"));
    manifest.insert_or_assign("output", Value(outputRoot.filename().string()));
    manifest.insert_or_assign("routes", Value(std::move(routeValues)));
    manifest.insert_or_assign("views", Value(std::move(viewValues)));
    manifest.insert_or_assign("assets", Value(std::move(assetValues)));
    manifest.insert_or_assign("packages", Value(std::move(packageValues)));

    writer_.writeTextFile(outputRoot / "wevoa.build.json", serializeJson(Value(std::move(manifest))));
    writer_.writeTextFile(outputRoot / "wevoa.templates.json",
                          serializeJson(Value(application.compiledTemplateManifest())));
    writer_.writeTextFile(outputRoot / "wevoa.snapshot.json", serializeJson(application.snapshotManifest()));
    if (options.staticExport) {
        Value::Array staticRouteValues;
        staticRouteValues.reserve(result.staticRoutes.size());
        for (const auto& route : result.staticRoutes) {
            staticRouteValues.emplace_back(route);
        }

        Value::Object staticManifest;
        staticManifest.insert_or_assign("mode", Value("static"));
        staticManifest.insert_or_assign("output", Value(result.staticOutputRoot.filename().string()));
        staticManifest.insert_or_assign("routes", Value(std::move(staticRouteValues)));
        writer_.writeTextFile(outputRoot / "wevoa.static.json", serializeJson(Value(std::move(staticManifest))));
    }
    return result;
}

void BuildPipeline::copyDirectoryContents(const std::filesystem::path& source,
                                          const std::filesystem::path& target) const {
    namespace fs = std::filesystem;

    std::error_code error;
    if (!fs::exists(source, error) || !fs::is_directory(source, error) || error) {
        return;
    }

    for (fs::recursive_directory_iterator iterator(source, fs::directory_options::skip_permission_denied, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            throw std::runtime_error("Unable to copy build input directory: " + source.string());
        }

        if (!iterator->is_regular_file(error) || error) {
            continue;
        }

        const fs::path relative = fs::relative(iterator->path(), source, error);
        if (error) {
            throw std::runtime_error("Unable to resolve build input path: " + iterator->path().string());
        }

        const fs::path destination = target / relative;
        writer_.writeTextFile(destination, [&]() {
            std::ifstream stream(iterator->path(), std::ios::binary);
            if (!stream) {
                throw std::runtime_error("Unable to read build input file: " + iterator->path().string());
            }

            return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }());
    }
}

std::vector<std::string> BuildPipeline::enumeratePublicAssets(const std::filesystem::path& directory) const {
    namespace fs = std::filesystem;

    std::vector<std::string> assets;
    std::error_code error;
    if (!fs::exists(directory, error) || !fs::is_directory(directory, error) || error) {
        return assets;
    }

    for (fs::recursive_directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            throw std::runtime_error("Unable to enumerate built assets in: " + directory.string());
        }

        if (!iterator->is_regular_file(error) || error) {
            continue;
        }

        const fs::path relative = fs::relative(iterator->path(), directory, error);
        if (error) {
            continue;
        }

        assets.push_back('/' + relative.generic_string());
    }

    std::sort(assets.begin(), assets.end());
    return assets;
}

std::vector<std::string> BuildPipeline::enumeratePackageDirectories(const std::filesystem::path& directory) const {
    namespace fs = std::filesystem;

    std::vector<std::string> packages;
    std::error_code error;
    if (!fs::exists(directory, error) || !fs::is_directory(directory, error) || error) {
        return packages;
    }

    for (fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            throw std::runtime_error("Unable to enumerate built packages in: " + directory.string());
        }

        if (iterator->is_directory(error) && !error) {
            packages.push_back(iterator->path().filename().string());
        }
    }

    std::sort(packages.begin(), packages.end());
    return packages;
}

}  // namespace wevoaweb
