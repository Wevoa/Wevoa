# Quick Start

This guide gets you from source checkout to a running WevoaWeb app.

## Fastest Path After Install

If WevoaWeb is already installed globally, the fastest path is:

```text
wevoa --version
wevoa create app
cd app
wevoa start
```

Expected version output example:

```text
WevoaWeb Runtime 786.0.0
```

What each step does:

- `wevoa --version` confirms the global runtime is available
- `wevoa create app` scaffolds a new starter project
- `cd app` enters the generated project folder
- `wevoa start` launches the development server

If you do not have WevoaWeb installed yet, use one of the build or install paths below.

## 1. Build the CLI

### Windows with MinGW

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic -I. main.cpp `
    cli/cli_handler.cpp `
    cli/build_pipeline.cpp `
    cli/migration_manager.cpp `
    cli/package_manager.cpp `
    cli/project_inspector.cpp `
    cli/project_creator.cpp `
    lexer/lexer.cpp parser/parser.cpp ast/ast.cpp `
    interpreter/value.cpp interpreter/environment.cpp `
    interpreter/callable.cpp interpreter/interpreter.cpp interpreter/route.cpp `
    runtime/builtins.cpp runtime/ast_printer.cpp runtime/session.cpp `
    runtime/template_engine.cpp runtime/config_loader.cpp runtime/sqlite_module.cpp `
    server/http_types.cpp server/web_app.cpp server/http_server.cpp server/dev_server.cpp server/serve_server.cpp server/session_store.cpp `
    watcher/file_watcher.cpp `
    utils/logger.cpp utils/keyboard.cpp utils/file_writer.cpp utils/project_layout.cpp utils/browser_launcher.cpp utils/security.cpp `
    -o wevoa.exe -lws2_32 -lsqlite3
```

Or use the release script:

```powershell
.\scripts\build-release.ps1
```

### Linux / macOS

```bash
g++ -std=c++17 -Wall -Wextra -pedantic -I. main.cpp \
    cli/cli_handler.cpp \
    cli/build_pipeline.cpp \
    cli/migration_manager.cpp \
    cli/package_manager.cpp \
    cli/project_inspector.cpp \
    cli/project_creator.cpp \
    lexer/lexer.cpp parser/parser.cpp ast/ast.cpp \
    interpreter/value.cpp interpreter/environment.cpp \
    interpreter/callable.cpp interpreter/interpreter.cpp interpreter/route.cpp \
    runtime/builtins.cpp runtime/ast_printer.cpp runtime/session.cpp \
    runtime/template_engine.cpp runtime/config_loader.cpp runtime/sqlite_module.cpp \
    server/http_types.cpp server/web_app.cpp server/http_server.cpp server/dev_server.cpp server/serve_server.cpp server/session_store.cpp \
    watcher/file_watcher.cpp \
    utils/logger.cpp utils/keyboard.cpp utils/file_writer.cpp utils/project_layout.cpp utils/browser_launcher.cpp utils/security.cpp \
    -o wevoa -lsqlite3
```

Or use the release script:

```bash
./scripts/build-release.sh
```

## 2. Create a Project

```powershell
.\wevoa.exe create my-app
cd .\my-app
```

## 3. Start the Dev Server

```powershell
..\wevoa.exe start
```

Open:

```text
http://localhost:3000
```

## 4. Edit Backend and Frontend

Generated projects use:

- `app/` for backend route code
- `views/` for templates and layouts
- `public/` for static assets

Routes now live in `app/main.wev`.

`app/main.wev`

```text
const site = {
  title: "Welcome to WevoaWeb",
  subtitle: "Build web apps without JavaScript"
}

route "/" {
return view("index.wev", {
  title: "WevoaWeb",
  site: site
})
}

route "/docs" {
return view("docs.wev", {
  title: "Documentation",
  site: site,
  sections: []
})
}
```

`views/index.wev`

```text
extend "layout.wev"

section content {
<section class="hero">
  <h1>{{ site.title }}</h1>
  <p>{{ site.subtitle }}</p>
</section>
}
```

## 5. Reload

While the dev server is running:

- press `R` to reload manually
- press `Q` to quit
- use `Ctrl+C` for graceful shutdown

The file watcher also reloads when files in `app/`, `views/`, or `public/` change.

## 6. Inspect and Build

```powershell
..\wevoa.exe doctor
..\wevoa.exe info --global
..\wevoa.exe make:migration create_users
..\wevoa.exe migrate
..\wevoa.exe install my-local-package
..\wevoa.exe build
..\wevoa.exe serve
```

## 6. Build And Serve

From the project folder:

```powershell
..\wevoa.exe build
..\wevoa.exe serve
```

`build/` will contain the production app bundle, a production config file, and a `wevoa.build.json` manifest.

## 7. Install The Runtime

Windows:

```powershell
.\scripts\install-wevoa.ps1
```

Linux:

```bash
./scripts/install-wevoa.sh
```

After installation, open a new terminal and run:

```text
wevoa --version
```

If you want the full Windows product flow instead of the script-based install helper, build and run the GUI installer:

```powershell
.\scripts\build-installer.ps1
```

Installer output:

```text
dist\installer\WevoaSetup.exe
```
