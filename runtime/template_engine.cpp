#include "runtime/template_engine.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "ast/ast.h"
#include "interpreter/environment.h"
#include "interpreter/interpreter.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "utils/error.h"

namespace wevoaweb {

namespace {

using Node = TemplateEngine::CompiledNode;
using Expression = TemplateEngine::CompiledExpression;
using Attribute = TemplateEngine::ComponentAttribute;

struct ParseResult {
    std::vector<Node> nodes;
    std::optional<std::string> terminator;
};

thread_local std::uint64_t g_templateRenderMicros = 0;

std::string readFileContents(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open template file: " + path.string());
    }

    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool isIdentifierStart(char ch) {
    const auto unsignedChar = static_cast<unsigned char>(ch);
    return std::isalpha(unsignedChar) != 0 || ch == '_';
}

bool isIdentifierContinue(char ch) {
    const auto unsignedChar = static_cast<unsigned char>(ch);
    return std::isalnum(unsignedChar) != 0 || ch == '_';
}

bool matchesAnyTerminator(const std::string& directive, const std::vector<std::string>& terminators) {
    for (const auto& terminator : terminators) {
        if (directive == terminator) {
            return true;
        }
    }

    return false;
}

void appendDependency(std::vector<std::filesystem::path>& dependencies, const std::filesystem::path& path) {
    if (std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end()) {
        dependencies.push_back(path);
    }
}

std::optional<std::filesystem::path> parseExtendLine(const std::string& line) {
    const std::string trimmed = trim(line);
    if (!startsWith(trimmed, "extend ")) {
        return std::nullopt;
    }

    const auto firstQuote = trimmed.find('"');
    const auto lastQuote = trimmed.find_last_of('"');
    if (firstQuote == std::string::npos || lastQuote == std::string::npos || firstQuote == lastQuote) {
        throw std::runtime_error("Invalid extend directive: " + line);
    }

    return std::filesystem::path(trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1));
}

std::optional<std::string> parseSectionLine(const std::string& line) {
    const std::string trimmed = trim(line);
    if (!startsWith(trimmed, "section ")) {
        return std::nullopt;
    }

    if (trimmed.empty() || trimmed.back() != '{') {
        throw std::runtime_error("Invalid section directive: " + line);
    }

    std::string name = trim(trimmed.substr(8, trimmed.size() - 9));
    if (name.empty()) {
        throw std::runtime_error("Section name must not be empty.");
    }

    return name;
}

Expression compileExpression(const std::string& expressionSource) {
    try {
        Lexer lexer(expressionSource);
        const auto tokens = lexer.scanTokens();
        Parser parser(tokens, expressionSource);
        auto expression = parser.parseExpressionOnly();
        return Expression {expressionSource, std::shared_ptr<Expr>(expression.release())};
    } catch (const WevoaError& error) {
        throw std::runtime_error("Template expression '{{ " + expressionSource + " }}' failed: " +
                                 std::string(error.what()));
    }
}

Value evaluateExpression(const Expression& expression,
                         Interpreter& interpreter,
                         const std::shared_ptr<Environment>& environment) {
    return interpreter.evaluateInEnvironment(*expression.expression, environment);
}

std::pair<std::string, Expression> parseForDirective(const std::string& directive) {
    const std::string remainder = trim(directive.substr(4));
    if (remainder.empty()) {
        throw std::runtime_error("Invalid for directive: " + directive);
    }

    std::size_t cursor = 0;
    if (!isIdentifierStart(remainder[cursor])) {
        throw std::runtime_error("Invalid loop variable in directive: " + directive);
    }

    ++cursor;
    while (cursor < remainder.size() && isIdentifierContinue(remainder[cursor])) {
        ++cursor;
    }

    const std::string variableName = remainder.substr(0, cursor);
    while (cursor < remainder.size() && std::isspace(static_cast<unsigned char>(remainder[cursor])) != 0) {
        ++cursor;
    }

    if (cursor + 1 >= remainder.size() || remainder.substr(cursor, 2) != "in") {
        throw std::runtime_error("For directive must use 'in': " + directive);
    }

    cursor += 2;
    while (cursor < remainder.size() && std::isspace(static_cast<unsigned char>(remainder[cursor])) != 0) {
        ++cursor;
    }

    const std::string iterableExpression = trim(remainder.substr(cursor));
    if (iterableExpression.empty()) {
        throw std::runtime_error("For directive iterable expression must not be empty.");
    }

    return {variableName, compileExpression(iterableExpression)};
}

std::string parseIncludeDirective(const std::string& directive) {
    const std::string remainder = trim(directive.substr(8));
    if (remainder.size() < 2 || remainder.front() != '"' || remainder.back() != '"') {
        throw std::runtime_error("Include directive must be formatted as include \"file.wev\".");
    }

    const std::string path = remainder.substr(1, remainder.size() - 2);
    if (path.empty()) {
        throw std::runtime_error("Include directive path must not be empty.");
    }

    return path;
}

std::string normalizeStandaloneTemplateDirectives(const std::string& source) {
    std::istringstream stream(source);
    std::ostringstream normalized;
    std::string line;

    while (std::getline(stream, line)) {
        const std::string trimmed = trim(line);
        if (startsWith(trimmed, "include ")) {
            normalized << "{% " << trimmed << " %}";
        } else {
            normalized << line;
        }

        if (!stream.eof()) {
            normalized << '\n';
        }
    }

    return normalized.str();
}

std::unordered_map<std::string, std::string> parseComponentAttributes(const std::string& source) {
    std::unordered_map<std::string, std::string> attributes;
    std::size_t cursor = 0;

    while (cursor < source.size()) {
        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= source.size()) {
            break;
        }

        std::size_t nameStart = cursor;
        while (cursor < source.size() && isIdentifierContinue(source[cursor])) {
            ++cursor;
        }
        if (nameStart == cursor) {
            break;
        }

        const std::string name = source.substr(nameStart, cursor - nameStart);
        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= source.size() || source[cursor] != '=') {
            attributes.insert_or_assign(name, "");
            continue;
        }

