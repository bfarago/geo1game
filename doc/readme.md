# Documentation
for the GEO 1 game project

## Table of Contents
- Notes and considerations in [Development Plans](devplans.md)
- The [Architectural Overview](architect.md)
- The [Development Summary](devplans.md)
- The [Changelog](changelog.md)
- The [Plan of the Game](gameplan.md)

## Introduction
This project is a work-in-progress server–client architecture engine, capable of producing and managing modern 2.5D and 3D data structures efficiently on the server side, then delivering filtered content to clients via HTTP and WebSocket protocols.

The initial proof-of-concept was a planetary terrain generator built in a single day, which has gradually evolved into its current state.

Servers typically lack a GPU but run on 64-bit hardware architectures supporting SIMD instructions. Therefore, the project includes a native environment optimized for processing large datasets such as map data, using cache-efficient, SIMD-accelerated algorithms.

The engine also features a plugin architecture — similar to a system I’ve built previously — designed to release memory when certain subsystems are idle. Since much of the generated content remains unchanged over short time periods, a custom caching system is in place. Compared to a Python-based terrain generator, this native implementation achieves roughly a 20× speedup, and caching further reduces client wait time.

This allows the server-side service to generate terrain, biosphere, precipitation, and temperature data dynamically, at the required resolution, per client request. An Apache proxy provides access to the C-based service through various protocols.

The plugin system enables specific tasks to be modularized — for example, image generation is handled by an image plugin, while application-specific operations (e.g., game logic) are handled by a dedicated plugin that can also abstract the actual database engine. Supported backends include JSON, SQLite, and MySQL. Clients access the application server via HTTP, HTTPS, WS, and WSS protocols.

A sample WebSocket-based text chat is implemented, and some database operations are also exposed. On the client side, a JavaScript module provides resilient communication (with fallback support), allowing background operation. This approach resembles the underlying protocol model used by modern social media platforms.

The system is designed to be modular, efficient, and extensible, supporting a wide range of future game mechanics and service components. In the shorter term, the goal is to separate the core engine from application-specific projects. In the longer run, delivering a production-ready application service is considered an achievable and realistic objective.