#!/usr/bin/env bash
set -euo pipefail

OUTPUT_DIR="${1:-dist/linux}"
BINARY_NAME="${2:-wevoa}"
STATIC_RUNTIME="${STATIC_RUNTIME:-0}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${OUTPUT_DIR}" = /* ]]; then
  RESOLVED_OUTPUT_DIR="${OUTPUT_DIR}"
else
  RESOLVED_OUTPUT_DIR="${ROOT_DIR}/${OUTPUT_DIR}"
fi

mkdir -p "${RESOLVED_OUTPUT_DIR}"
OUTPUT_PATH="${RESOLVED_OUTPUT_DIR}/${BINARY_NAME}"

SOURCES=(
  "main.cpp"
  "cli/cli_handler.cpp"
  "cli/build_pipeline.cpp"
  "cli/migration_manager.cpp"
  "cli/package_manager.cpp"
  "cli/project_inspector.cpp"
  "cli/project_creator.cpp"
  "lexer/lexer.cpp"
  "parser/parser.cpp"
  "ast/ast.cpp"
  "interpreter/value.cpp"
  "interpreter/environment.cpp"
  "interpreter/callable.cpp"
  "interpreter/interpreter.cpp"
  "interpreter/route.cpp"
  "runtime/builtins.cpp"
  "runtime/ast_printer.cpp"
  "runtime/session.cpp"
  "runtime/template_engine.cpp"
  "runtime/config_loader.cpp"
  "runtime/sqlite_module.cpp"
  "server/http_types.cpp"
  "server/web_app.cpp"
  "server/http_server.cpp"
  "server/dev_server.cpp"
  "server/serve_server.cpp"
  "server/session_store.cpp"
  "watcher/file_watcher.cpp"
  "utils/logger.cpp"
  "utils/keyboard.cpp"
  "utils/file_writer.cpp"
  "utils/project_layout.cpp"
  "utils/browser_launcher.cpp"
  "utils/security.cpp"
)

ARGS=(
  -std=c++17
  -O2
  -DNDEBUG
  -Wall
  -Wextra
  -pedantic
  "-I${ROOT_DIR}"
)

if [[ "${STATIC_RUNTIME}" == "1" ]]; then
  ARGS+=(-static)
fi

for source in "${SOURCES[@]}"; do
  ARGS+=("${ROOT_DIR}/${source}")
done

ARGS+=(-o "${OUTPUT_PATH}" -lsqlite3)

echo "[Wevoa] Building Linux release binary..."
g++ "${ARGS[@]}"
chmod +x "${OUTPUT_PATH}"
echo "[Wevoa] Release build complete"
echo "[Wevoa] Output: ${OUTPUT_PATH}"
