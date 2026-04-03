# WevoaWeb Distribution Strategy

**TL;DR:** WevoaWeb should be distributed primarily via installer (`WevoaSetup.exe`), with `wevoa.exe` as a secondary option for advanced users.

This document defines how WevoaWeb should be distributed to end users as a professional installable platform.

The goal is not only to ship a runtime binary. The goal is to make WevoaWeb feel like a real product that can be:

- downloaded easily
- installed globally
- used from any terminal
- removed cleanly
- updated through clear release versions

## Quick Start After Install

After installing WevoaWeb through the Windows installer, a user should be able to open a new terminal and run:

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

- `wevoa --version`: confirms the runtime is installed globally and available on `PATH`
- `wevoa create app`: generates a working starter project
- `cd app`: enters the generated project folder
- `wevoa start`: starts the built-in development server

---

## 1. Distribution Goal

WevoaWeb is not just a framework source tree. It is a combined:

- programming language
- runtime
- web framework
- CLI tool

Because of that, users should not need to manually wire up environment variables, copy binaries around by hand, or guess how to make the tool globally available.

The distribution model should therefore prioritize:

- easy installation
- global CLI access
- predictable filesystem layout
- uninstall support
- versioned releases

---

## 2. Two Distribution Models

### 2.1 Option A: Direct Binary Distribution

This model ships only the runtime binary:

- `wevoa.exe`

The user would:

1. download the binary
2. place it somewhere manually
3. optionally add that folder to `PATH`
4. run `wevoa --version`

#### Benefits

- simplest artifact to produce
- useful for advanced users
- good for CI pipelines and portable usage
- easy to embed in internal tooling or archives

#### Drawbacks

- user must decide where to place the binary
- PATH setup becomes a manual step
- no automatic uninstall path
- no Start Menu or shortcut integration
- less professional first-run experience for non-technical users

#### Best Use Cases

- CI environments
- portable zip distributions
- advanced developers
- internal testing and smoke validation

---

### 2.2 Option B: Installer-Based Distribution

This model ships a setup wizard:

- `WevoaSetup.exe`

The installer handles:

- copying the runtime to a stable install directory
- adding Wevoa to `PATH`
- creating uninstall metadata
- optionally creating shortcuts

#### Benefits

- best user experience
- no manual environment configuration
- clear install location
- clean uninstall path
- feels like a real platform rather than a raw executable

#### Drawbacks

- requires installer maintenance
- requires installer build tooling
- adds one more release artifact to produce and test

#### Best Use Cases

- normal Windows end users
- public releases
- professional onboarding
- product-style platform distribution

---

## 3. Recommended Distribution Model

### Recommendation

For real users, WevoaWeb should be distributed primarily through:

- **Installer-based distribution**

Direct binary distribution should still exist, but as a secondary path.

### Why Installer-Based Distribution Is the Best Default

WevoaWeb is a CLI runtime platform. For that kind of software, installation is not only about copying one file. It is also about making the platform usable from the system shell.

That means users benefit from an installer because it can:

- place the runtime in a stable location
- configure `PATH`
- register an uninstall entry
- create a familiar Windows setup flow

This is the same reason mainstream runtimes use installers on Windows: the installer turns a raw executable into a globally available tool.

### Final Recommendation

Use both models, but prioritize them differently:

- primary release for normal Windows users: `WevoaSetup.exe`
- secondary release for advanced/manual usage: `wevoa.exe`

---

## 4. Expected User Journey

### 4.1 Installer-Based Journey

Recommended user flow:

1. User downloads `WevoaSetup.exe`
2. User launches the setup wizard
3. Installer copies `wevoa.exe` into a stable install folder
4. Installer optionally adds the install folder to `PATH`
5. Installer registers uninstall metadata
6. User opens a new terminal
7. User runs:

```text
wevoa --version
```

8. User creates a project:

```text
wevoa create my-app
cd my-app
wevoa start
```

This is the intended "it just works" experience.

### 4.2 Direct-Binary Journey

Advanced-user flow:

1. User downloads `wevoa.exe`
2. User places it in a folder of choice
3. User either:
   - runs it with a full path
   - or manually adds the folder to `PATH`
4. User runs `wevoa.exe --version` or `wevoa --version`

This flow is valid, but less beginner-friendly.

---

## 5. Why the Installer Matters

