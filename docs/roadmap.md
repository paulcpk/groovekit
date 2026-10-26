# GrooveKit — Roadmap

Build order for the coding agent. Each milestone should be usable and tested before moving on. See [architecture.md](architecture.md) for decisions.

## v0 — Headless engine

- C++ engine with JUCE + Tracktion, no UI.
- GrooveKit JSON project model (JSON Schema): channels, patterns (notes), playlist, mixer with sends. Audio and automation clips are in the schema from the start.
- Compiler from the JSON model into a Tracktion Edit.
- Built-in polyphonic sampler with multiple layers per channel, and step patterns.
- Offline render: JSON song in, WAV out.
- Render tests and sanitizers in CI (macOS) from day one.

Done when: a hand-written JSON song renders to a correct WAV.

## v0.5 — Agent and Digitakt loop

- Command system with a persistent history log and undo.
- WebSocket API with a shared schema and generated C++/TypeScript types.
- MCP server exposing commands as tools; Claude Code drives a song end to end.
- Analysis tools: LUFS, spectrum, masking, true peak.
- Sample library index with analysis, so the agent can build a full song from the library without the Digitakt.
- VST3/AU hosting, including the Digitakt Overbridge plugin. Keep the multichannel audio-device path open as well.
- Audio recording of Digitakt tracks into clips.
- Basic mastering chain.

Done when: record Digitakt grooves, ask Claude for an arrangement, and get a mastered bounce.

## v1 — First UI

- Tauri shell with React, TypeScript, Tailwind, shadcn/ui.
- Playlist with toggleable tracks and automation lanes.
- Channel rack with step buttons, piano roll, mixer, transport.
- Sidebar with sample browser and history tabs.
- Plugin windows and "last tweaked" automation clips.
- Live view of agent changes, grouped per request.
- Mac build with signing and notarization.

## v1.5 — History and AI polish

- Time-grouped history, jump to any point, explicit branches, discard warning pop-up.
- In-app chat panel with Claude, GPT and local model support.
- Export with samples.
- Engine auto-restart and recovery from the history log.

## Later

- Own JUCE effects: parametric EQ, compressor, mastering limiter, phaser, flanger, filters, delay, reverb.
- Windows build.
- Reference-track matching for mastering.
- Out-of-process plugin sandboxing if crashes are a problem.
- iPad remote control over Wi-Fi; later a standalone iPad app with AUv3.
