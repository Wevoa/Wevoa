#include "cli/project_creator.h"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wevoaweb {

namespace {

std::string humanizeProjectName(const std::filesystem::path& projectPath) {
    const std::string rawName = projectPath.filename().string();
    if (rawName.empty()) {
        return "WevoaWeb App";
    }

    std::string displayName;
    displayName.reserve(rawName.size());

    bool capitalizeNext = true;
    for (const char ch : rawName) {
        if (ch == '-' || ch == '_' || ch == ' ') {
            if (!displayName.empty() && displayName.back() != ' ') {
                displayName.push_back(' ');
            }
            capitalizeNext = true;
            continue;
        }

        const auto unsignedChar = static_cast<unsigned char>(ch);
        displayName.push_back(static_cast<char>(capitalizeNext ? std::toupper(unsignedChar)
                                                               : std::tolower(unsignedChar)));
        capitalizeNext = false;
    }

    return displayName.empty() ? "WevoaWeb App" : displayName;
}

std::string escapeForQuotedLiteral(const std::string& value) {
    std::ostringstream escaped;
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped << "\\\\";
                break;
            case '"':
                escaped << "\\\"";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                escaped << ch;
                break;
        }
    }

    return escaped.str();
}

}  // namespace

ProjectCreator::ProjectCreator(FileWriter writer) : writer_(std::move(writer)) {}

std::filesystem::path ProjectCreator::createProject(const std::string& projectName) const {
    const auto targetPath = resolveTargetPath(projectName);
    const std::string displayName = humanizeProjectName(targetPath);

    std::error_code error;
    if (std::filesystem::exists(targetPath, error)) {
        if (error) {
            throw std::runtime_error("Failed to inspect target path: " + targetPath.string());
        }

        throw std::runtime_error("Project directory already exists: " + targetPath.string());
    }

    writer_.createDirectory(targetPath / "app");
    writer_.createDirectory(targetPath / "views");
    writer_.createDirectory(targetPath / "views" / "partials");
    writer_.createDirectory(targetPath / "public");
    writer_.createDirectory(targetPath / "storage");
    writer_.createDirectory(targetPath / "migrations");
    writer_.createDirectory(targetPath / "packages");

    writer_.writeTextFile(targetPath / "app" / "main.wev", mainTemplate(displayName));
    writer_.writeTextFile(targetPath / "views" / "layout.wev", layoutTemplate(displayName));
    writer_.writeTextFile(targetPath / "views" / "index.wev", indexTemplate());
    writer_.writeTextFile(targetPath / "views" / "docs.wev", docsTemplate());
    writer_.writeTextFile(targetPath / "views" / "contact.wev", contactTemplate());
    writer_.writeTextFile(targetPath / "views" / "partials" / "nav.wev", navTemplate());
    writer_.writeTextFile(targetPath / "public" / "style.css", styleTemplate());
    writer_.writeTextFile(targetPath / "wevoa.config.json", configTemplate(displayName));
    writer_.writeTextFile(targetPath / "README.md", readmeTemplate(displayName));
    writer_.writeTextFile(targetPath / "storage" / ".gitkeep", "");

    return targetPath;
}

std::filesystem::path ProjectCreator::resolveTargetPath(const std::string& projectName) const {
    if (projectName.empty()) {
        throw std::runtime_error("Project name must not be empty.");
    }

    const std::filesystem::path projectPath(projectName);
    if (projectPath.has_root_path()) {
        throw std::runtime_error("Project name must be a relative folder name.");
    }

    return std::filesystem::current_path() / projectPath;
}

std::string ProjectCreator::layoutTemplate(const std::string& displayName) {
    const std::string escapedName = escapeForQuotedLiteral(displayName);

    return "<!DOCTYPE html>\n"
           "<html lang=\"en\">\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "  <title>{{ title }}</title>\n"
           "  <link rel=\"stylesheet\" href=\"/style.css\">\n"
           "</head>\n"
           "<body>\n"
           "  <div class=\"shell\">\n"
           "    <header class=\"topbar\">\n"
           "      <a class=\"brand\" href=\"/\">" + escapedName +
           "</a>\n"
           "      include \"partials/nav.wev\"\n"
           "    </header>\n"
           "    <main class=\"frame\">\n"
           "      {{ content }}\n"
           "    </main>\n"
           "  </div>\n"
           "</body>\n"
           "</html>\n";
}

