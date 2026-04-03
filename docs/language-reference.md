# Language Reference

This document describes the current WevoaWeb language and web runtime surface.

## Variables

Mutable variables:

```text
let count = 10
```

Constants:

```text
const title = "WevoaWeb"
```

## Data Types

Current runtime value types:

- `integer`
- `string`
- `boolean`
- `array`
- `object`
- `nil`
- callable values

## Expressions

Supported operators:

- `+`
- `-`
- `*`
- `/`
- `>`
- `<`
- `>=`
- `<=`
- `==`
- `!=`
- `&&`
- `||`
- unary `-`
- unary `!`

String concatenation works through `+` when either operand is a string.

Truthy and falsy behavior:

- `nil` is falsy
- `false` is falsy
- `0` is falsy
- `""` is falsy
- empty arrays and empty objects are falsy
- everything else is truthy

## Arrays

```text
let users = ["Ahad", "Ali"]
print(users[0])
```

## Objects

```text
let user = {
  name: "Ahad",
  age: 18
}

print(user.name)
print(user["name"])
```

## Blocks

```text
{
let name = "inner"
print(name)
}
```

Blocks create a new lexical scope.

## Conditionals

```text
if (count > 5) {
print("big")
} else {
print("small")
}
```

## Loops

Classic loop:

```text
loop (i = 0; i < 3; i = i + 1) {
print(i)
}
```

While loop:

```text
let index = 0

while (index < 3) {
print(index)
index = index + 1
}
```

Loop control:

```text
while (true) {
break
}
```

```text
loop (i = 0; i < 5; i = i + 1) {
if (i == 2) {
continue
}
print(i)
}
```

## Functions

```text
func greet(name) {
return "Hello " + name
}
```

Functions support:

- parameters
- return values
- lexical closures

## Imports

```text
import "shared.wev"
```

Imported files execute once per runtime session, even if requested multiple times.

Package imports resolve from `packages/` when the path starts with `@`.

```text
import "@shared-auth"
```

## Templates

### File-Based Templates

Use `view()` from a route or function:

```text
route "/" {
return view("index.wev", {
  title: "Users",
  users: ["Ahad", "Ali"]
})
}
```

`views/index.wev`

```text
<h1>{{ title }}</h1>
<p>{{ users[0] }}</p>
```

### Template Control Flow

Loop blocks:

```text
{% for item in items %}
<li>{{ item }}</li>
{% end %}
```

Conditional blocks:

```text
{% if condition %}
<p>True</p>
{% else %}
<p>False</p>
{% end %}
```

These blocks can be nested and work alongside normal `{{ expression }}` interpolation.

### Template Includes

Standalone includes:

```text
include "partials/nav.wev"
```

Included templates render in the same template environment as the parent file.

### Template Components

Declare reusable components in route source files:

```text
component card {
html {
<article class="card">
  <h2>{{ title }}</h2>
  <p>{{ body }}</p>
</article>
}
}
```

Use them inside templates as self-closing tags:

```text
<card title="Hello" body="Reusable markup" />
```

### Inline HTML Blocks

```text
route "/help" {
return html {
<h1>Hello {{ name + "!" }}</h1>
}
}
```

Inline `html { ... }` blocks render with the current scope and support `{{ expression }}` interpolation.

## Layouts

Use `extend` and `section` inside template files:

```text
extend "layout.wev"

section content {
<h1>Hello</h1>
}
```

The layout can render `{{ content }}` or any named section you define.

## Built-in Functions

### `print()`

Prints its arguments separated by spaces and ends with a newline.

```text
print("Hello", 10)
```

### `input()`

Reads a line from standard input.

```text
let name = input("Name: ")
```

### `view()`

Renders a template file from `views/`.

```text
return view("index.wev", {
  title: "Welcome"
})
```

### `len()`

Returns the size of a string, array, or object.

```text
print(len(["Ahad", "Ali"]))
```

### `append()`

Returns a new array with one extra element appended.

```text
let names = append(["Ahad"], "Ali")
```

### `hash()`

Hashes a password string for later verification.

```text
let digest = hash("secret-pass")
```

### `verify()`

Verifies a password against a stored hash.

```text
print(verify("secret-pass", digest))
```

### `verify_csrf()`

Compares a submitted token against the active request token.

```text
if (!verify_csrf(request.form("csrf_token"))) {
return status(403, "Forbidden")
}
```

### `json()`

Returns a JSON HTTP response.

```text
return json({ success: true })
```

### `redirect()`

Returns an HTTP redirect response.

```text
return redirect("/login")
```

### `status()`

Returns a response with an explicit status code.

```text
return status(404, "Not Found")
```

## SQLite

WevoaWeb includes a native SQLite module for lightweight SQL-backed apps.

Open a database:

```text
let db = sqlite.open(config.database)
```

Run a write query:

```text
db.exec("INSERT INTO users (name) VALUES (?)", ["Ahad"])
```

Run a row query:

```text
let rows = db.query("SELECT name FROM users ORDER BY id ASC LIMIT 3")
print(rows[0].name)
```

Read a single value:

```text
let total = db.scalar("SELECT COUNT(*) FROM users")
```

SQLite parameter rules:

- use arrays for positional `?` parameters
- use objects for named parameters like `:name`
- supported bound value types are `nil`, `integer`, `boolean`, and `string`

## Routes

Routes are a language-level web feature.

GET route:

```text
route "/" {
return "<h1>Home</h1>"
}
```

POST route:

```text
route "/submit" method POST {
let name = request.form("name")
return "<h1>" + name + "</h1>"
}
```

PUT / PATCH / DELETE routes:

```text
route "/users/:id" method PUT {
let payload = request.json()
return json({ id: params.id, name: payload.name })
}
```

Dynamic route parameters:

```text
route "/users/:id" {
return "<h1>" + params.id + "</h1>"
}
```

Route middleware:

```text
func auth() {
if (!session.get("signed_in")) {
return redirect("/login")
}

return true
}

route "/dashboard" middleware auth {
return view("dashboard.wev", {})
}
```

Rules:

- route paths must evaluate to strings
- route paths must start with `/`
- duplicate routes are rejected per method and path

## Request API

Routes receive a `request` object.

Available members:

- `request.method`
- `request.method()`
- `request.path`
- `request.target`
- `request.body`
- `request.query("key")`
- `request.form("key")`
- `request.json()`
- `request.csrf()`
- `request.query_data`
- `request.form_data`
- `request.json_data`
- `request.json_valid`
- `request.json_error`

## Sessions

Routes also receive a `session` object.

Available members:

- `session.get("key")`
- `session.set("key", value)`
- `session.delete("key")`

Current session storage is cookie-backed and stored in memory.

## Migrations

Create and apply SQL migrations from the project root:

```text
wevoa make:migration create_users
wevoa migrate
```

Migrations are stored in `migrations/` and tracked in SQLite through the `wevoa_migrations` table.

## Config

`wevoa.config.json` is parsed at startup and exposed as a global `config` object.

Example:

```text
print(config.env)
```

## Comments

Single-line comments:

```text
// this is a comment
```

## Source Diagnostics

The lexer, parser, template renderer, and interpreter attach source spans to errors using file names, line numbers, columns, and a source snippet when available.

## Package Installation

Install a local package into `packages/`:

```text
wevoa install ../shared-package
```

If the source does not exist yet, WevoaWeb creates a local placeholder package scaffold.

## Not Yet Supported

- floating-point numbers
- user-defined classes
- network package registry installs
- bytecode or JIT execution
