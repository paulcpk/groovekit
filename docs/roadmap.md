# GrooveKit — Roadmap

Build order for the coding agent. Each milestone should be usable and tested before moving on. See [architecture.md](architecture.md) for decisions.

Status (2026-10-26): v0 done; v0.5 done except the Digitakt-specific items marked open; v1 mostly done (open items marked).

## v0 — Headless engine (done)

- C++ engine with JUCE + Tracktion, no UI.
- GrooveKit JSON project model (JSON Schema): channels, patterns (notes), playlist, mixer with sends. Audio and automation clips are in the schema from the start.
- Compiler from the JSON model into a Tracktion Edit.
- Built-in polyphonic sampler with multiple layers per channel, and step patterns.
- Offline render: JSON song in, WAV out.
- Render tests and sanitizers in CI (macOS) from day one.

Done when: a hand-written JSON song renders to a correct WAV.

## v0.5 — Agent and Digitakt loop

- [x] Command system with a persistent history log and undo.
- [x] WebSocket API with a shared schema and generated C++/TypeScript types.
- [x] MCP server exposing commands as tools. Open: a real session where Claude Code drives a song end to end.
- [x] Analysis tools: LUFS, true peak, loudness range, spectrum, stereo, masking between stems.
- [x] Sample library index with analysis, so the agent can build a full song from the library without the Digitakt.
- [x] Time-stretching of audio clips to the project tempo.
- [x] VST3/AU hosting with out-of-process scanning, plugin state and parameters; the Overbridge plugin loads.
- [ ] Multi-output routing of instrument plugins (Overbridge per-track outputs into separate inserts).
- [x] Live playback and audio recording into clips (generic inputs). Open: verify with the Digitakt over USB.
- [x] Basic mastering chain (EQ, compressor, lookahead limiter, loudness targeting).

Done when: record Digitakt grooves, ask Claude for an arrangement, and get a mastered bounce.

## v1 — First UI

- [x] Tauri shell with React, TypeScript, Tailwind and Radix primitives (shadcn-style components).
- [x] Playlist with toggleable tracks and automation clips.
- [x] Channel rack with step buttons, piano roll, mixer (with meters and live parameter changes), transport.
- [x] Sidebar with sample browser and history tabs.
- [x] Plugin windows. Automation clips from any mixer or effect parameter; open: "last tweaked" from inside plugin windows.
- [x] Live view of agent changes, grouped per request.
- [ ] Mac build with signing and notarization (unsigned .app and .dmg build; signing needs a Developer ID).

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