        ++cursor;
        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= source.size() || source[cursor] != '"') {
            throw std::runtime_error("Component attribute '" + name + "' must use quoted values.");
        }

        ++cursor;
        std::size_t valueStart = cursor;
        while (cursor < source.size() && source[cursor] != '"') {
            ++cursor;
        }
        if (cursor >= source.size()) {
            throw std::runtime_error("Unterminated component attribute value.");
        }

        attributes.insert_or_assign(name, source.substr(valueStart, cursor - valueStart));
        ++cursor;
    }

    return attributes;
}

std::optional<Attribute> compileComponentAttribute(const std::string& name, const std::string& rawValue) {
    Attribute attribute;
    attribute.name = name;
    attribute.rawValue = rawValue;

    const std::string trimmedValue = trim(rawValue);
    if (startsWith(trimmedValue, "{{") && trimmedValue.size() >= 4 &&
        trimmedValue.substr(trimmedValue.size() - 2) == "}}") {
        attribute.expression = compileExpression(trim(trimmedValue.substr(2, trimmedValue.size() - 4)));
    }

    return attribute;
}

std::optional<std::pair<Node, std::size_t>> tryParseComponentNode(const std::string& source, std::size_t open) {
    if (open + 1 >= source.size() || !isIdentifierStart(source[open + 1])) {
        return std::nullopt;
    }

    const auto close = source.find("/>", open + 1);
    if (close == std::string::npos) {
        return std::nullopt;
    }

    const std::string markup = source.substr(open, close - open + 2);
    const std::string inner = trim(source.substr(open + 1, close - open - 1));

    std::size_t nameEnd = 0;
    while (nameEnd < inner.size() && isIdentifierContinue(inner[nameEnd])) {
        ++nameEnd;
    }
    if (nameEnd == 0) {
        return std::nullopt;
    }

    Node node;
    node.type = Node::Type::Component;
    node.text = inner.substr(0, nameEnd);
    node.identifier = markup;

    const auto attributes = parseComponentAttributes(inner.substr(nameEnd));
    for (const auto& [name, value] : attributes) {
        const auto attribute = compileComponentAttribute(name, value);
        if (attribute.has_value()) {
            node.attributes.push_back(*attribute);
        }
    }

    return std::make_pair(std::move(node), close + 2);
}

std::size_t findNextPotentialComponent(const std::string& source, std::size_t cursor) {
    std::size_t search = cursor;
    while (search < source.size()) {
        const auto candidate = source.find('<', search);
        if (candidate == std::string::npos) {
            return candidate;
        }

        if (tryParseComponentNode(source, candidate).has_value()) {
            return candidate;
        }

        search = candidate + 1;
    }

    return std::string::npos;
}

std::size_t findNextTemplateMarker(const std::string& source, std::size_t cursor) {
    const auto expressionPos = source.find("{{", cursor);
    const auto directivePos = source.find("{%", cursor);
    const auto componentPos = findNextPotentialComponent(source, cursor);

    std::size_t marker = std::string::npos;
    for (const auto candidate : {expressionPos, directivePos, componentPos}) {
        if (candidate == std::string::npos) {
            continue;
        }
        marker = marker == std::string::npos ? candidate : std::min(marker, candidate);
    }

    return marker;
}

