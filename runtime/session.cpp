#include "runtime/session.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "runtime/ast_printer.h"
#include "utils/error.h"

namespace wevoaweb {

namespace {

std::string readFileContents(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::string extractLine(const std::string& source, int lineNumber) {
    std::istringstream stream(source);
    std::string line;
    for (int currentLine = 1; std::getline(stream, line); ++currentLine) {
        if (currentLine == lineNumber) {
            return line;
        }
    }

    return "";
}

std::string pointerLine(int column) {
    std::string line;
    if (column > 1) {
        line.append(static_cast<std::size_t>(column - 1), ' ');
    }
    line += '^';
    return line;
}

std::string formatDiagnostic(const std::string& sourceName,
                             const std::string& source,
                             const WevoaError& error) {
    std::ostringstream stream;
    stream << sourceName << ":" << formatSpan(error.span()) << ": " << error.what();

    const std::string snippet = extractLine(source, error.span().start.line);
    if (!snippet.empty()) {
        stream << "\n\n" << snippet << "\n" << pointerLine(error.span().start.column);
    }

    return stream.str();
}

std::filesystem::path normalizePath(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolutePath = std::filesystem::absolute(path, error);
    if (error) {
        return path;
    }

    const auto canonicalPath = std::filesystem::weakly_canonical(absolutePath, error);
    return error ? absolutePath : canonicalPath;
}

std::filesystem::path resolvePackageImportPath(const std::string& packageSpecifier) {
    std::string specifier = packageSpecifier;
    if (!specifier.empty() && specifier.front() == '@') {
        specifier.erase(specifier.begin());
    }

    if (specifier.empty()) {
        throw std::runtime_error("Package import path must not be empty.");
    }

    const auto separator = specifier.find('/');
    const std::string packageName = separator == std::string::npos ? specifier : specifier.substr(0, separator);
    const std::string remainder = separator == std::string::npos ? "" : specifier.substr(separator + 1);

    std::filesystem::path resolved = std::filesystem::current_path() / "packages" / packageName;
    if (remainder.empty()) {
        resolved /= "main.wev";
    } else {
        resolved /= remainder;
    }

    return resolved;
}

void collectInlineTemplatesFromExpr(const Expr& expr, std::vector<std::string>& sources);

void collectInlineTemplatesFromStmt(const Stmt& stmt, std::vector<std::string>& sources) {
    if (const auto* expression = dynamic_cast<const ExpressionStmt*>(&stmt)) {
        collectInlineTemplatesFromExpr(*expression->expression, sources);
        return;
    }
    if (const auto* declaration = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        if (declaration->initializer) {
            collectInlineTemplatesFromExpr(*declaration->initializer, sources);
        }
        return;
    }
    if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
        for (const auto& child : block->statements) {
            collectInlineTemplatesFromStmt(*child, sources);
        }
        return;
    }
    if (const auto* conditional = dynamic_cast<const IfStmt*>(&stmt)) {
        collectInlineTemplatesFromExpr(*conditional->condition, sources);
        collectInlineTemplatesFromStmt(*conditional->thenBranch, sources);
        if (conditional->elseBranch) {
            collectInlineTemplatesFromStmt(*conditional->elseBranch, sources);
        }
        return;
    }
    if (const auto* loop = dynamic_cast<const LoopStmt*>(&stmt)) {
        if (loop->initializer) {
            collectInlineTemplatesFromStmt(*loop->initializer, sources);
        }
        if (loop->condition) {
            collectInlineTemplatesFromExpr(*loop->condition, sources);
        }
        if (loop->increment) {
            collectInlineTemplatesFromExpr(*loop->increment, sources);
        }
        collectInlineTemplatesFromStmt(*loop->body, sources);
        return;
    }
    if (const auto* loop = dynamic_cast<const WhileStmt*>(&stmt)) {
        collectInlineTemplatesFromExpr(*loop->condition, sources);
        collectInlineTemplatesFromStmt(*loop->body, sources);
        return;
    }
    if (const auto* function = dynamic_cast<const FuncDeclStmt*>(&stmt)) {
        collectInlineTemplatesFromStmt(*function->body, sources);
        return;
    }
    if (const auto* route = dynamic_cast<const RouteDeclStmt*>(&stmt)) {
        collectInlineTemplatesFromExpr(*route->path, sources);
        collectInlineTemplatesFromStmt(*route->body, sources);
        return;
    }
    if (const auto* importStmt = dynamic_cast<const ImportStmt*>(&stmt)) {
        collectInlineTemplatesFromExpr(*importStmt->path, sources);
        return;
    }
    if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (returnStmt->value) {
            collectInlineTemplatesFromExpr(*returnStmt->value, sources);
        }
    }
}

void collectInlineTemplatesFromExpr(const Expr& expr, std::vector<std::string>& sources) {
    if (const auto* html = dynamic_cast<const HtmlExpr*>(&expr)) {
        sources.push_back(html->source);
        return;
    }
    if (const auto* assign = dynamic_cast<const AssignExpr*>(&expr)) {
        collectInlineTemplatesFromExpr(*assign->value, sources);
        return;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
        collectInlineTemplatesFromExpr(*unary->right, sources);
        return;
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
        collectInlineTemplatesFromExpr(*binary->left, sources);
        collectInlineTemplatesFromExpr(*binary->right, sources);
        return;
    }
    if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expr)) {
        collectInlineTemplatesFromExpr(*grouping->expression, sources);
        return;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
        collectInlineTemplatesFromExpr(*call->callee, sources);
        for (const auto& argument : call->arguments) {
            collectInlineTemplatesFromExpr(*argument, sources);
        }
        return;
    }
    if (const auto* array = dynamic_cast<const ArrayExpr*>(&expr)) {
        for (const auto& element : array->elements) {
            collectInlineTemplatesFromExpr(*element, sources);
        }
        return;
    }
    if (const auto* object = dynamic_cast<const ObjectExpr*>(&expr)) {
        for (const auto& field : object->fields) {
            collectInlineTemplatesFromExpr(*field.value, sources);
        }
        return;
    }
    if (const auto* property = dynamic_cast<const GetExpr*>(&expr)) {
        collectInlineTemplatesFromExpr(*property->object, sources);
        return;
    }
    if (const auto* index = dynamic_cast<const IndexExpr*>(&expr)) {
        collectInlineTemplatesFromExpr(*index->object, sources);
        collectInlineTemplatesFromExpr(*index->index, sources);
    }
}

