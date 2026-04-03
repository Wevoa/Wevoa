# WevoaWeb Internal Platform Reference

This document is the current internal architecture and status reference for WevoaWeb. It is intended for engineers working on the runtime, CLI, build and packaging flow, installer path, and future ecosystem work.

It describes the platform as it exists now, not as an aspirational roadmap.

---

## 1. Executive Summary

### 1.1 What WevoaWeb Is

WevoaWeb is a vertically integrated web platform built in modern C++17. It combines:

- a custom scripting language
- a tree-walk interpreter
- a server-side template engine
- an HTTP server
- a developer CLI
- a build pipeline
- a Windows installer path

Those are not separate products layered together from outside. They are subsystems inside one runtime and one source tree.

### 1.2 Current Release Identity

Current version:

- `786.0.0`

Single source of truth:

- `utils/version.h`

That version is consumed by:

- `wevoa --version`
- startup logs
- build manifests
- Windows installer metadata

### 1.3 Current Maturity

WevoaWeb is no longer just a language experiment. It is now a functioning developer platform with:

- project generation
- dev server
- production build output
- production serve mode
- compiled template caching
- bounded worker-pool concurrency
- snapshot-based request startup
- cookie-backed sessions
- CSRF protection
- SQLite integration
- migrations
- a generated Windows installer

At the same time, it is still intentionally compact. It does not yet have a bytecode VM, async I/O, remote package registry, or broad database abstraction.

### 1.4 Practical Status

Today the platform is strongest for:

- server-rendered apps
- internal tools
- dashboards
- admin panels
- small to medium full-stack products
- language/runtime experimentation with a real deployment surface

The main constraints are:

- tree-walk execution cost
- SQLite write serialization under heavy write load
- no remote package ecosystem yet
- no serialized runtime snapshot artifact for instant startup reuse

---

## 2. Platform Identity and Mental Model

### 2.1 Core Definition

WevoaWeb is a server-first application platform where route logic, template rendering, static asset serving, packaging, and runtime hosting are all operated through the same executable.

The design goal is to remove the usual fragmentation where developers must separately think about:

- language runtime
- HTTP framework
- template system
- project generator
- migration workflow
- build tool
- deployment launcher

Instead, WevoaWeb exposes one control surface:

- `wevoa`

### 2.2 Developer Mental Model

A developer using WevoaWeb thinks in terms of one project with a conventional structure:

- `app/` for route logic and backend behavior
- `views/` for templates and layout composition
- `public/` for static files
- `storage/` for runtime data such as SQLite files and uploads
- `migrations/` for SQL migrations
- `packages/` for local installed packages
- `wevoa.config.json` for runtime configuration

The standard workflow is:

1. create a project with `wevoa create`
2. define routes in `app/`
3. define layouts and templates in `views/`
4. run `wevoa start`
5. build with `wevoa build`
6. serve production output with `wevoa serve`

### 2.3 Backend and Frontend Relationship

WevoaWeb does not split the app into a browser SPA runtime and a separate JSON API by default. The normal path is:

- route code executes on the server
- templates render on the server
- the browser receives HTML and static assets

The backend/frontend split is therefore:

- backend logic in Wevoa source
- frontend presentation in Wevoa templates

but both are executed inside one runtime process.

---

## 3. Repository and Runtime Structure

### 3.1 Core Source Layout

The main platform source tree is organized into these subsystems:

- `lexer/`
- `parser/`
- `ast/`
- `interpreter/`
- `runtime/`
- `server/`
- `cli/`
- `watcher/`
- `utils/`
- `scripts/`
- `installer/`
- `docs/`

### 3.2 Role of Each Major Subsystem

- `lexer/`: converts raw source text into tokens with source spans
- `parser/`: turns tokens into AST nodes
- `ast/`: defines program structure types
- `interpreter/`: evaluates the language and owns runtime semantics
- `runtime/`: session management, template engine, config loading, SQLite bridge, built-ins
- `server/`: HTTP parsing, request dispatch, session store, dev/prod server runners
- `cli/`: command parsing and platform commands
- `watcher/`: file change detection for dev mode
- `utils/`: logging, versioning, filesystem helpers, browser launch, security helpers
- `scripts/`: release and installer build scripts
- `installer/`: Inno Setup installer definition