ParseResult compileTemplateNodes(const std::string& source,
                                 std::size_t& cursor,
                                 const std::vector<std::string>& terminators,
                                 const std::filesystem::path* currentTemplate,
                                 std::vector<std::filesystem::path>& dependencies,
                                 const std::function<std::filesystem::path(const std::filesystem::path&,
                                                                          const std::filesystem::path&)>&
                                     resolveRelativePath,
                                 const std::function<std::filesystem::path(const std::string&)>& resolveTemplatePath) {
    ParseResult result;

    while (cursor < source.size()) {
        const auto marker = findNextTemplateMarker(source, cursor);
        if (marker == std::string::npos) {
            if (cursor < source.size()) {
                Node textNode;
                textNode.type = Node::Type::Text;
                textNode.text = source.substr(cursor);
                result.nodes.push_back(std::move(textNode));
            }
            cursor = source.size();
            return result;
        }

        if (marker > cursor) {
            Node textNode;
            textNode.type = Node::Type::Text;
            textNode.text = source.substr(cursor, marker - cursor);
            result.nodes.push_back(std::move(textNode));
        }

        if (source.compare(marker, 2, "{{") == 0) {
            const auto close = source.find("}}", marker + 2);
            if (close == std::string::npos) {
                throw std::runtime_error("Unclosed template expression.");
            }

            Node expressionNode;
            expressionNode.type = Node::Type::Expression;
            expressionNode.expression = compileExpression(trim(source.substr(marker + 2, close - marker - 2)));
            result.nodes.push_back(std::move(expressionNode));
            cursor = close + 2;
            continue;
        }

        if (source.compare(marker, 2, "{%") == 0) {
            const auto close = source.find("%}", marker + 2);
            if (close == std::string::npos) {
                throw std::runtime_error("Unclosed template directive.");
            }

            const std::string directive = trim(source.substr(marker + 2, close - marker - 2));
            cursor = close + 2;

            if (matchesAnyTerminator(directive, terminators)) {
                result.terminator = directive;
                return result;
            }

            if (startsWith(directive, "for ")) {
                Node forNode;
                forNode.type = Node::Type::ForBlock;
                auto [variableName, iterableExpression] = parseForDirective(directive);
                forNode.text = std::move(variableName);
                forNode.expression = std::move(iterableExpression);

                auto body = compileTemplateNodes(
                    source, cursor, {"end"}, currentTemplate, dependencies, resolveRelativePath, resolveTemplatePath);
                if (!body.terminator.has_value() || *body.terminator != "end") {
                    throw std::runtime_error("For directive must be closed with {% end %}.");
                }

                forNode.children = std::move(body.nodes);
                result.nodes.push_back(std::move(forNode));
                continue;
            }

            if (startsWith(directive, "include ")) {
                Node includeNode;
                includeNode.type = Node::Type::Include;
                const std::string includePath = parseIncludeDirective(directive);
                includeNode.includePath = currentTemplate != nullptr
                                              ? resolveRelativePath(*currentTemplate, std::filesystem::path(includePath))
                                              : resolveTemplatePath(includePath);
                appendDependency(dependencies, includeNode.includePath);
                result.nodes.push_back(std::move(includeNode));
                continue;
            }

            if (startsWith(directive, "if ")) {
                const std::string conditionExpression = trim(directive.substr(3));
                if (conditionExpression.empty()) {
                    throw std::runtime_error("If directive condition must not be empty.");
                }

                Node ifNode;
                ifNode.type = Node::Type::IfBlock;
                ifNode.expression = compileExpression(conditionExpression);

                auto thenBranch = compileTemplateNodes(
                    source, cursor, {"else", "end"}, currentTemplate, dependencies, resolveRelativePath, resolveTemplatePath);
                if (thenBranch.terminator == std::optional<std::string> {"else"}) {
                    auto elseBranch = compileTemplateNodes(
                        source, cursor, {"end"}, currentTemplate, dependencies, resolveRelativePath, resolveTemplatePath);
                    if (!elseBranch.terminator.has_value() || *elseBranch.terminator != "end") {
                        throw std::runtime_error("If directive else branch must be closed with {% end %}.");
                    }
                    ifNode.elseChildren = std::move(elseBranch.nodes);
                } else if (!thenBranch.terminator.has_value() || *thenBranch.terminator != "end") {
                    throw std::runtime_error("If directive must be closed with {% end %}.");
                }

                ifNode.children = std::move(thenBranch.nodes);
                result.nodes.push_back(std::move(ifNode));
                continue;
            }

            if (directive == "else" || directive == "end") {
                throw std::runtime_error("Unexpected template directive {% " + directive + " %}.");
            }

            throw std::runtime_error("Unknown template directive {% " + directive + " %}.");
        }

        const auto componentNode = tryParseComponentNode(source, marker);
        if (!componentNode.has_value()) {
            Node textNode;
            textNode.type = Node::Type::Text;
            textNode.text.assign(1, source[marker]);
            result.nodes.push_back(std::move(textNode));
            cursor = marker + 1;
            continue;
        }

        result.nodes.push_back(componentNode->first);
        cursor = componentNode->second;
    }

    return result;
}

