#include "cli/build_pipeline.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include "runtime/config_loader.h"
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

    copyDirectoryContents(sourceLayout.appDirectory, outputRoot / "app");
    copyDirectoryContents(sourceLayout.viewsDirectory, outputRoot / "views");
    copyDirectoryContents(sourceLayout.publicDirectory, outputRoot / "public");

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

    Value::Object manifest;
    manifest.insert_or_assign("runtime", Value(kWevoaRuntimeName));
    manifest.insert_or_assign("version", Value(kWevoaVersion));
    manifest.insert_or_assign("mode", Value("production"));
    manifest.insert_or_assign("output", Value(outputRoot.filename().string()));
    manifest.insert_or_assign("routes", Value(std::move(routeValues)));
    manifest.insert_or_assign("views", Value(std::move(viewValues)));
    manifest.insert_or_assign("assets", Value(std::move(assetValues)));

    writer_.writeTextFile(outputRoot / "wevoa.build.json", serializeJson(Value(std::move(manifest))));
    writer_.writeTextFile(outputRoot / "wevoa.templates.json",
                          serializeJson(Value(application.compiledTemplateManifest())));
    writer_.writeTextFile(outputRoot / "wevoa.snapshot.json", serializeJson(application.snapshotManifest()));
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

}  // namespace wevoaweb
