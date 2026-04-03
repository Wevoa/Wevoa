#pragma once

#include <filesystem>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "interpreter/value.h"
#include "utils/source.h"

namespace wevoaweb {

class Environment;
class Expr;
class Interpreter;

class TemplateEngine {
  public:
    struct CompiledExpression {
        std::string source;
        std::shared_ptr<Expr> expression;
    };

    struct ComponentAttribute {
        std::string name;
        std::string rawValue;
        std::optional<CompiledExpression> expression;
    };

    struct CompiledNode {
        enum class Type {
            Text,
            Expression,
            Include,
            ForBlock,
            IfBlock,
            Component,
        };

        Type type = Type::Text;
        std::string text;
        std::string identifier;
        std::filesystem::path includePath;
        std::optional<CompiledExpression> expression;
        std::vector<ComponentAttribute> attributes;
        std::vector<CompiledNode> children;
        std::vector<CompiledNode> elseChildren;
    };

    struct ParsedTemplate {
        std::optional<std::filesystem::path> layoutPath;
        std::string body;
        std::unordered_map<std::string, std::string> sections;
    };

    struct CompiledTemplate {
        std::filesystem::path path;
        std::optional<std::filesystem::path> layoutPath;
        std::vector<CompiledNode> bodyNodes;
        std::unordered_map<std::string, std::vector<CompiledNode>> sections;
        std::vector<std::filesystem::path> dependencies;
    };

    struct CompiledFragment {
        std::optional<std::filesystem::path> currentTemplate;
        std::vector<CompiledNode> nodes;
        std::vector<std::filesystem::path> dependencies;
    };

    explicit TemplateEngine(std::filesystem::path viewsRoot, bool strictCache = false);

    std::string render(const std::string& templatePath,
                       const Value& data,
                       Interpreter& interpreter,
                       const SourceSpan& span) const;
    std::string renderInline(const std::string& source, Interpreter& interpreter) const;
    void validateTemplate(const std::string& templatePath) const;
    void validateTemplateFile(const std::filesystem::path& path) const;
    void preloadTemplates(const std::vector<std::string>& templatePaths) const;
    void preloadTemplateFragments(const std::vector<std::string>& sources,
                                  const std::vector<std::string>& templateContexts = {}) const;
    Value::Array compiledTemplateManifest() const;
    void clearCache() const;
    void invalidateTemplate(const std::filesystem::path& path) const;
    void setStrictCache(bool enabled);
    void setCacheLimits(std::size_t templateLimit, std::size_t fragmentLimit);

    static void resetThreadMetrics();
    static std::uint64_t consumeThreadRenderMicros();

    const std::filesystem::path& viewsRoot() const;

  private:
    ParsedTemplate parseTemplateSource(const std::filesystem::path& path) const;
    std::shared_ptr<const CompiledTemplate> compileTemplate(const std::filesystem::path& path) const;
    std::shared_ptr<const CompiledFragment> compileFragment(const std::string& source,
                                                            const std::filesystem::path* currentTemplate) const;
    std::string renderTemplateFile(const std::filesystem::path& path,
                                   Interpreter& interpreter,
                                   std::shared_ptr<Environment> environment) const;
    std::string renderCompiledTemplate(const CompiledTemplate& compiled,
                                       Interpreter& interpreter,
                                       std::shared_ptr<Environment> environment) const;
    std::string renderCompiledFragment(const CompiledFragment& compiled,
                                       Interpreter& interpreter,
                                       std::shared_ptr<Environment> environment) const;
    std::string renderTemplateString(const std::string& source,
                                     Interpreter& interpreter,
                                     std::shared_ptr<Environment> environment,
                                     const std::filesystem::path* currentTemplate = nullptr) const;
    std::filesystem::path resolveTemplatePath(const std::string& templatePath) const;
    std::filesystem::path resolveRelativeTemplatePath(const std::filesystem::path& currentTemplate,
                                                      const std::filesystem::path& relativeTemplate) const;
    std::shared_ptr<Environment> makeTemplateEnvironment(const Value& data,
                                                         Interpreter& interpreter,
                                                         const SourceSpan& span) const;

    std::filesystem::path viewsRoot_;
    bool strictCache_ = false;
    std::size_t templateCacheLimit_ = 256;
    std::size_t fragmentCacheLimit_ = 512;
    mutable std::mutex cacheMutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<const CompiledTemplate>> compiledTemplates_;
    mutable std::unordered_map<std::string, std::shared_ptr<const CompiledFragment>> compiledFragments_;
    mutable std::list<std::string> templateCacheOrder_;
    mutable std::list<std::string> fragmentCacheOrder_;
    mutable std::unordered_map<std::string, std::list<std::string>::iterator> templateCachePositions_;
    mutable std::unordered_map<std::string, std::list<std::string>::iterator> fragmentCachePositions_;
};

}  // namespace wevoaweb