std::string renderNodes(const std::vector<Node>& nodes,
                        Interpreter& interpreter,
                        const std::shared_ptr<Environment>& environment,
                        const std::filesystem::path* currentTemplate,
                        const std::function<std::string(const std::filesystem::path&,
                                                        const std::shared_ptr<Environment>&)>& includeRenderer,
                        const std::function<std::string(const std::string&,
                                                        Interpreter&,
                                                        std::shared_ptr<Environment>,
                                                        const std::filesystem::path*)>& inlineRenderer) {
    std::string output;

    for (const auto& node : nodes) {
        switch (node.type) {
            case Node::Type::Text:
                output += node.text;
                break;

            case Node::Type::Expression:
                if (node.expression.has_value()) {
                    output += evaluateExpression(*node.expression, interpreter, environment).toString();
                }
                break;

            case Node::Type::Include:
                output += includeRenderer(node.includePath, environment);
                break;

            case Node::Type::ForBlock: {
                const Value iterable = evaluateExpression(*node.expression, interpreter, environment);

                if (iterable.isNil()) {
                    break;
                }

                if (iterable.isArray()) {
                    std::int64_t index = 0;
                    for (const auto& item : iterable.asArray()) {
                        auto loopEnvironment = std::make_shared<Environment>(environment);
                        loopEnvironment->define(node.text, item, true);
                        loopEnvironment->define(node.text + "_index", Value(index), true);
                        output += renderNodes(
                            node.children, interpreter, loopEnvironment, currentTemplate, includeRenderer, inlineRenderer);
                        ++index;
                    }
                    break;
                }

                if (iterable.isObject()) {
                    for (const auto& [key, item] : iterable.asObject()) {
                        auto loopEnvironment = std::make_shared<Environment>(environment);
                        loopEnvironment->define(node.text, item, true);
                        loopEnvironment->define(node.text + "_key", Value(key), true);
                        output += renderNodes(
                            node.children, interpreter, loopEnvironment, currentTemplate, includeRenderer, inlineRenderer);
                    }
                    break;
                }

                throw std::runtime_error("Template for-loop expression must evaluate to an array, object, or nil.");
            }

            case Node::Type::IfBlock:
                if (evaluateExpression(*node.expression, interpreter, environment).isTruthy()) {
                    output += renderNodes(
                        node.children, interpreter, environment, currentTemplate, includeRenderer, inlineRenderer);
                } else {
                    output += renderNodes(
                        node.elseChildren, interpreter, environment, currentTemplate, includeRenderer, inlineRenderer);
                }
                break;

            case Node::Type::Component: {
                if (!interpreter.hasComponent(node.text)) {
                    output += node.identifier;
                    break;
                }

                auto componentEnvironment = std::make_shared<Environment>(environment);
                for (const auto& attribute : node.attributes) {
                    Value value = Value(attribute.rawValue);
                    if (attribute.expression.has_value()) {
                        value = evaluateExpression(*attribute.expression, interpreter, environment);
                    }
                    componentEnvironment->define(attribute.name, std::move(value), true);
                }

                output += inlineRenderer(interpreter.componentSource(node.text, SourceSpan {}),
                                         interpreter,
                                         componentEnvironment,
                                         currentTemplate);
                break;
            }
        }
    }

    return output;
}

std::size_t countNodes(const std::vector<Node>& nodes) {
    std::size_t total = nodes.size();
    for (const auto& node : nodes) {
        total += countNodes(node.children);
        total += countNodes(node.elseChildren);
    }
    return total;
}

std::string fragmentCacheKey(const std::string& source, const std::filesystem::path* currentTemplate) {
    return (currentTemplate != nullptr ? currentTemplate->generic_string() : std::string()) + "\n" + source;
}

template <typename CacheMap>
void evictLeastRecentlyUsed(CacheMap& cache,
                            std::list<std::string>& order,
                            std::unordered_map<std::string, std::list<std::string>::iterator>& positions,
                            std::size_t limit) {
    if (limit == 0) {
        cache.clear();
        order.clear();
        positions.clear();
        return;
    }

    while (cache.size() > limit && !order.empty()) {
        const std::string key = order.back();
        order.pop_back();
        positions.erase(key);
        cache.erase(key);
    }
}