### 3.3 Runtime Artifact Layout

Current important artifacts are:

- repo runtime binary: `wevoa.exe`
- release runtime binary: `dist/windows/wevoa.exe`
- Windows GUI installer: `dist/installer/WevoaSetup.exe`

---

## 4. High-Level System Diagram

```text
Developer
  -> wevoa CLI
     -> project creation / build / serve / diagnostics

Browser or API Client
  -> HTTP server
     -> request parser
     -> static asset short-circuit
     -> route matcher
     -> request runtime clone
     -> middleware
     -> route execution
     -> template engine / response helper
     -> response normalization
     -> HTTP response

App Source
  -> lexer
  -> parser
  -> AST
  -> runtime session
  -> startup snapshot
  -> per-request isolated execution

Templates
  -> compile to template node trees
  -> cache with LRU limits
  -> render to HTML

SQLite
  -> thread-local connection per worker
  -> query/exec/scalar
  -> arrays / objects back into Wevoa values
```

---

## 5. Current Platform Status by Area

### 5.1 Stable and Usable Now

- `wevoa create`, `start`, `build`, `serve`, `doctor`, `info`, `migrate`, `make:migration`, `install`, `--version`
- HTTP `GET`, `POST`, `PUT`, `PATCH`, `DELETE`
- dynamic route parameters
- middleware
- response helpers
- request helpers for query, form, JSON, files, method, and CSRF
- sessions and CSRF enforcement
- compiled template cache
- file includes, layouts, sections, loops, conditions, components
- bounded worker-pool HTTP serving
- queue backpressure with `503`
- SQLite access with per-thread connections and WAL mode
- production build manifests
- Windows installer generation

### 5.2 Working but Still Early

- package system
- migration system
- component model
- installer polish beyond the current GUI flow
- performance metrics

### 5.3 Not Yet Present

- bytecode VM
- async server
- remote package registry
- multiple built-in database backends
- serialized runtime snapshot restore
- ORM / query builder

---

## 6. Language System

### 6.1 Language Role

The Wevoa language is the behavioral core of the platform. Route definitions, helpers, middleware, and components are written in it.

### 6.2 Supported Syntax

The language currently supports:

- `let`
- `const`
- `func`
- `route`
- `component`
- `if / else`
- `loop (...)`
- `while`
- `return`
- `break`
- `continue`
- `import`
- `html { ... }`

It also supports:

- arrays
- objects
- indexing
- property access
- function calls
- comments with `//`

### 6.3 Runtime Value System

Runtime values are represented through `Value`, which is backed by `std::variant`.

Current value categories:

- `nil`
- integer
- string
- boolean
- callable
- array
- object

Arrays and objects are heap-backed and shared through smart pointers inside the value wrapper.

### 6.4 Scoping and Closures

WevoaWeb uses lexical scoping with chained environments.

Important implications:

- globals live in a root environment
- block scopes create child environments
- functions capture their declaration environment
- routes capture their declaration environment
- constants are protected from reassignment

User-defined functions inherit active request context when called during route execution, so route helpers can access:

- `request`
- `params`
- `session`

without manually threading those values everywhere.

### 6.5 Imports

Imports are runtime-executed, session-scoped composition.

Import behavior:

- relative imports resolve from the importing file
- package imports beginning with `@` resolve from `packages/`
- imports are normalized and deduplicated per runtime session

This means imports expand one shared runtime world rather than creating isolated module processes.

---

## 7. Language Execution Pipeline

### 7.1 Source Flow

The source pipeline is:

```text
source
  -> lexer
  -> token stream
  -> parser
  -> AST
  -> runtime session ownership
  -> interpreter execution
```

### 7.2 Lexer

The lexer:

- normalizes line endings
- tracks line and column
- emits explicit tokens
- recognizes keywords, identifiers, strings, integers, punctuation, and operators

The lexer does not apply semantic meaning. It produces structured input for later stages.

### 7.3 Parser

The parser is recursive descent with precedence-aware expression parsing.

It is responsible for:

- top-level declarations
- route syntax
- middleware clauses
- component declarations
- control flow
- object and array literals
- operator precedence
- `html { ... }` source capture

### 7.4 AST Ownership