std::vector<std::string> deduplicateSources(std::vector<std::string> sources) {
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    return sources;
}

}  // namespace

RuntimeSession::RuntimeSession(std::istream& input, std::ostream& output, bool debugAst)
    : debugAst_(debugAst), interpreter_(output, input) {
    interpreter_.setImportLoader([this](const std::string& path, const SourceSpan& span) { importFile(path, span); });
}

void RuntimeSession::setDebugAst(bool enabled) {
    debugAst_ = enabled;
}

void RuntimeSession::defineGlobal(std::string name, Value value, bool isConstant) {
    interpreter_.defineGlobal(std::move(name), std::move(value), isConstant);
}

void RuntimeSession::setCurrentRequest(Value request) {
    interpreter_.setCurrentRequest(std::move(request));
}

void RuntimeSession::clearCurrentRequest() {
    interpreter_.clearCurrentRequest();
}

std::shared_ptr<Program> RuntimeSession::parseProgram(std::string_view sourceName, const std::string& source) {
    try {
        Lexer lexer(source);
        const auto tokens = lexer.scanTokens();

        Parser parser(tokens, source);
        return std::make_shared<Program>(parser.parse());
    } catch (const WevoaError& error) {
        const std::string label = sourceName.empty() ? "<memory>" : std::string(sourceName);
        throw std::runtime_error(formatDiagnostic(label, source, error));
    }
}

void RuntimeSession::executeProgram(const std::string& sourceName, const std::shared_ptr<Program>& program) {
    if (debugAst_) {
        AstPrinter printer;
        interpreter_.output() << printer.printProgram(*program);
    }

    programs_.push_back(program);
    if (!sourceName.empty()) {
        sourceStack_.push_back(sourceName);
    }

    try {
        interpreter_.setCurrentSourceName(sourceName);
        interpreter_.interpret(*program);
    } catch (...) {
        if (!sourceName.empty()) {
            sourceStack_.pop_back();
        }
        interpreter_.setCurrentSourceName(sourceStack_.empty() ? "" : sourceStack_.back());
        programs_.pop_back();
        throw;
    }

    if (!sourceName.empty()) {
        sourceStack_.pop_back();
    }
    interpreter_.setCurrentSourceName(sourceStack_.empty() ? "" : sourceStack_.back());
}