An installer is important because software installation on Windows typically does more than copy files. It often also creates directories, registers environment data, and sets up uninstall behavior. That is exactly the difference between "a file you have somewhere" and "a tool installed on the machine" ([Wikipedia: Installation](https://en.wikipedia.org/wiki/Installation_%28computer_programs%29)).

For WevoaWeb specifically, the installer solves these practical problems:

- users do not need to decide where the runtime should live
- users do not need to edit environment variables manually
- the CLI becomes globally callable
- uninstall becomes safe and predictable
- future upgrades have a defined path

Without the installer, many users would still be able to use WevoaWeb, but the product would feel unfinished.

---

## 6. Release Artifacts

### 6.1 Recommended Windows Release Outputs

The release package should contain:

- `dist/windows/wevoa.exe`
- `dist/installer/WevoaSetup.exe`

Optional extras:

- `README.txt` or packaged docs
- sample project archive
- release notes

### 6.2 Practical Meaning of Each Artifact

- `wevoa.exe`: raw runtime binary for advanced users, CI, and portable use
- `WevoaSetup.exe`: normal user-facing installer

### 6.3 Recommended Public Download Presentation

For a GitHub Release or download page:

- first/highlighted asset: `WevoaSetup.exe`
- secondary asset: `wevoa.exe`

This keeps the main onboarding path simple while still supporting advanced workflows.

---

## 7. Versioned Release Strategy

### 7.1 Version Source of Truth

WevoaWeb should continue using one canonical version definition:

- `utils/version.h`

That version should flow into:

- `wevoa --version`
- runtime startup logs
- build manifests
- installer metadata
- release notes

### 7.2 Version Naming

Current version model:

- `786.0.0`

This is valid as long as the platform uses it consistently.

### 7.3 Release Folder Structure

Recommended release layout:

```text
dist/
  windows/
    wevoa.exe
  installer/
    WevoaSetup.exe
```

This is clearer than placing the raw binary and the installer in the same folder because the two artifacts serve different audiences.

---

## 8. Installer Implementation

### 8.1 Why Inno Setup

WevoaWeb currently uses Inno Setup for Windows installer generation.

This is a strong choice because Inno Setup is a script-driven Windows installer system that can generate a single EXE setup package and supports shortcuts, uninstall behavior, and registry/environment changes ([Wikipedia: Inno Setup](https://en.wikipedia.org/wiki/Inno_Setup)).

### 8.2 Why It Fits WevoaWeb

WevoaWeb needs exactly the sort of installation behavior Inno Setup is good at:

- install files under `Program Files`
- add the runtime to `PATH`
- create Start Menu / Desktop shortcuts
- register uninstall metadata
- ship one setup executable

### 8.3 Current Installer Path

Current installer inputs and outputs:

- source definition: `installer/WevoaSetup.iss`
- build script: `scripts/build-installer.ps1`
- output: `dist/installer/WevoaSetup.exe`

### 8.4 Expected Installer Flow

Recommended installer wizard flow:

1. Welcome page
2. Install location page
3. Optional tasks page
   - Add to PATH
   - Start Menu shortcut
   - Desktop shortcut
4. Install progress page
5. Finish page
   - open terminal
   - view docs

This is the right experience for a runtime platform because it feels familiar to Windows developers.

---

## 9. Global CLI Behavior

### 9.1 Why PATH Matters

After installation, the user should be able to type:

```text
wevoa --version
wevoa create app
wevoa start
```

without changing directories to the install folder.

That behavior depends on the install location being added to `PATH`. Windows uses the directories listed in `PATH` as part of the executable search process for commands like `.exe` files, which is why adding Wevoa's install folder enables global terminal usage ([Microsoft Learn: path](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/path)).

### 9.2 Expected Outcome After Install

After a successful installer-based install:

- any new terminal can locate `wevoa.exe`
- users can run Wevoa from any working directory
- projects no longer depend on relative paths to the runtime

This is essential for a platform that aims to feel comparable to established runtimes.

---

## 10. User Experience Principles

### 10.1 What Good Distribution Looks Like

Good WevoaWeb distribution should feel like this:

- download one installer
- click through a clear setup wizard
- open terminal
- run `wevoa --version`
- create and run a project immediately

### 10.2 What Users Should Not Need To Do

Users should not need to:

- manually edit environment variables
- manually copy the runtime to system folders
- manually register uninstall behavior
- guess where the runtime should be installed

### 10.3 Product Standard

If the platform requires manual shell setup for normal users, it is still behaving like a developer artifact.

If the platform installs cleanly and becomes globally callable immediately, it behaves like a real product runtime.

That is the standard WevoaWeb should target.

---

## 11. Distribution Best Practices

### 11.1 Ship Both, Promote One

Recommended release practice:

- always ship both `wevoa.exe` and `WevoaSetup.exe`
- always promote `WevoaSetup.exe` as the main Windows download

### 11.2 Keep Install Location Stable

Recommended default install location:

```text
C:\Program Files\Wevoa\
```

This keeps the platform:

- easy to locate
- predictable for support
- consistent for PATH and uninstall behavior

### 11.3 Keep Versioning Unified

Version should always come from one place:

- `utils/version.h`

That prevents mismatches between:

- CLI version
- installer version
- release notes
- build metadata

### 11.4 Preserve a Clean Uninstall Path

Every official installer should:

- register uninstall metadata
- remove PATH entries it created
- remove installed files safely

This is part of platform trust and operational hygiene.

---

## 12. Future Distribution Upgrades

Once the current installer-based distribution is stable, future platform work can add:

- auto-updater
- version manager for side-by-side runtime installs
- signed installers
- release channels such as stable/beta/nightly
- package ecosystem and remote registry

Potential future commands:

- `wevoa self-update`
- `wevoa use <version>`
- `wevoa install <package>` with remote sources

These are later-stage improvements. They should come after the installer path is stable and predictable.

---

## 13. Final Recommendation

For Windows users, WevoaWeb should be distributed primarily as:

- `WevoaSetup.exe`

and secondarily as:

- `wevoa.exe`

That is the correct product strategy because:

- the installer gives the best user experience
- the runtime becomes globally available
- uninstall is clean
- setup feels professional
- the raw binary still exists for advanced and automated use cases

The key principle is simple:

**WevoaWeb should be distributed as an installable platform, not merely as a standalone developer executable.**

---

## References

- [Installation (computer programs) - Wikipedia](https://en.wikipedia.org/wiki/Installation_%28computer_programs%29)
- [Inno Setup - Wikipedia](https://en.wikipedia.org/wiki/Inno_Setup)
- [Microsoft Learn: path](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/path)
