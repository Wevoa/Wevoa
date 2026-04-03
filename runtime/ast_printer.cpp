#include "runtime/ast_printer.h"

namespace wevoaweb {

std::string AstPrinter::printProgram(const std::vector<std::unique_ptr<Stmt>>& statements) const {
    std::string output = "Program\n";
    for (const auto& statement : statements) {
        printStmt(*statement, 1, output);
    }
    return output;
}

void AstPrinter::printStmt(const Stmt& stmt, int indent, std::string& output) const {
    if (const auto* expression = dynamic_cast<const ExpressionStmt*>(&stmt)) {
        appendLine(output, indent, "ExpressionStmt");
        printExpr(*expression->expression, indent + 1, output);
        return;
    }

    if (const auto* variable = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        appendLine(output,
                   indent,
                   std::string(variable->isConstant ? "ConstDecl " : "LetDecl ") + variable->name.lexeme);
        if (variable->initializer) {
            printExpr(*variable->initializer, indent + 1, output);
        }
        return;
    }

    if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
        appendLine(output, indent, "Block");
        for (const auto& nested : block->statements) {
            printStmt(*nested, indent + 1, output);
        }
        return;
    }

    if (const auto* conditional = dynamic_cast<const IfStmt*>(&stmt)) {
        appendLine(output, indent, "If");
        appendLine(output, indent + 1, "Condition");
        printExpr(*conditional->condition, indent + 2, output);
        appendLine(output, indent + 1, "Then");
        printStmt(*conditional->thenBranch, indent + 2, output);
        if (conditional->elseBranch) {
            appendLine(output, indent + 1, "Else");
            printStmt(*conditional->elseBranch, indent + 2, output);
        }
        return;
    }

    if (const auto* loop = dynamic_cast<const LoopStmt*>(&stmt)) {
        appendLine(output, indent, "Loop");
        if (loop->initializer) {
            appendLine(output, indent + 1, "Initializer");
            printStmt(*loop->initializer, indent + 2, output);
        }
        if (loop->condition) {
            appendLine(output, indent + 1, "Condition");
            printExpr(*loop->condition, indent + 2, output);
        }
        if (loop->increment) {
            appendLine(output, indent + 1, "Increment");
            printExpr(*loop->increment, indent + 2, output);
        }
        appendLine(output, indent + 1, "Body");
        printStmt(*loop->body, indent + 2, output);
        return;
    }

    if (const auto* loop = dynamic_cast<const WhileStmt*>(&stmt)) {
        appendLine(output, indent, "While");
        appendLine(output, indent + 1, "Condition");
        printExpr(*loop->condition, indent + 2, output);
        appendLine(output, indent + 1, "Body");
        printStmt(*loop->body, indent + 2, output);
        return;
    }

    if (const auto* function = dynamic_cast<const FuncDeclStmt*>(&stmt)) {
        std::string line = "FuncDecl " + function->name.lexeme + "(";
        for (std::size_t i = 0; i < function->params.size(); ++i) {
            if (i > 0) {
                line += ", ";
            }
            line += function->params[i].lexeme;
        }
        line += ")";
        appendLine(output, indent, line);
        printStmt(*function->body, indent + 1, output);
        return;
    }

    if (const auto* route = dynamic_cast<const RouteDeclStmt*>(&stmt)) {
        appendLine(output, indent, "RouteDecl " + route->method);
        appendLine(output, indent + 1, "Path");
        printExpr(*route->path, indent + 2, output);
        if (!route->middleware.empty()) {
            appendLine(output, indent + 1, "Middleware");
            for (const auto& middleware : route->middleware) {
                appendLine(output, indent + 2, middleware.lexeme);
            }
        }
        appendLine(output, indent + 1, "Body");
        printStmt(*route->body, indent + 2, output);
        return;
    }

    if (const auto* component = dynamic_cast<const ComponentDeclStmt*>(&stmt)) {
        appendLine(output, indent, "ComponentDecl " + component->name.lexeme);
        appendLine(output, indent + 1, component->source);
        return;
    }

    if (const auto* importStmt = dynamic_cast<const ImportStmt*>(&stmt)) {
        appendLine(output, indent, "Import");
        printExpr(*importStmt->path, indent + 1, output);
        return;
    }

    if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        appendLine(output, indent, "Return");
        if (ret->value) {
            printExpr(*ret->value, indent + 1, output);
        }
        return;
    }

    if (dynamic_cast<const BreakStmt*>(&stmt) != nullptr) {
        appendLine(output, indent, "Break");
        return;
    }

    if (dynamic_cast<const ContinueStmt*>(&stmt) != nullptr) {
        appendLine(output, indent, "Continue");
    }
}

void AstPrinter::printExpr(const Expr& expr, int indent, std::string& output) const {
    if (const auto* literal = dynamic_cast<const LiteralExpr*>(&expr)) {
        appendLine(output, indent, "Literal " + literal->value.toString());
        return;
    }

    if (const auto* variable = dynamic_cast<const VariableExpr*>(&expr)) {
        appendLine(output, indent, "Variable " + variable->name.lexeme);
        return;
    }

    if (const auto* assign = dynamic_cast<const AssignExpr*>(&expr)) {
        appendLine(output, indent, "Assign " + assign->name.lexeme);
        printExpr(*assign->value, indent + 1, output);
        return;
    }

    if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
        appendLine(output, indent, "Unary " + unary->op.lexeme);
        printExpr(*unary->right, indent + 1, output);
        return;
    }

    if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
        appendLine(output, indent, "Binary " + binary->op.lexeme);
        printExpr(*binary->left, indent + 1, output);
        printExpr(*binary->right, indent + 1, output);
        return;
    }

    if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expr)) {
        appendLine(output, indent, "Grouping");
        printExpr(*grouping->expression, indent + 1, output);
        return;
    }

    if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
        appendLine(output, indent, "Call");
        printExpr(*call->callee, indent + 1, output);
        for (const auto& argument : call->arguments) {
            printExpr(*argument, indent + 1, output);
        }
        return;
    }

    if (const auto* array = dynamic_cast<const ArrayExpr*>(&expr)) {
        appendLine(output, indent, "Array");
        for (const auto& element : array->elements) {
            printExpr(*element, indent + 1, output);
        }
        return;
    }

    if (const auto* object = dynamic_cast<const ObjectExpr*>(&expr)) {
        appendLine(output, indent, "Object");
        for (const auto& field : object->fields) {
            appendLine(output, indent + 1, "Field " + field.key.lexeme);
            printExpr(*field.value, indent + 2, output);
        }
        return;
    }

    if (const auto* html = dynamic_cast<const HtmlExpr*>(&expr)) {
        appendLine(output, indent, "Html");
        appendLine(output, indent + 1, html->source);
        return;
    }

    if (const auto* get = dynamic_cast<const GetExpr*>(&expr)) {
        appendLine(output, indent, "Get " + get->name.lexeme);
        printExpr(*get->object, indent + 1, output);
        return;
    }

    if (const auto* index = dynamic_cast<const IndexExpr*>(&expr)) {
        appendLine(output, indent, "Index");
        printExpr(*index->object, indent + 1, output);
        printExpr(*index->index, indent + 1, output);
    }
}

void AstPrinter::appendLine(std::string& output, int indent, const std::string& text) {
    output.append(static_cast<std::size_t>(indent) * 2U, ' ');
    output += text;
    output.push_back('\n');
}

}  // namespace wevoaweb