std::string ProjectCreator::mainTemplate(const std::string& displayName) {
    const std::string escapedName = escapeForQuotedLiteral(displayName);

    return "const site = {\n"
           "  project_name: \"" +
           escapedName +
           "\",\n"
           "  framework_name: \"WevoaWeb\",\n"
           "  title: \"Welcome to WevoaWeb\",\n"
           "  subtitle: \"Build web apps without JavaScript\",\n"
           "  description: \"A server-side frontend framework with its own language\"\n"
           "}\n"
           "\n"
           "const docs_sections = [\n"
           "  { title: \"Getting Started\", body: \"Open app/main.wev to define routes and views/index.wev to shape your first screen.\" },\n"
           "  { title: \"Routing\", body: \"Use route \\\"/path\\\" { ... } for GET or route \\\"/path\\\" method POST { ... }.\" },\n"
           "  { title: \"Templates\", body: \"Render templates with view(), add {{ values }}, {% if %}, {% for %}, extend, section, and include.\" },\n"
           "  { title: \"Database\", body: \"Open SQLite with sqlite.open() and read rows back as arrays of objects.\" },\n"
           "  { title: \"CLI\", body: \"Use wevoa start for dev mode, wevoa build for production output, and wevoa serve to run the build.\" }\n"
           "]\n"
           "\n"
           "route \"/\" {\n"
           "return view(\"index.wev\", {\n"
           "  title: site.framework_name,\n"
           "  site: site\n"
           "})\n"
           "}\n"
           "\n"
           "route \"/docs\" {\n"
           "return view(\"docs.wev\", {\n"
           "  title: \"Documentation\",\n"
           "  site: site,\n"
           "  sections: docs_sections\n"
           "})\n"
           "}\n"
           "\n"
           "route \"/contact\" {\n"
           "return view(\"contact.wev\", {\n"
           "  title: \"Contact\",\n"
           "  site: site,\n"
           "  csrf: request.csrf(),\n"
           "  submitted: false,\n"
           "  name: \"\",\n"
           "  email: \"\",\n"
           "  message: \"\"\n"
           "})\n"
           "}\n"
           "\n"
           "route \"/contact\" method POST {\n"
           "return view(\"contact.wev\", {\n"
           "  title: \"Contact\",\n"
           "  site: site,\n"
           "  csrf: request.csrf(),\n"
           "  submitted: true,\n"
           "  name: request.form(\"name\"),\n"
           "  email: request.form(\"email\"),\n"
           "  message: request.form(\"message\")\n"
           "})\n"
           "}\n"
           "\n"
           "route \"/hello/:name\" {\n"
           "return status(200, \"<h1>Hello \" + params.name + \"</h1>\")\n"
           "}\n";
}

std::string ProjectCreator::indexTemplate() {
    return R"WEV(extend "layout.wev"

section content {
<section class="hero">
  <div class="eyebrow">Developer-ready runtime</div>
  <h1>{{ site.title }}</h1>
  <p>{{ site.subtitle }}</p>
  <p class="hero-copy">{{ site.description }}</p>

  <div class="actions">
    <a class="button button-primary" href="/docs">Documentation</a>
    <a class="button" href="/contact">Example Form</a>
  </div>

  <div class="box-grid">
    <article class="box">
      <span class="box-label">Route</span>
      <code>route "/hello/:name"</code>
    </article>
    <article class="box">
      <span class="box-label">Template</span>
      <code>include "partials/nav.wev"</code>
    </article>
    <article class="box">
      <span class="box-label">Build</span>
      <code>wevoa build</code>
    </article>
  </div>
</section>
}
)WEV";
}

std::string ProjectCreator::docsTemplate() {
    return R"WEV(extend "layout.wev"

section content {
<section class="docs-grid">
  <aside class="docs-sidebar">
    <h2>Wevoa Docs</h2>
    <p>Core topics for your first app.</p>
  </aside>

  <div class="docs-content">
    {% for section in sections %}
    <article class="box docs-box">
      <h3>{{ section.title }}</h3>
      <p>{{ section.body }}</p>
    </article>
    {% end %}
  </div>
</section>
}
)WEV";
}