void touchLeastRecentlyUsed(const std::string& key,
                           std::list<std::string>& order,
                           std::unordered_map<std::string, std::list<std::string>::iterator>& positions) {
    if (const auto found = positions.find(key); found != positions.end()) {
        order.erase(found->second);
    }

    order.push_front(key);
    positions.insert_or_assign(key, order.begin());
}

class RenderTimer final {
  public:
    RenderTimer() : started_(std::chrono::steady_clock::now()) {}

    ~RenderTimer() {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_);
        g_templateRenderMicros += static_cast<std::uint64_t>(elapsed.count());
    }

  private:
    std::chrono::steady_clock::time_point started_;
};

}  // namespace

TemplateEngine::TemplateEngine(std::filesystem::path viewsRoot, bool strictCache)
    : viewsRoot_(std::move(viewsRoot)), strictCache_(strictCache) {}

std::string TemplateEngine::render(const std::string& templatePath,
                                   const Value& data,
                                   Interpreter& interpreter,
                                   const SourceSpan& span) const {
    RenderTimer timer;
    auto environment = makeTemplateEnvironment(data, interpreter, span);
    return renderTemplateFile(resolveTemplatePath(templatePath), interpreter, std::move(environment));
}

std::string TemplateEngine::renderInline(const std::string& source, Interpreter& interpreter) const {
    RenderTimer timer;
    return renderTemplateString(source, interpreter, interpreter.currentEnvironment(), nullptr);
}

void TemplateEngine::validateTemplate(const std::string& templatePath) const {
    validateTemplateFile(resolveTemplatePath(templatePath));
}

void TemplateEngine::validateTemplateFile(const std::filesystem::path& path) const {
    static_cast<void>(compileTemplate(path));
}

void TemplateEngine::preloadTemplates(const std::vector<std::string>& templatePaths) const {
    for (const auto& templatePath : templatePaths) {
        static_cast<void>(compileTemplate(resolveTemplatePath(templatePath)));
    }
}

void TemplateEngine::preloadTemplateFragments(const std::vector<std::string>& sources,
                                              const std::vector<std::string>& templateContexts) const {
    for (const auto& source : sources) {
        static_cast<void>(compileFragment(source, nullptr));
        for (const auto& context : templateContexts) {
            const auto resolved = resolveTemplatePath(context);
            static_cast<void>(compileFragment(source, &resolved));
        }
    }
}

Value::Array TemplateEngine::compiledTemplateManifest() const {
    Value::Array manifest;
    std::lock_guard<std::mutex> lock(cacheMutex_);

    manifest.reserve(compiledTemplates_.size());
    for (const auto& [path, compiled] : compiledTemplates_) {
        Value::Object entry;
        entry.insert_or_assign("path", Value(path));
        entry.insert_or_assign("kind", Value("template"));
        entry.insert_or_assign("layout",
                               Value(compiled->layoutPath.has_value() ? compiled->layoutPath->generic_string() : ""));

        Value::Array dependencies;
        for (const auto& dependency : compiled->dependencies) {
            dependencies.emplace_back(dependency.generic_string());
        }
        entry.insert_or_assign("dependencies", Value(std::move(dependencies)));

        std::size_t nodeCount = countNodes(compiled->bodyNodes);
        for (const auto& [name, nodes] : compiled->sections) {
            static_cast<void>(name);
            nodeCount += countNodes(nodes);
        }
        entry.insert_or_assign("node_count", Value(static_cast<std::int64_t>(nodeCount)));
        entry.insert_or_assign("section_count", Value(static_cast<std::int64_t>(compiled->sections.size())));
        manifest.emplace_back(std::move(entry));
    }

    for (const auto& [key, compiled] : compiledFragments_) {
        Value::Object entry;
        entry.insert_or_assign("path", Value(key));
        entry.insert_or_assign("kind", Value("fragment"));
        entry.insert_or_assign("layout", Value(""));

        Value::Array dependencies;
        for (const auto& dependency : compiled->dependencies) {
            dependencies.emplace_back(dependency.generic_string());
        }
        entry.insert_or_assign("dependencies", Value(std::move(dependencies)));
        entry.insert_or_assign("node_count", Value(static_cast<std::int64_t>(countNodes(compiled->nodes))));
        entry.insert_or_assign("section_count", Value(static_cast<std::int64_t>(0)));
        manifest.emplace_back(std::move(entry));
    }

    return manifest;
}

void TemplateEngine::clearCache() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    compiledTemplates_.clear();
    compiledFragments_.clear();
    templateCacheOrder_.clear();
    fragmentCacheOrder_.clear();
    templateCachePositions_.clear();
    fragmentCachePositions_.clear();
}

