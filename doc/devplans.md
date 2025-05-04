# Development Plan

## Build (Make) Framework

I'm currently planning to restructure my build setup. Initially, I used bash scripts to build my C code for maximum flexibility with compiler flags like library linking and SIMD tuning. As the project grows, the build steps are becoming more stable, so I'm outlining the requirements.

- Use only common, basic tools:
  - autoconf, automake, make, gcc, flex, bison
  - avoid specialized or heavyweight tools
  - Python scripts are preferred over external build systems
- Support both remote and local development:
  - The development machine is often different from the terminal I'm typing on
  - Remote host/user settings should remain outside the repo
  - Remote: Linux server with minimal tools
  - Local: mainly macOS, but sometimes Windows or Linux
  - VS Code is the preferred IDE
- Local builds should focus on:
  - fast iteration (e.g., unit testing)
  - development happens over SSH on remote files
  - frequent git commits in the repo
- Remote builds are for:
  - release builds and external dependency integration
  - watchdog via cron to restart crash-prone dev binaries
  - managing bind/listen conflicts (prevents duplicate starts)
- A single build system should:
  - be easy to understand and require minimal setup
  - support conditional features (profiling, logging, debug, etc.)
  - allow packaging (e.g., tar.gz, .deb)
  - support cross-compiling for other targets
  - provide consistent logs and error reporting
  - generate developer-facing documentation automatically
  - clean up intermediate files and cache directories
- Artifacts produced:
  - web content: PHP, JS, HTML, static assets
  - native Linux binary with:
    - support for service lifecycle (start/stop/reload/status)
    - logging with logrotate for both dev and prod
    - dual Apache/cron/fail2ban setup for prod and dev environments
    - structured logs (error/info/debug), with some stats
    - debug-friendly mode: runs clean under gdb without extra setup
- Makefile-specific needs:
  - SIMD-specific flags, optional or legacy library selection
  - override compiler easily for toolchain switching
  - separate targets for test, dev, release
  - support parallel builds where possible
  - clear dependency tracking to avoid unnecessary recompilation
  - Folder structure:
    - a typical GitHub style inside the project root, there are:
      - doc: for markdown documentation mainly
      - etc: similar to Unix system etc, a deployment example config only
      - scripts: non-web accessible (server-side) tools, Python and bash
      - www: non-generated (from developer point of view) static web content, probably PHP, HTML, PNG
      - bin: non-versioned folder, the folder for built native server-side executables (daemon)
      - plugins: non-versioned folder for plugin binaries (.so) built
      - var: non-versioned folder, mainly the runtime output generated files will be placed here
      - src: C sources
        - This folder is in the -I include path, and build system usual path.
        - C and H files for the daemon (*1)
        - plugin_*something* separated folders for different .so outputs, which are the plugins (there is a goal to separate .so files as much as possible)
  - Targets are:
    - geo daemon (bin/geod) native Linux executable
    - many .so files in plugins/ folder. These are automatically loaded/unloaded by the daemon.
    - not yet, but planned generated files somewhere in www/gen folder 
- Native binary related questions:
  - there is a goal to compile architecture-specific SIMD versions for some of the code, mostly computationally heavy parts like Perlin noise, filters, huge array calculations, etc.
  - it shall run in a virtual server XenU or similar; Intel SIMD is supported, but no PCI-connected GPU is available (so, no CUDA or GPU-specific code)

## System and Architect Questions
For more detailed documentation of the topic, see: [The Architect paragraph](architect.md) in the documentation.
- How the **system architecture** looks like (in a nutshell):
  - at the web browser client side:
    - some www/j/*.js JavaScript modules are executed
    - there is a CommunicationController class, which provides the WebSocket protocol or HTTP requests as a fallback option
  - at the webserver side:
    - mostly PHP scripts, and Apache proxy is used
    - there is a configured Apache virtual host, with proxy settings for HTTP, HTTPS, and WebSocket ws:, wss: protocols
    - all of those internal requests are serviced by a geod daemon
  - native clients:
    - macOS, iOS, iPadOS, watchOS are definitely planned (I already have a separate folder structure, which I would like to migrate here soon)
    - not yet planned, but an Android variant is needed (I know Kotlin, but...)
    - probably Windows target is also a goal (game servers and users like it)
- **Internal architecture**:
  - the daemon memory footprint is highly reduced to 2.5 - 3 Mbytes. Mostly everything is in .so files.
  - there is a plugin API with host interface and plugin interface
    - there is a registration phase when the daemon starts; all plugins provide some permanent config (mostly textual keys)
    - plugins can be offline, when not even the code is in memory
    - if a plugin-provided function is needed, the daemon loads the .so
  - both the daemon and the plugins are threaded, reentrant, or mutex-locked
  - there are generalized and specialized APIs. All plugins can always call the host interface or request a plugin to start and call the specific API.
  - there is a home-keeper thread, which checks the last API access of a plugin and unloads it if not needed
  - there is a
    - general load-unload (init, finish) thread access API
    - event API to trigger a plugin
    - cache (file handling) API
    - image file generation (PNG) API
    - HTTP, WebSocket API
    - Map generator and query API

## Open points to clarify

- This is not a typical build setup, as each plugin may have its own library dependencies and compiler flags. I need a clear strategy for handling this with `automake`, `autoconf`, etc., preferably in a simple way. If that's not feasible, I may use Python scripts instead of trying to force Makefiles to do things they aren't suited for.
- Should plugin `.so` files maintain versioning or ABI compatibility across builds?
- Should the configure/build process detect and handle optional plugin features dynamically?
- Are there additional platform-specific concerns (e.g., macOS vs. Linux) beyond SIMD?
- Should unit tests be implemented per plugin, or target the daemon as a whole?
- Will test data or mock subsystems be needed for effective isolated testing?
- Is cross-compilation for mobile platforms (e.g., iOS) part of the build system, or handled separately?
- Should `make install` place plugins in a runtime-searchable path, or be managed manually?
- Do any plugins need to support hot-reloading at runtime? If so, how should build artifacts be organized?
- Is packaging required per plugin, or as a combined distribution?