AST nodes are allocated with `std::unique_ptr`.

Programs are retained by `RuntimeSession` so that:

- route declarations
- function declarations
- closures

can safely keep references to AST-backed behavior for the lifetime of the running application.

### 7.5 Interpreter

The interpreter is a tree-walk evaluator.

That means:

- behavior is easy to extend
- execution remains readable and debuggable
- runtime cost is higher than a VM or compiled backend

The interpreter is responsible for:

- evaluating expressions
- executing statements
- managing scopes
- registering built-ins
- registering routes
- registering components
- carrying request and session context
- returning values for response normalization

---

## 8. Runtime Session and Snapshot Model

### 8.1 Why `RuntimeSession` Exists

`RuntimeSession` is the orchestration layer between source parsing and runtime execution.

It owns:

- parsed program graphs
- import state
- route registry
- component registry
- interpreter coordination
- source-aware diagnostics

### 8.2 Snapshot-Based Startup

The application is loaded once at startup.

During startup:

- source files are lexed and parsed
- routes are registered
- globals are initialized
- components are registered
- imports are resolved

That bootstrapped state is captured as a runtime snapshot.

### 8.3 Per-Request Isolation

Incoming requests do not replay all app source from scratch.

Instead:

- a request-local runtime clone is created from the application snapshot
- request-specific values are injected into that clone
- the route executes in that isolated runtime
- request context is cleared after the response

This is the platform's core isolation strategy.

Benefits:

- faster request startup than replaying all scripts
- no variable leakage between requests
- stable shared app bootstrap state

---

## 9. Template Engine

### 9.1 Role

The template engine turns views and inline HTML fragments into final HTML strings.

It is used by:

- `view("file.wev", data)`
- inline `html { ... }`
- component expansion

### 9.2 Rendering Model

Templates do not operate as plain string substitution. They are compiled into internal template node trees.

Compilation captures:

- literal text blocks
- interpolation expressions
- `if` nodes
- `for` nodes
- includes
- layout metadata
- section metadata
- component usage boundaries

### 9.3 Interpolation

`{{ ... }}` expressions are parsed once during template compilation and evaluated during rendering.

This avoids repeated lex/parse cost for the same templates during steady-state execution.

### 9.4 Directives

Supported directives:

- `{% if ... %}`
- `{% else %}`
- `{% for item in items %}`
- `{% end %}`
- `include "file.wev"`

These are handled by the template engine's own parser/compiler, not by the main language parser.

### 9.5 Layouts and Sections

File-level directives:

- `extend "layout.wev"`
- `section content { ... }`

Rendering order:

1. child template is compiled and rendered
2. sections are collected
3. a layout environment is created
4. section output is injected
5. the layout template renders around the child content

### 9.6 Components

Components are declared in backend Wevoa source and consumed from templates.

Current model:

- self-closing custom tags
- attribute binding into child template environments
- component source compilation and caching through the same fragment pipeline

### 9.7 Caching

Template compilation results are cached in memory.

There are two bounded caches:

- template cache for file views
- fragment cache for inline HTML and component bodies

Both caches use LRU-style eviction and configurable limits:

- `max_template_cache_size`
- `max_fragment_cache_size`

### 9.8 Development vs Production

Development mode:

- template cache invalidates on file change

Production mode:

- templates are preloaded at startup
- production requests render from compiled cache entries

Important nuance:

- templates are precompiled at startup, not serialized into a portable compiled bytecode format yet

---

## 10. Routing System

### 10.1 Route Declaration

Routes are language-level declarations, not external configuration.

Example shape:

```text
route "/users/:id/edit" method PUT middleware auth {
  ...
}
```

### 10.2 Registration

When interpreted, a route stores:

- method
- path pattern
- AST declaration
- captured lexical environment
- source name for diagnostics

### 10.3 Matching

Current matching is segment-based and deterministic:

1. compare HTTP method
2. split route pattern and request path
3. require equal segment count
4. match static segments exactly
5. collect `:param` segments into `params`

Multiple parameters are supported naturally.

### 10.4 Middleware

Middleware names are resolved as ordinary runtime callables.

Middleware contract:

- `nil` -> continue
- `true` -> continue
- any other value -> short-circuit and return as the response