void TemplateEngine::invalidateTemplate(const std::filesystem::path& path) const {
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    std::lock_guard<std::mutex> lock(cacheMutex_);
    const std::string key = (error ? path : resolved).string();
    compiledTemplates_.erase(key);
    if (const auto found = templateCachePositions_.find(key); found != templateCachePositions_.end()) {
        templateCacheOrder_.erase(found->second);
        templateCachePositions_.erase(found);
    }
    compiledFragments_.clear();
    fragmentCacheOrder_.clear();
    fragmentCachePositions_.clear();
}

void TemplateEngine::setStrictCache(bool enabled) {
    strictCache_ = enabled;
}

void TemplateEngine::setCacheLimits(std::size_t templateLimit, std::size_t fragmentLimit) {
    templateCacheLimit_ = templateLimit;
    fragmentCacheLimit_ = fragmentLimit;
}

void TemplateEngine::resetThreadMetrics() {
    g_templateRenderMicros = 0;
}

std::uint64_t TemplateEngine::consumeThreadRenderMicros() {
    const std::uint64_t total = g_templateRenderMicros;
    g_templateRenderMicros = 0;
    return total;
}

const std::filesystem::path& TemplateEngine::viewsRoot() const {
    return viewsRoot_;
}

TemplateEngine::ParsedTemplate TemplateEngine::parseTemplateSource(const std::filesystem::path& path) const {
    const std::string source = readFileContents(path);
    std::istringstream stream(source);

    ParsedTemplate parsed;
    std::ostringstream body;
    std::ostringstream sectionBody;
    std::string currentSection;
    bool firstNonEmptyProcessed = false;

    std::string line;
    while (std::getline(stream, line)) {
        if (!firstNonEmptyProcessed) {
            const std::string trimmedLine = trim(line);
            if (!trimmedLine.empty()) {
                firstNonEmptyProcessed = true;
                if (const auto extendPath = parseExtendLine(line); extendPath.has_value()) {
                    parsed.layoutPath = resolveRelativeTemplatePath(path, *extendPath);
                    continue;
                }
            }
        }

        if (currentSection.empty()) {
            if (const auto sectionName = parseSectionLine(line); sectionName.has_value()) {
                currentSection = *sectionName;
                sectionBody.str("");
                sectionBody.clear();
                continue;
            }

            body << line;
            if (!stream.eof()) {
                body << '\n';
            }
            continue;
        }

        if (trim(line) == "}") {
            parsed.sections.insert_or_assign(currentSection, sectionBody.str());
            currentSection.clear();
            sectionBody.str("");
            sectionBody.clear();
            continue;
        }

        sectionBody << line;
        if (!stream.eof()) {
            sectionBody << '\n';
        }
    }

    if (!currentSection.empty()) {
        throw std::runtime_error("Unclosed section '" + currentSection + "' in template " + path.string());
    }

    parsed.body = body.str();
    return parsed;
}

std::shared_ptr<const TemplateEngine::CompiledTemplate> TemplateEngine::compileTemplate(
    const std::filesystem::path& path) const {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    const auto resolvedPath = error ? path : normalized;
    const std::string cacheKey = resolvedPath.string();

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto found = compiledTemplates_.find(cacheKey);
        if (found != compiledTemplates_.end()) {
            touchLeastRecentlyUsed(cacheKey, templateCacheOrder_, templateCachePositions_);
            return found->second;
        }
    }

    auto compiled = std::make_shared<CompiledTemplate>();
    compiled->path = resolvedPath;

    const auto parsed = parseTemplateSource(resolvedPath);
    compiled->layoutPath = parsed.layoutPath;
    if (compiled->layoutPath.has_value()) {
        appendDependency(compiled->dependencies, *compiled->layoutPath);
    }

    const auto compileSource = [this, &resolvedPath, compiled](const std::string& source) {
        const std::string normalizedSource = normalizeStandaloneTemplateDirectives(source);
        std::size_t cursor = 0;
        auto parsedNodes = compileTemplateNodes(
            normalizedSource,
            cursor,
            {},
            &resolvedPath,
            compiled->dependencies,
            [this](const std::filesystem::path& currentTemplate, const std::filesystem::path& relativeTemplate) {
                return resolveRelativeTemplatePath(currentTemplate, relativeTemplate);
            },
            [this](const std::string& templatePath) { return resolveTemplatePath(templatePath); });
        if (parsedNodes.terminator.has_value()) {
            throw std::runtime_error("Unexpected template terminator {% " + *parsedNodes.terminator + " %}.");
        }
        return parsedNodes.nodes;
    };

    compiled->bodyNodes = compileSource(parsed.body);
    for (const auto& [name, sectionSource] : parsed.sections) {
        compiled->sections.insert_or_assign(name, compileSource(sectionSource));
    }

    if (compiled->layoutPath.has_value()) {
        static_cast<void>(compileTemplate(*compiled->layoutPath));
    }
    for (const auto& dependency : compiled->dependencies) {
        if (!compiled->layoutPath.has_value() || dependency != *compiled->layoutPath) {
            static_cast<void>(compileTemplate(dependency));
        }
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto [iterator, inserted] = compiledTemplates_.emplace(cacheKey, compiled);
        if (!inserted) {
            touchLeastRecentlyUsed(cacheKey, templateCacheOrder_, templateCachePositions_);
            return iterator->second;
        }
        if (!strictCache_) {
            touchLeastRecentlyUsed(cacheKey, templateCacheOrder_, templateCachePositions_);
            evictLeastRecentlyUsed(compiledTemplates_, templateCacheOrder_, templateCachePositions_, templateCacheLimit_);
        }
    }

    return compiled;
}