std::string ProjectCreator::contactTemplate() {
    return R"WEV(extend "layout.wev"

section content {
<section class="form-shell">
  <div class="form-intro">
    <div class="eyebrow">Example form</div>
    <h1>Contact Route</h1>
    <p>Submit this form to see POST handling inside your WevoaWeb starter.</p>
  </div>

  <form class="contact-form" method="POST" action="/contact">
    <input type="hidden" name="_csrf" value="{{ csrf }}">
    <label>
      <span>Name</span>
      <input type="text" name="name" placeholder="Your name">
    </label>
    <label>
      <span>Email</span>
      <input type="email" name="email" placeholder="Your email">
    </label>
    <label>
      <span>Message</span>
      <textarea name="message" placeholder="Tell us what you are building"></textarea>
    </label>
    <button class="button button-primary" type="submit">Send</button>
  </form>

  {% if submitted %}
  <article class="box submission-box">
    <h2>Latest submission</h2>
    <p><strong>Name:</strong> {{ name }}</p>
    <p><strong>Email:</strong> {{ email }}</p>
    <p><strong>Message:</strong> {{ message }}</p>
  </article>
  {% end %}
</section>
}
)WEV";
}

std::string ProjectCreator::navTemplate() {
    return R"WEV(<nav class="nav">
  <a href="/">Home</a>
  <a href="/docs">Docs</a>
  <a href="/contact">Contact</a>
</nav>
)WEV";
}

std::string ProjectCreator::styleTemplate() {
    return R"WEV(:root {
  color-scheme: light;
  --page: #ffffff;
  --page-soft: #f5faf6;
  --line: rgba(15, 23, 42, 0.12);
  --line-strong: rgba(15, 23, 42, 0.2);
  --text: #0f172a;
  --muted: #4b5563;
  --accent: rgba(34, 197, 94, 0.18);
  --accent-strong: #22c55e;
}

* {
  box-sizing: border-box;
}

html,
body {
  min-height: 100%;
}

body {
  margin: 0;
  font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif;
  color: var(--text);
  background: linear-gradient(180deg, #ffffff 0%, #f7fbf8 100%);
}

a {
  color: inherit;
  text-decoration: none;
}

code {
  font-family: "Cascadia Code", "Consolas", monospace;
  font-size: 0.95rem;
}

.shell {
  min-height: 100vh;
  width: min(100% - 32px, 1080px);
  margin: 0 auto;
  display: grid;
  grid-template-rows: auto 1fr;
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 24px;
  padding: 24px 0 18px;
}

.brand {
  font-size: 1rem;
  font-weight: 600;
  letter-spacing: -0.03em;
}

.nav {
  display: inline-flex;
  gap: 14px;
  align-items: center;
}

.nav a {
  padding: 10px 14px;
  border: 1px solid transparent;
}

.nav a:hover {
  border-color: var(--line);
  background: var(--page-soft);
}

.frame {
  display: grid;
  align-content: center;
  padding: 12px 0 40px;
}

.hero,
.docs-grid,
.form-shell {
  border: 1px solid var(--line);
  background: rgba(255, 255, 255, 0.94);
  box-shadow: 0 18px 40px rgba(15, 23, 42, 0.05);
}

.hero {
  padding: 48px;
}

.eyebrow {
  display: inline-block;
  margin-bottom: 16px;
  padding: 6px 10px;
  background: var(--accent);
  color: #166534;
  font-size: 0.82rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

h1,
h2,
h3,
p {
  margin: 0;
}

h1 {
  font-size: clamp(2.6rem, 6vw, 4.4rem);
  line-height: 0.98;
  letter-spacing: -0.05em;
  font-weight: 600;
}

.hero > p:first-of-type {
  margin-top: 14px;
  font-size: 1.15rem;
  color: var(--muted);
}

.hero-copy {
  margin-top: 10px;
  max-width: 620px;
  color: var(--muted);
  line-height: 1.7;
}

.actions {
  display: flex;
  gap: 12px;
  margin-top: 28px;
}

.button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 12px 18px;
  border: 1px solid var(--line-strong);
  background: #ffffff;
  color: var(--text);
}

.button:hover {
  background: var(--page-soft);
}

.button-primary {
  background: var(--accent);
  border-color: rgba(34, 197, 94, 0.35);
}

.button-primary:hover {
  background: rgba(34, 197, 94, 0.28);
}

.box-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 12px;
  margin-top: 28px;
}

.box {
  border: 1px solid var(--line);
  background: #ffffff;
  padding: 16px;
}

.box-label {
  display: block;
  margin-bottom: 8px;
  font-size: 0.8rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--muted);
}

.docs-grid {
  display: grid;
  grid-template-columns: 240px minmax(0, 1fr);
  min-height: 520px;
}

