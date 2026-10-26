# GrooveKit

An agent-first music production app, with AI agents as first-class producers. The human is the director; the agent arranges, mixes, automates and masters.

Songs are built from channels (a built-in multi-layer sampler or hosted VST3/AU instruments), patterns of notes that any number of channels share, a playlist of free tracks holding pattern, audio and automation clips, and a mixer of inserts with effects, sends and routing. Everything the UI can do, an agent can do through the same API, and every change is undoable.

Status: early development. The engine, command API, MCP server, sample library, analysis, mastering, live playback and recording (including clock-synced hardware), plugin hosting and a first desktop UI work. See [docs/architecture.md](docs/architecture.md) and [docs/roadmap.md](docs/roadmap.md).

## Download

Website: https://groovekit.org

The latest macOS app (Apple silicon): [GrooveKit-macOS.zip](https://github.com/paulcpk/groovekit/releases/latest/download/GrooveKit-macOS.zip). Every version is on the [releases page](https://github.com/paulcpk/groovekit/releases).

Unzip it and move GrooveKit to Applications. Until releases are notarized, macOS says it "could not verify" GrooveKit the first time: click Done, then in System Settings → Privacy & Security click Open Anyway next to "GrooveKit was blocked" and confirm. (Or run `xattr -dr com.apple.quarantine /Applications/GrooveKit.app` once.)

To publish a release, push a version tag: `git tag v0.2.0 && git push origin v0.2.0`. The release workflow builds the app and attaches the zip.

## Signing releases

Releases are signed with a Developer ID and notarized by Apple when these repository secrets are set
(Settings → Secrets and variables → Actions); without them they're signed ad hoc and macOS warns
on first launch.

1. Join the Apple Developer Program, and in Xcode → Settings → Accounts → Manage Certificates
   create a **Developer ID Application** certificate. Export it from Keychain Access as a .p12
   with a password.
2. `APPLE_CERTIFICATE`: the .p12 as base64 (`base64 -i certificate.p12 | pbcopy`).
3. `APPLE_CERTIFICATE_PASSWORD`: the .p12's password.
4. `APPLE_SIGNING_IDENTITY`: the certificate's name, e.g. `Developer ID Application: Your Name (TEAMID1234)`
   (`security find-identity -v -p codesigning` lists it).
5. In App Store Connect → Users and Access → Integrations → App Store Connect API, create a key
   with the Developer role, then set `APPLE_API_ISSUER` (the issuer ID), `APPLE_API_KEY` (the key ID)
   and `APPLE_API_PRIVATE_KEY` (the contents of the downloaded `AuthKey_….p8`).

The next version tag then builds a release that opens without a warning.

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

### The app

```sh
cd ui
pnpm dev        # in the browser at http://localhost:5178
pnpm app        # as a desktop app (needs Rust: brew install rust)
pnpm app:build  # a .app and .dmg, with the engine bundled (build the release preset first)
```

Both find the engine serving the project (for example the one Claude Code's MCP server started, so you watch the agent's changes live) or start one. The app opens the project opened last (or `GROOVEKIT_PROJECT`, or `~/Music/GrooveKit/Scratch`); one engine serves a project at a time, and opening another project switches every client of that engine to it.

Songs are JSON files described by [schema/project.schema.json](schema/project.schema.json). The examples in [tests/songs](tests/songs) double as render tests.

## Repository layout

- `engine/` — C++ engine: model, commands and history, API server, compiler to Tracktion, sampler, mixer, effects, renderer, analysis, CLI
- `schema/` — JSON Schemas for the project and the command protocol, and the C++/TypeScript generator (`pnpm codegen`)
- `mcp/` — the MCP server for agents
- `ui/` — the app: React UI and the Tauri desktop shell
- `tests/` — render test songs and the fixture samples they use

## License

AGPLv3. GrooveKit builds on [JUCE 8](https://juce.com) (AGPLv3), [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) (GPLv3), [Rubber Band](https://breakfastquay.com/rubberband/) (GPLv2+) and [IXWebSocket](https://github.com/machinezone/IXWebSocket) (BSD).

Contributions are welcome under the terms in [CONTRIBUTING.md](CONTRIBUTING.md).