std::shared_ptr<const TemplateEngine::CompiledFragment> TemplateEngine::compileFragment(
    const std::string& source,
    const std::filesystem::path* currentTemplate) const {
    std::filesystem::path resolvedTemplate;
    const std::filesystem::path* normalizedTemplate = nullptr;
    if (currentTemplate != nullptr) {
        std::error_code error;
        resolvedTemplate = std::filesystem::weakly_canonical(*currentTemplate, error);
        normalizedTemplate = error ? currentTemplate : &resolvedTemplate;
    }

    const std::string cacheKey = fragmentCacheKey(source, normalizedTemplate);
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto found = compiledFragments_.find(cacheKey);
        if (found != compiledFragments_.end()) {
            touchLeastRecentlyUsed(cacheKey, fragmentCacheOrder_, fragmentCachePositions_);
            return found->second;
        }
    }

    auto compiled = std::make_shared<CompiledFragment>();
    if (normalizedTemplate != nullptr) {
        compiled->currentTemplate = *normalizedTemplate;
    }

    const std::string normalizedSource = normalizeStandaloneTemplateDirectives(source);
    std::size_t cursor = 0;
    auto parsed = compileTemplateNodes(
        normalizedSource,
        cursor,
        {},
        normalizedTemplate,
        compiled->dependencies,
        [this](const std::filesystem::path& templatePath, const std::filesystem::path& relativeTemplate) {
            return resolveRelativeTemplatePath(templatePath, relativeTemplate);
        },
        [this](const std::string& templatePath) { return resolveTemplatePath(templatePath); });

    if (parsed.terminator.has_value()) {
        throw std::runtime_error("Unexpected template terminator {% " + *parsed.terminator + " %}.");
    }

    compiled->nodes = std::move(parsed.nodes);
    for (const auto& dependency : compiled->dependencies) {
        static_cast<void>(compileTemplate(dependency));
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto [iterator, inserted] = compiledFragments_.emplace(cacheKey, compiled);
        if (!inserted) {
            touchLeastRecentlyUsed(cacheKey, fragmentCacheOrder_, fragmentCachePositions_);
            return iterator->second;
        }
        if (!strictCache_) {
            touchLeastRecentlyUsed(cacheKey, fragmentCacheOrder_, fragmentCachePositions_);
            evictLeastRecentlyUsed(compiledFragments_,
                                   fragmentCacheOrder_,
                                   fragmentCachePositions_,
                                   fragmentCacheLimit_);
        }
    }

    return compiled;
}

std::string TemplateEngine::renderTemplateFile(const std::filesystem::path& path,
                                               Interpreter& interpreter,
                                               std::shared_ptr<Environment> environment) const {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    const auto resolvedPath = error ? path : normalized;

    std::shared_ptr<const CompiledTemplate> compiled;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto found = compiledTemplates_.find(resolvedPath.string());
        if (found != compiledTemplates_.end()) {
            touchLeastRecentlyUsed(resolvedPath.string(), templateCacheOrder_, templateCachePositions_);
            compiled = found->second;
        }
    }

    if (!compiled) {
        if (strictCache_) {
            throw std::runtime_error("Template '" + resolvedPath.string() + "' was not precompiled.");
        }
        compiled = compileTemplate(resolvedPath);
    }

    return renderCompiledTemplate(*compiled, interpreter, std::move(environment));
}