.docs-sidebar {
  padding: 28px 24px;
  border-right: 1px solid var(--line);
  background: var(--page-soft);
}

.docs-sidebar p {
  margin-top: 10px;
  color: var(--muted);
  line-height: 1.6;
}

.docs-content {
  padding: 24px;
  display: grid;
  gap: 12px;
}

.docs-box p {
  margin-top: 8px;
  color: var(--muted);
  line-height: 1.65;
}

.form-shell {
  padding: 32px;
  display: grid;
  gap: 20px;
  align-content: start;
}

.form-intro p {
  margin-top: 10px;
  color: var(--muted);
  line-height: 1.6;
}

.contact-form {
  display: grid;
  gap: 14px;
}

.contact-form label {
  display: grid;
  gap: 8px;
}

.contact-form span {
  font-size: 0.92rem;
  font-weight: 600;
}

.contact-form input,
.contact-form textarea {
  width: 100%;
  padding: 12px 14px;
  border: 1px solid var(--line-strong);
  background: #ffffff;
  color: var(--text);
  font: inherit;
}

.contact-form textarea {
  min-height: 140px;
  resize: vertical;
}

.submission-box p {
  margin-top: 10px;
  color: var(--muted);
}

@media (max-width: 860px) {
  .docs-grid {
    grid-template-columns: 1fr;
  }

  .docs-sidebar {
    border-right: 0;
    border-bottom: 1px solid var(--line);
  }

  .box-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 640px) {
  .shell {
    width: min(100% - 20px, 1080px);
  }

  .topbar {
    flex-direction: column;
    align-items: flex-start;
    padding-bottom: 12px;
  }

  .nav {
    flex-wrap: wrap;
    gap: 10px;
  }

  .hero,
  .form-shell {
    padding: 24px;
  }

  .actions {
    flex-direction: column;
  }
}
)WEV";
}

std::string ProjectCreator::configTemplate(const std::string& displayName) {
    const std::string escapedName = escapeForQuotedLiteral(displayName);

    return "{\n"
           "  \"port\": 3000,\n"
           "  \"env\": \"dev\",\n"
           "  \"name\": \"" +
           escapedName +
           "\",\n"
           "  \"worker_threads\": 4,\n"
           "  \"max_queue_size\": 64,\n"
           "  \"max_template_cache_size\": 256,\n"
           "  \"max_fragment_cache_size\": 512,\n"
           "  \"session_ttl\": 3600,\n"
           "  \"max_session_count\": 4096,\n"
           "  \"performance_metrics\": false,\n"
           "  \"database\": {\n"
           "    \"path\": \"storage/app.sqlite\"\n"
           "  },\n"
           "  \"packages\": [],\n"
           "  \"open_browser\": true\n"
           "}\n";
}

std::string ProjectCreator::readmeTemplate(const std::string& displayName) {
    return "# " + displayName +
           "\n\n"
           "Generated with WevoaWeb.\n\n"
           "## Quick Start\n\n"
           "```text\n"
           "wevoa --version\n"
           "wevoa start\n"
           "```\n\n"
           "Version output example:\n\n"
           "```text\n"
           "WevoaWeb Runtime 786.0.0\n"
           "```\n\n"
           "## First Steps\n\n"
           "- Open `/` for the welcome page\n"
           "- Open `/docs` for the starter docs page\n"
           "- Open `/contact` for the example form\n"
           "- Try `/hello/your-name` for the route parameter example\n\n"
           "## Routes\n\n"
           "- `/` welcome page\n"
           "- `/docs` documentation page\n"
           "- `/contact` example GET/POST form\n"
           "- `/hello/:name` route parameter example\n\n"
           "## Structure\n\n"
           "- `app/` route definitions\n"
           "- `views/` layout, docs, and page templates\n"
           "- `public/` static styling assets\n"
           "- `migrations/` SQL migrations for `wevoa migrate`\n"
           "- `packages/` locally installed reusable packages\n"
           "- `storage/` runtime database files\n"
           "- `wevoa.config.json` project configuration\n\n"
           "## Development\n\n"
           "```powershell\n"
           "wevoa start\n"
           "```\n\n"
           "## Production\n\n"
           "```powershell\n"
           "wevoa build\n"
           "wevoa serve\n"
           "```\n\n"
           "## Database\n\n"
           "```powershell\n"
           "wevoa make:migration create_users\n"
           "wevoa migrate\n"
           "```\n";
}

}  // namespace wevoaweb