This makes redirect-based auth middleware and guard logic simple without a separate middleware object model.

---

## 11. HTTP Server Architecture

### 11.1 Listener Model

The HTTP server owns:

- one listening socket
- a stoppable accept loop
- a bounded request queue
- a fixed-size worker pool

The accept loop remains simple and portable. Worker threads provide concurrency.

### 11.2 Queue Control

Queue size is configurable via:

- `max_queue_size`

If the queue is full:

- the request is rejected immediately
- the server returns `503 Service Unavailable`
- a warning is logged

This is one of the main production hardening steps because it prevents unbounded growth under overload.

### 11.3 Request Parsing

The request parser turns bytes into `HttpRequest`.

Parsed fields include:

- method
- target
- path
- version
- headers
- raw body
- query parameters
- form parameters
- uploaded files
- parsed JSON body
- JSON parse error

Supported body handling:

- `application/x-www-form-urlencoded`
- `application/json`
- `multipart/form-data`

### 11.4 Static File Serving

`GET` requests check `public/` first.

Static serving is:

- path-normalized
- traversal-protected
- content-type aware for common file types

Static asset hits bypass route execution.

### 11.5 Request Lifecycle

The core runtime lifecycle is:

```text
socket accepted
  -> request parsed
  -> static asset check
  -> route match
  -> request runtime clone
  -> session + params + request injection
  -> CSRF gate for mutating methods
  -> middleware
  -> route execution
  -> template/render helper execution
  -> response normalization
  -> response serialization
  -> socket close
```

### 11.6 Supported Methods

Current supported methods:

- `GET`
- `POST`
- `PUT`
- `PATCH`
- `DELETE`

Unsupported methods return `405 Method Not Allowed`.

---

## 12. Request Context, Sessions, and Security

### 12.1 Request Object

Routes receive a request object that exposes:

- `request.method`
- `request.method()`
- `request.path`
- `request.target`
- `request.body`
- `request.query(key)`
- `request.form(key)`
- `request.json()`
- `request.csrf()`
- `request.file(name)`

It also carries object-backed maps for:

- query data
- form data
- JSON data
- uploaded files

### 12.2 Sessions

Sessions are:

- cookie-backed
- stored in memory
- protected by a mutex
- cleaned up by a background TTL thread
- bounded by a maximum session count

Current session API:

- `session.get(key)`
- `session.set(key, value)`
- `session.delete(key)`

### 12.3 Session Safety

Session control features now include:

- `session_ttl`
- cleanup interval
- `max_session_count`
- LRU-style eviction when over limit

### 12.4 CSRF Protection

The platform now applies automatic CSRF checks to:

- `POST`
- `PUT`
- `PATCH`
- `DELETE`

CSRF token flow:

- session gets a generated token
- route renders the token into forms or returns it for API clients
- request token is checked before route execution

By default, cookies are:

- `HttpOnly`
- `SameSite=Lax`

`Secure` is configurable and not forced by default, which keeps local HTTP development and installer verification predictable.

### 12.5 Password Helpers

Built-in helpers:

- `hash(password)`
- `verify(password, digest)`

These exist as runtime helpers, not as a separate auth subsystem.

---

## 13. Response System

### 13.1 Response Normalization

Route results are normalized into `HttpResponse`.

A route may return:

- plain value -> HTML/text response body
- `json(...)` -> JSON response
- `redirect(...)` -> redirect response
- `status(...)` -> explicit status/content-type response

### 13.2 Response Headers

The interpreter can accumulate response headers during execution, and the HTTP layer merges them into the final normalized response.

This is how:

- cookies
- redirects
- content-type changes

flow out of route execution into the actual HTTP response.

---

## 14. Database System

### 14.1 SQLite as a Runtime Module

Database access is exposed as a runtime module:

- `sqlite.open(path)`

This keeps SQL integration out of the language grammar and inside the runtime integration layer.

### 14.2 Connection Strategy

SQLite access is now worker-safe.

Current model:

- one logical database object in Wevoa code
- one SQLite connection per worker thread for that database path
- no shared SQLite connection across threads

This is critical for correctness because SQLite is not meant to share a single live handle across concurrent worker threads.

### 14.3 WAL and Contention Handling

