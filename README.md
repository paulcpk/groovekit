# GrooveKit

An agent-first DAW: FL Studio's workflow, a clean minimal look, and AI agents as first-class producers.
The human is the director; the agent does arrangement, mixing, automation and mastering.

Status: early development. The headless engine, command API, MCP server, sample library, analysis, mastering, live playback/recording and plugin hosting work; the UI comes next (v1). See [docs/architecture.md](docs/architecture.md) and [docs/roadmap.md](docs/roadmap.md).

## Building (macOS)

Requirements: Xcode Command Line Tools, CMake ≥ 3.22, Ninja, Node ≥ 24 and pnpm.

```sh
git submodule update --init --recursive
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## Using the engine

```sh
# Render a song to a 48 kHz / 24-bit WAV (add --tail 2 for reverb tails)
build/dev/engine/groovekit-engine render tests/songs/groove/song.json -o groove.wav --tail 2

# Check a song against the schema and the cross-reference rules
build/dev/engine/groovekit-engine validate tests/songs/groove/song.json

# Peak, RMS, integrated LUFS and onsets of a WAV, as JSON
build/dev/engine/groovekit-engine analyze groove.wav

# Built-in effects with their parameter names, units and ranges
build/dev/engine/groovekit-engine effects
```

### With Claude Code

The engine serves a JSON-RPC API over WebSocket (`groovekit-engine serve <project-folder>`, see [docs/protocol.md](docs/protocol.md)), and [mcp/](mcp) exposes it as MCP tools. The repository's [.mcp.json](.mcp.json) starts both when Claude Code runs in this folder; the project folder defaults to `~/Music/GrooveKit/Scratch` (or `GROOVEKIT_PROJECT`). Build the engine first, then start Claude Code here (`cd groovekit && claude`), approve the `groovekit` server, check `/mcp`, and ask it to make a beat.

To use GrooveKit from Claude Code in any folder, register it for your user instead:

```sh
claude mcp add --scope user groovekit -- node /path/to/groovekit/mcp/src/index.ts
```

Songs are JSON files described by [schema/project.schema.json](schema/project.schema.json). The examples in [tests/songs](tests/songs) double as render tests.

## Repository layout

- `engine/` — C++ engine: model, commands and history, API server, compiler to Tracktion, sampler, mixer, effects, renderer, analysis, CLI
- `schema/` — JSON Schemas for the project and the command protocol, and the C++/TypeScript generator (`pnpm codegen`)
- `mcp/` — the MCP server for agents
- `tests/` — render test songs and the fixture samples they use

## License

AGPLv3. GrooveKit builds on [JUCE 8](https://juce.com) (AGPLv3), [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) (GPLv3), [Rubber Band](https://breakfastquay.com/rubberband/) (GPLv2+) and [IXWebSocket](https://github.com/machinezone/IXWebSocket) (BSD).
