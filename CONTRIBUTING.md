# Contributing to GEO1Game

Thank you for considering a contribution to the project! This document outlines the recommended steps and best practices to follow when contributing code, features, or documentation.

## Development Environment

This project is primarily developed using:
- C for the native daemon
- Python and PHP for auxiliary scripts
- JavaScript (Three.js) for the client
- SQLite / MySQL for backend data
- VS Code is the recommended editor (with PlantUML, Markdown, and C/C++ extensions)

You may use either macOS or Linux for development. Some components (like the daemon) require POSIX compatibility and native build tools.

## Building the Project

You can use the provided Makefiles or the autoconf/automake toolchain:

```bash
./configure
make
```

Or in minimal setups:

```bash
make -f Makefile.dev
```

Some tasks (e.g. plugins or image generation) require a remote build context or SSH access to a Linux server.

## Code Contributions

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/your-feature-name`.
3. Commit your changes with clear messages.
4. Test locally using `make test` or your preferred unit test flow.
5. Submit a pull request.

## Code Style

- Use consistent indentation (tabs or spaces per project settings).
- Keep comments in English, avoid accented characters in code.
- Modularize your changes and avoid mixing unrelated fixes.

## Communication

If you're unsure where to start or want to propose something major:
- Open a GitHub Issue first.
- For technical discussions, start with a draft PR or ask via the Issue tracker.

Thank you again!
