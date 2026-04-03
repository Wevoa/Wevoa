param(
    [string]$OutputDir = "dist/windows",
    [string]$BinaryName = "wevoa.exe",
    [switch]$StaticRuntime
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$resolvedOutputDir = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    [System.IO.Path]::GetFullPath($OutputDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $OutputDir))
}
$binaryPath = Join-Path $resolvedOutputDir $BinaryName

New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

$sources = @(
    "main.cpp",
    "cli/cli_handler.cpp",
    "cli/build_pipeline.cpp",
    "cli/migration_manager.cpp",
    "cli/package_manager.cpp",
    "cli/project_inspector.cpp",
    "cli/project_creator.cpp",
    "lexer/lexer.cpp",
    "parser/parser.cpp",
    "ast/ast.cpp",
    "interpreter/value.cpp",
    "interpreter/environment.cpp",
    "interpreter/callable.cpp",
    "interpreter/interpreter.cpp",
    "interpreter/route.cpp",
    "runtime/builtins.cpp",
    "runtime/ast_printer.cpp",
    "runtime/session.cpp",
    "runtime/template_engine.cpp",
    "runtime/config_loader.cpp",
    "runtime/sqlite_module.cpp",
    "server/http_types.cpp",
    "server/web_app.cpp",
    "server/http_server.cpp",
    "server/dev_server.cpp",
    "server/serve_server.cpp",
    "server/session_store.cpp",
    "watcher/file_watcher.cpp",
    "utils/logger.cpp",
    "utils/keyboard.cpp",
    "utils/file_writer.cpp",
    "utils/project_layout.cpp",
    "utils/browser_launcher.cpp",
    "utils/security.cpp"
) | ForEach-Object { Join-Path $root $_ }

$arguments = @(
    "-std=c++17",
    "-O2",
    "-DNDEBUG",
    "-Wall",
    "-Wextra",
    "-pedantic",
    "-I$root"
)

if ($StaticRuntime) {
    $arguments += @("-static-libgcc", "-static-libstdc++")
}

$arguments += $sources
$arguments += @("-o", $binaryPath, "-lws2_32", "-lsqlite3")

Write-Host "[Wevoa] Building Windows release binary..."
& g++ @arguments

if ($LASTEXITCODE -ne 0) {
    throw "Release build failed."
}

Get-Item $binaryPath | Out-Null
Write-Host "[Wevoa] Release build complete"
Write-Host "[Wevoa] Output: $binaryPath"
