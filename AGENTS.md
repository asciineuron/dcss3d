# AGENTS.md

## About

dcss3d is envisioned to be a 3d renderer webview of a server session [Dungeon Crawl Stone Soup](https://github.com/crawl/crawl) game. It is inspired by crawl's native 2d javascript webtiles viewer, and acts as a consumer of the web api. It uses this data to reconstruct the game in 3d a la doom and control the remote 2d game by playing.

## Coding Guidelines

- Above all, follow the existing style and general architecture of the surrounding code.

- Before making a set of changes, draft out an implementation plan, including the what and why of the problem. Reach out to me to discuss before beginning implementation.
- When making changes, ALWAYS stop before making a commit. Reach out to me to discuss what you have implemented and why, we can update and review your implementation plan, and only then will I ask you to make the commit.
- If you need to test the game, you must stop and ask me to playtest for you. This will be needed to check if whatever issue is fixed. 
- If you get stuck on a problem, try stepping back and considering the bigger picture. Are all your assumptions correct? Could the issue lie elsewhere? Often spiraling into micro-analysis is unproductive.

- Keep track of your findings and progress in `WORK_NOTES.md`

- Tech stack:
  - C++23 language
  - CMake build system
  - SDL3_GPU 3D framework
  - Slang shader development
  - imgui ui overlay
  - mdspan multi-dimensional array spanning
  - nlohmann_json json parsing
  - glm mathematics
  - spdlog logging
  - Catch2 unit testing
  - crawl's upstream webtiles api, see `/Volumes/Ext/Code/crawl/crawl-ref/source/webserver/`

## External Documentation

- If needed, search for documentation in the following locations:
  - SDL: `/Volumes/Ext/Downloads/sdlwiki/SDL3/`

## Build/Run Instructions

1. In base directory `dcss3d`, run `./run.sh`

## Testing

1. Run `cd build && ctest`
