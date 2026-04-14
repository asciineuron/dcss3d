# AGENTS.md

## About

dcss3d is envisioned to be a 3d renderer webview of a server session [Dungeon Crawl Stone Soup](https://github.com/crawl/crawl) game. It is inspired by crawl's native 2d javascript webtiles viewer, and acts as a consumer of the web api. It uses this data to reconstruct the game in 3d a la doom and control the remote 2d game by playing.

## Coding Guidelines

- Above all, follow the existing style and general architecture of the surrounding code.

- Tech stack:
  - C++23 language
  - CMake build system
  - SDL3_GPU 3D framework
  - Slang for shader development
  - imgui for ui overlay
  - mdspan for multi-dimensional array spanning
  - nlohmann_json for json parsing
  - glm for mathematics
  - spdlog for logging
  - unit test framework?
  - crawl's upstream webtiles api, see `/Volumes/Ext/Code/crawl/crawl-ref/source/webserver/`

## Build/Run Instructions

1. In base directory `dcss3d`, run `./run.sh`