Opened connections enable:

- `PRAGMA journal_mode = WAL`
- `PRAGMA synchronous = NORMAL`
- busy timeout

This improves concurrency and reduces avoidable write contention failures.

### 14.4 Query Surface

Current DB methods:

- `db.exec(sql[, params])`
- `db.query(sql[, params])`
- `db.scalar(sql[, params])`

### 14.5 Parameter Binding

Supported parameter forms:

- array for positional bindings
- object for named bindings
- `nil`

### 14.6 Result Mapping

Rows become arrays of objects:

- result set -> array
- row -> object
- column -> field

This matches the native Wevoa runtime model and makes views/routes consume SQL results naturally.

### 14.7 Database Metrics

Database execution time is accumulated per request and exposed through runtime logging alongside:

- total request time
- route time
- template time
- DB time

---

## 15. CLI Architecture

### 15.1 Role

The CLI is the operating control plane of the platform.

All major workflows are entered through `main()` and then dispatched into subsystems.

### 15.2 Supported Commands

Current command surface:

- `wevoa create`
- `wevoa start`
- `wevoa build`
- `wevoa serve`
- `wevoa doctor`
- `wevoa info`
- `wevoa migrate`
- `wevoa make:migration`
- `wevoa install`
- `wevoa --version`
- `wevoa help`

### 15.3 Command Roles

- `create`: scaffold a new project
- `start`: development runtime with watcher and reload
- `build`: validated production output
- `serve`: run the built app
- `doctor`: inspect structure and health
- `info`: show project/runtime metadata
- `migrate`: apply pending SQL migrations
- `make:migration`: create timestamped migration files
- `install`: install a local package into `packages/`

### 15.4 Version Path

`wevoa --version` reads from `utils/version.h`, which is also used by build artifacts and the installer build script.

---

## 16. Build and Production Output

### 16.1 What `wevoa build` Actually Does

`wevoa build` is a packaging-and-validation pipeline, not a compiler.

It currently does this:

- detect current project layout
- create `build/`
- copy `app/`, `views/`, and `public/`
- force production environment settings
- load the built app through the real runtime
- validate routes and templates
- preload template compilation metadata
- emit manifests

### 16.2 Build Outputs

Current outputs include:

- `build/wevoa.config.json`
- `build/wevoa.build.json`
- `build/wevoa.snapshot.json`
- `build/wevoa.templates.json`

### 16.3 Manifest Roles

`wevoa.build.json`:

- runtime name
- version
- mode
- routes
- views
- assets

`wevoa.snapshot.json`:

- runtime preload summary
- route count
- view count
- component count
- inline template count
- queue/cache/session config

`wevoa.templates.json`:

- template metadata
- dependency sets
- layouts
- section counts
- node counts

Important nuance:

- build emits metadata about the startup snapshot and compiled templates
- it does not yet emit a reusable serialized runtime snapshot that `serve` can memory-map or restore directly

### 16.4 `wevoa serve`

Production serve mode:

- loads the built layout
- forces production mode
- preloads templates at startup
- uses worker threads and bounded queue settings from config
- serves without file watching

---

## 17. Configuration Surface

### 17.1 Important Runtime Config Keys

Current important config values include:

- `port`
- `env`
- `worker_threads`
- `max_queue_size`
- `max_template_cache_size`
- `max_fragment_cache_size`
- `session_ttl`
- `max_session_count`
- `performance_metrics`
- `open_browser`
- `database.path`
- `secure_cookies`

### 17.2 How Config Is Used

- dev server reads config and overlays `env = dev`
- serve mode reads built config and overlays `env = production`
- worker pool size and queue bounds come from config
- template caches and session limits come from config
- DB path is read from config for migrations and app logic

---

## 18. Logging, Diagnostics, and Errors

### 18.1 Logging

Current log categories include:

- `[Wevoa]`
- `[INFO]`
- `[WARN]`
- `[ERROR]`
- `[ROUTE]`
- `[BUILD]`
- `[PERF]`

### 18.2 Startup Summary

`wevoa start` and `wevoa serve` now report:

- environment mode
- loaded route count
- loaded view count
- worker threads
- queue limit
- template cache size
- fragment cache size
- session TTL
- max sessions