void RuntimeSession::runSource(const std::string& sourceName, const std::string& source) {
    if (!sourceName.empty()) {
        sourceCache_[sourceName] = source;
    }

    const auto program = parseProgram(sourceName, source);
    if (!sourceName.empty()) {
        parsedPrograms_.insert_or_assign(sourceName, program);
    }

    executeProgram(sourceName, program);
}

void RuntimeSession::runCachedSource(const std::string& sourceName) {
    const auto cachedProgram = parsedPrograms_.find(sourceName);
    if (cachedProgram != parsedPrograms_.end()) {
        executeProgram(sourceName, cachedProgram->second);
        return;
    }

    const auto cachedSource = sourceCache_.find(sourceName);
    if (cachedSource == sourceCache_.end()) {
        throw std::runtime_error("No cached source is available for: " + sourceName);
    }

    runSource(sourceName, cachedSource->second);
}

void RuntimeSession::shareParsedSourcesWith(RuntimeSession& target) const {
    target.sourceCache_ = sourceCache_;
    target.parsedPrograms_ = parsedPrograms_;
}

std::unique_ptr<RuntimeSession> RuntimeSession::cloneSnapshot() const {
    auto& snapshotSource = const_cast<Interpreter&>(interpreter_);
    auto snapshot = std::make_unique<RuntimeSession>(snapshotSource.input(), snapshotSource.output(), debugAst_);
    snapshot->sourceCache_ = sourceCache_;
    snapshot->parsedPrograms_ = parsedPrograms_;
    snapshot->programs_ = programs_;
    snapshot->importedFiles_ = importedFiles_;
    interpreter_.copySnapshotTo(snapshot->interpreter_);
    return snapshot;
}

void RuntimeSession::runFile(const std::string& path) {
    const auto normalized = normalizePath(path);
    runSource(normalized.string(), readFileContents(normalized));
}

bool RuntimeSession::hasRoute(const std::string& path, const std::string& method) const {
    return interpreter_.hasRoute(path, method);
}

Value RuntimeSession::renderRoute(const std::string& path, const std::string& method) {
    return interpreter_.renderRoute(path, method);
}

std::vector<std::string> RuntimeSession::routePaths() const {
    return interpreter_.routePaths();
}

std::vector<std::string> RuntimeSession::componentSources() const {
    return deduplicateSources(interpreter_.componentSources());
}

std::vector<std::string> RuntimeSession::inlineTemplateSources() const {
    std::vector<std::string> sources;
    for (const auto& program : programs_) {
        for (const auto& statement : *program) {
            collectInlineTemplatesFromStmt(*statement, sources);
        }
    }
    return deduplicateSources(std::move(sources));
}

Interpreter& RuntimeSession::interpreter() {
    return interpreter_;
}

void RuntimeSession::importFile(const std::string& path, const SourceSpan& span) {
    std::filesystem::path resolvedPath(path);

    if (!path.empty() && path.front() == '@') {
        resolvedPath = resolvePackageImportPath(path);
    } else if (resolvedPath.is_relative()) {
        if (!sourceStack_.empty()) {
            resolvedPath = std::filesystem::path(sourceStack_.back()).parent_path() / resolvedPath;
        } else {
            resolvedPath = std::filesystem::current_path() / resolvedPath;
        }
    }

    resolvedPath = normalizePath(resolvedPath);
    const std::string cacheKey = resolvedPath.string();
    if (!importedFiles_.insert(cacheKey).second) {
        return;
    }

    try {
        if (parsedPrograms_.find(cacheKey) != parsedPrograms_.end()) {
            runCachedSource(cacheKey);
        } else {
            runSource(cacheKey, readFileContents(resolvedPath));
        }
    } catch (const std::exception&) {
        importedFiles_.erase(cacheKey);
        throw;
    } catch (...) {
        importedFiles_.erase(cacheKey);
        throw RuntimeError("Import failed for '" + cacheKey + "'.", span);
    }
}

}  // namespace wevoaweb
