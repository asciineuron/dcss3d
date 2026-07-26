# AGENTS.md

## About

dcss3d is envisioned to be a 3d renderer webview of a server session [Dungeon Crawl Stone Soup](https://github.com/crawl/crawl) game. It is inspired by crawl's native 2d javascript webtiles viewer, and acts as a consumer of the web api. It uses this data to reconstruct the game in 3d a la doom and control the remote 2d game by playing.

## Design Docs

- Before making any changes, draft out an implementation plan including the what and why of the problem, and your prosed design architecture. Reach out to me to discuss your thought process step by step before beginning implementation. 
- Record this in `design_docs/` and update it as our conversation progresses.
- Consider multiple possible approaches and weigh their tradeoffs.
- Include mermaid diagrams to visually explain your design.
- Use red/green TDD to structure your implementation plan.

## Coding Guidelines

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

- Above all, follow the existing style and general architecture of the surrounding code.

- When making changes, ALWAYS stop before making a commit. Reach out to me to discuss what you have implemented and why, we can update and review your implementation plan, and only then will I tell you to make the commit.
- If you need to test the game, you must stop and ask me to playtest for you. 
- If you get stuck on a problem, try stepping back and considering the bigger picture. Are all your assumptions correct? Could the issue lie elsewhere? Don't spiral into micro-analysis.

- Add comments to code in sections that are particularly important, confusing, or complicated, for use by future agents.

## External Documentation and Examples

- If needed, search for documentation in the following locations:
  - SDL3: 
    - `/Volumes/Ext/Downloads/sdlwiki/SDL3/`
    - SDL gpu examples: `./SDL_gpu_examples/`
  - Upstream dcss webserver:
    - `/Volumes/Ext/Code/crawl/crawl-ref/source/webserver/` 
    - Use this to see what data is available for clients to consume.
  - Upstream official javascript webtiles client:
    - `/Volumes/Ext/Code/crawl/crawl-ref/source/webserver/webtiles`
    - Use this for handling data received from the server. Try to match their datastructures and match behavior *exactly*.
    - See `./CRAWL_WEBTILES_API_NOTES.md` for overall api structure
    - See `./MONSTER_DATA_API.md` for detailed monster api reference

## Build/Run Instructions

1. In base directory `dcss3d`, run `./run.sh`

## Testing

1. Run `cd build && ctest`