### 18.3 Error Formatting

Runtime errors now try to preserve:

- route path
- source file
- source line
- message

The goal is that failures surface as operational diagnostics rather than vague process-level crashes.

### 18.4 Metrics

Optional performance metrics currently track:

- total request time
- route execution time
- template rendering time
- database time

Shutdown summaries can also emit aggregate request timing data.

---

## 19. Windows Distribution and Installer

### 19.1 Runtime Distribution

The platform is distributable as a single runtime executable.

Primary Windows runtime paths:

- `wevoa.exe`
- `dist/windows/wevoa.exe`

### 19.2 Installer Path

The Windows GUI installer is built with Inno Setup.

Source definition:

- `installer/WevoaSetup.iss`

Build entry:

- `scripts/build-installer.ps1`

Output:

- `dist/installer/WevoaSetup.exe`

### 19.3 Installer Responsibilities

The installer currently handles:

- copying `wevoa.exe`
- optional PATH integration
- shortcuts
- uninstall support
- basic documentation exposure

Installer version metadata is driven from `utils/version.h` through the build script, so runtime and installer versions stay aligned.

---

## 20. Performance and Scalability Characteristics

### 20.1 What Has Been Improved

Major runtime improvements already in place:

- request startup from snapshots instead of source replay
- compiled template caches
- bounded worker-pool concurrency
- request queue backpressure
- worker-safe SQLite access
- WAL mode
- session cleanup and memory bounds

### 20.2 Current Bottlenecks

The biggest remaining runtime costs are:

- tree-walk interpretation
- dynamic value handling
- template evaluation and string assembly
- SQLite write serialization on write-heavy apps
- full app reload in dev mode

### 20.3 What This Means in Practice

WevoaWeb is now much more production-capable than an earlier single-threaded prototype, but it is still not optimized like:

- a bytecode VM
- an async reactor server
- a JIT runtime

Its current strength is disciplined simplicity with real operational features, not raw peak throughput.

---

## 21. Current Limitations

### 21.1 Critical Long-Term Gaps

- no bytecode VM
- no async network layer
- no remote package registry
- no rollback path in migrations
- no float type in the value system

### 21.2 Important Platform Gaps

- SQLite is the only built-in database backend
- package management is still local-only
- component model is intentionally small
- build output does not yet contain a reusable serialized runtime snapshot
- worker pool sizing is fixed rather than adaptive

### 21.3 Operational Limits

- sessions are in-memory only
- runtime state is process-local
- horizontal scaling requires external process management and sticky/session strategy
- no asset optimization or minification pipeline

---

## 22. Overall Status Assessment

### 22.1 What We Have Today

WevoaWeb today is a real, working platform with:

- a custom language
- a functioning runtime
- a compiled template system
- a bounded concurrent HTTP server
- route, session, CSRF, and DB integration
- a usable CLI
- a production build path
- a Windows installer

### 22.2 What Makes It Unique

Its uniqueness is not a single feature. It is the fact that the platform owns the full path from source file to HTTP response:

- source lexing
- parsing
- AST ownership
- runtime execution
- request context
- template rendering
- HTTP normalization
- CLI operations
- build output
- installer packaging

### 22.3 Current Bottom Line

WevoaWeb is now best described as a compact, vertically integrated web runtime and framework platform. It is no longer just a prototype, but it is also not pretending to be a mature VM-based ecosystem yet.

It is ready for:

- serious internal development
- framework evolution
- packaging and installer work
- developer onboarding
- further runtime optimization

It should still be treated as a growing platform rather than a finished mass-scale ecosystem.

---

## 23. Key Files to Read First

For engineers onboarding to the core system, the most important files are:

- `main.cpp`
- `utils/version.h`
- `cli/cli_handler.cpp`
- `cli/build_pipeline.cpp`
- `server/web_app.cpp`
- `server/http_server.cpp`
- `server/session_store.cpp`
- `runtime/session.cpp`
- `runtime/template_engine.cpp`
- `runtime/sqlite_module.cpp`
- `interpreter/interpreter.cpp`
- `interpreter/route.cpp`
- `installer/WevoaSetup.iss`

These files provide the fastest path to understanding how the runtime starts, loads apps, serves requests, packages builds, and distributes itself.