std::string TemplateEngine::renderCompiledTemplate(const CompiledTemplate& compiled,
                                                   Interpreter& interpreter,
                                                   std::shared_ptr<Environment> environment) const {
    const auto includeRenderer = [this, &interpreter](const std::filesystem::path& includePath,
                                                      const std::shared_ptr<Environment>& env) {
        return renderTemplateFile(includePath, interpreter, env);
    };
    const auto inlineRenderer = [this](const std::string& source,
                                       Interpreter& nestedInterpreter,
                                       std::shared_ptr<Environment> env,
                                       const std::filesystem::path* currentTemplate) {
        return renderTemplateString(source, nestedInterpreter, std::move(env), currentTemplate);
    };

    if (compiled.layoutPath.has_value()) {
        auto layoutEnvironment = std::make_shared<Environment>(environment);

        if (!compiled.bodyNodes.empty() && compiled.sections.find("content") == compiled.sections.end()) {
            layoutEnvironment->define("content",
                                      Value(renderNodes(compiled.bodyNodes,
                                                        interpreter,
                                                        environment,
                                                        &compiled.path,
                                                        includeRenderer,
                                                        inlineRenderer)),
                                      true);
        }

        for (const auto& [name, nodes] : compiled.sections) {
            layoutEnvironment->define(name,
                                      Value(renderNodes(nodes,
                                                        interpreter,
                                                        environment,
                                                        &compiled.path,
                                                        includeRenderer,
                                                        inlineRenderer)),
                                      true);
        }

        return renderTemplateFile(*compiled.layoutPath, interpreter, std::move(layoutEnvironment));
    }

    if (!compiled.sections.empty() && compiled.bodyNodes.empty()) {
        const auto found = compiled.sections.find("content");
        if (found != compiled.sections.end()) {
            return renderNodes(found->second,
                               interpreter,
                               environment,
                               &compiled.path,
                               includeRenderer,
                               inlineRenderer);
        }
    }

    return renderNodes(compiled.bodyNodes,
                       interpreter,
                       environment,
                       &compiled.path,
                       includeRenderer,
                       inlineRenderer);
}

std::string TemplateEngine::renderCompiledFragment(const CompiledFragment& compiled,
                                                   Interpreter& interpreter,
                                                   std::shared_ptr<Environment> environment) const {
    return renderNodes(compiled.nodes,
                       interpreter,
                       environment,
                       compiled.currentTemplate.has_value() ? &*compiled.currentTemplate : nullptr,
                       [this, &interpreter](const std::filesystem::path& includePath,
                                            const std::shared_ptr<Environment>& env) {
                           return renderTemplateFile(includePath, interpreter, env);
                       },
                       [this](const std::string& childSource,
                              Interpreter& nestedInterpreter,
                              std::shared_ptr<Environment> nestedEnvironment,
                              const std::filesystem::path* nestedTemplate) {
                           return renderTemplateString(childSource,
                                                       nestedInterpreter,
                                                       std::move(nestedEnvironment),
                                                       nestedTemplate);
                       });
}

std::string TemplateEngine::renderTemplateString(const std::string& source,
                                                 Interpreter& interpreter,
                                                 std::shared_ptr<Environment> environment,
                                                 const std::filesystem::path* currentTemplate) const {
    std::shared_ptr<const CompiledFragment> compiled;
    const std::string cacheKey = fragmentCacheKey(source, currentTemplate);
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto found = compiledFragments_.find(cacheKey);
        if (found != compiledFragments_.end()) {
            touchLeastRecentlyUsed(cacheKey, fragmentCacheOrder_, fragmentCachePositions_);
            compiled = found->second;
        }
    }

    if (!compiled) {
        if (strictCache_) {
            throw std::runtime_error("Inline template fragment was not precompiled.");
        }
        compiled = compileFragment(source, currentTemplate);
    }

    return renderCompiledFragment(*compiled, interpreter, std::move(environment));
}

std::filesystem::path TemplateEngine::resolveTemplatePath(const std::string& templatePath) const {
    const std::filesystem::path candidate = viewsRoot_ / templatePath;
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(candidate, error);
    return error ? candidate : normalized;
}

std::filesystem::path TemplateEngine::resolveRelativeTemplatePath(const std::filesystem::path& currentTemplate,
                                                                  const std::filesystem::path& relativeTemplate) const {
    const auto candidate = currentTemplate.parent_path() / relativeTemplate;
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(candidate, error);
    return error ? candidate : normalized;
}

std::shared_ptr<Environment> TemplateEngine::makeTemplateEnvironment(const Value& data,
                                                                     Interpreter& interpreter,
                                                                     const SourceSpan& span) const {
    auto environment = std::make_shared<Environment>(interpreter.currentEnvironment());

    if (data.isNil()) {
        return environment;
    }

    if (!data.isObject()) {
        throw RuntimeError("view() context must be an object.", span);
    }

    for (const auto& [name, value] : data.asObject()) {
        environment->define(name, value, true);
    }

    return environment;
}

}  // namespace wevoaweb
