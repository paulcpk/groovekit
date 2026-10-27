# GrooveKit — Architecture

Open-source (AGPLv3) hobby project: an agent-first DAW with FL Studio's workflow, a clean minimal Ableton-style look, and AI agents as first-class producers. The human is the director; the agent does arrangement, mixing, automation and mastering.

## Goals

- Headless audio engine as the core; any UI (desktop, web, iPad) can be built on top.
- Solid VST3/AU plugin hosting, including opening plugin windows (e.g. Massive) and creating automation clips from their knobs.
- FL Studio semantics: channel rack with step patterns, sampler, piano roll, playlist with toggleable tracks and automation lanes, mixer. Own visual design, no Image-Line artwork, icons or trademarks.
- Primary use case: record grooves from an Elektron Digitakt II (via Overbridge), then iterate with an AI on arrangement and mastering.
- Mac first, Windows later.

## System overview

Four layers, bottom up:

1. **Engine** — C++ process built on JUCE + Tracktion Engine. Audio I/O, plugin hosting, realtime playback, offline rendering, analysis.
2. **API** — WebSocket server embedded in the engine process. Commands in, events out.
3. **UI** — React app running in a Tauri shell. One client among many.
4. **AI** — MCP server exposing the same commands as tools. Another client.

The UI and the agent are peers: both talk to the engine through the same command API.

## Tech stack

- Engine: C++20, JUCE 8 (AGPLv3) and Tracktion Engine 3.x (GPLv3), pinned as a git submodule. CMake + Ninja.
- Transport: WebSockets with JSON messages. Shared memory for high-rate streams (meters) on desktop can come later.
- Message schema: JSON Schema as the single source of truth; generate TypeScript types and C++ structs (nlohmann/json) from it. song.json uses the same schema as the snapshot clients receive.
- UI: React, TypeScript, Tailwind, shadcn/ui, Zustand (or similar) for the project-state mirror. Canvas/WebGL for piano roll, playlist and meters.
- Shell: Tauri, with the engine bundled as a sidecar binary.
- No backend/database needed initially; projects are local files.

## Engine API

- Every change is a typed, reversible **command** (e.g. createPattern, writeNotes, placeClip, setParameter, addInsertEffect, setAutomation, previewSample, render).
- The engine broadcasts **events/patches** so all connected clients stay in sync, including when the agent makes changes.
- On connect, a client receives a full project snapshot, then incremental patches.
- Each command produces a forward patch and an inverse patch against the project model. The same patches are the sync events, the undo data and the history log entries.
- Streams: playhead position and meter levels at roughly 30–60 Hz.
- Audio is never sent to a local UI; the engine plays directly to the audio device. Audio streaming is only relevant for future remote clients.
- UIs that talk to the engine over WebSockets are not linked to it, so they may use any license.

## State model

- **Project state** (song data) lives in the engine. The UI keeps a mirror.
- **UI state** (open windows, selection, zoom, scroll) lives only in React; the engine never knows about it.
- Example: opening a pattern and the sampler is pure UI state; clicking play on a sample sends one previewSample command.

## Project model

- Groove Kit owns its own FL-style model as the source of truth: channels, patterns, playlist tracks, clips (pattern, audio, automation), mixer inserts and sends, plugin states.
- The engine compiles this model into a Tracktion Edit behind the scenes: each channel becomes a hidden Tracktion track with its instrument; pattern placements become MIDI clips on those tracks; automation clips become automation curves.
- Step sequencer and piano roll edit the same note data (steps are short notes), so a step pattern can be opened in the piano roll.
- FL semantics: a pattern holds notes for many channels, and a pattern clip plays all of them. Playlist tracks are numbered lanes that only arrange (name, colour, mute, solo); any clip can go on any track, and a track needs no adding first. Everything about how a clip sounds and where it plays belongs to its channel and the mixer.
- Timebase: integer ticks at 960 PPQ. One fixed tempo and 4/4 at first; tempo and time-signature changes later.
- A channel's instrument is either the built-in sampler or a hosted plugin. The sampler is polyphonic and holds one or more layers (sample, root note, key and velocity range, gain, pan, volume envelope, one-shot or hold, loop, fades, normalize, polarity, swapped sides and transient shaping), so one channel can stack several samples. Envelopes have delay, attack, hold, decay, sustain and release with curves; the channel's modulation moves volume and cutoff with LFOs and resonance, pan and pitch with envelopes and LFOs, and its filter is a 12 or 24 dB/octave state-variable one (low-pass, high-pass, band-pass, notch). Transient shaping is done once per sample and shared.
- Mixer: each channel routes to an insert. Inserts have effect slots, volume, pan and sends to other inserts; everything ends at the master insert. Sends exist from the start.
- The schema covers pattern, audio and automation clips from the start, even where the engine supports them later.
- Audio clips play the first layer of a sampler channel: the layer's part of the file (start/end, reverse), stretched to the song's tempo when it has a `sourceTempo` and pitch-shifted by its transpose and fine tune (Rubber Band R3, offline, length kept), as a copy rendered once and cached in `~/Library/Caches/GrooveKit`; then the layer's gain, pan and envelope and the channel's filter (shaped per clip, from the timeline position) and the channel's fader, into its insert. The sampler itself only loads its samples when it might play notes (it's in the rack's list, has notes, or is the channel the keyboards play), so long recordings stay on disk. A channel's `group` (rack or arrangement) only decides which list of the channel rack shows it. Tracktion's own auto-tempo is switched off: it guesses tempos from file names and resizes clips by itself.

## Project files

- A project is a folder containing song.json, plugin state files (binary blobs, stored separately), and the history log.
- Samples are referenced by path by default.
- Export-with-samples option bundles every referenced file for moving or sharing.

## Sample browser

- FL-style explorer in the sidebar. The engine indexes library folders; the UI shows the tree; clicking sends a preview command.
- The agent can use the same index to pick sounds.
- Default library folder: `~/Music/Samples` (created if missing) until the user picks folders. The index lives in `~/Library/Application Support/GrooveKit/library.json`; scans run on several threads, only analyse new or changed files and run in the background whenever the engine starts.

## History and branches

- Every command is appended to a persistent log saved with the project, with periodic full snapshots for fast jumping. The log is a tree from day one, so branches need no migration later.
- Every change is written as it's made; saving marks the history entry the song was saved at (a `saved` record in the log). Quitting or opening another project with changes since asks whether to save; not saving steps back (or across branches) to that entry, so the changes since stay redoable.
- The sidebar has a History tab next to the explorer. The UI groups tiny steps by activity/idle gaps (e.g. "45 edits, 14:10–14:40").
- The user can jump to any point in the project history.
- Branching is explicit: a plus button creates a new branch from the selected point; the user can switch between branches.
- Jumping back and editing without creating a branch discards the newer history, after a warning pop-up that offers "create branch instead".
- Plugin states are stored whenever they change, content-addressed by hash so identical states are stored once; recorded audio files are never deleted.
- All agent changes from one request form a group that can be undone together.

## Plugin hosting

- VST3 and AU via Tracktion/JUCE.
- Plugin editors are native OS windows opened by the engine; on desktop they float or dock next to the UI. Remote UIs (web, iPad) show generic parameter controls instead.
- Automation linking works like FL's "last tweaked": the plugin notifies the host on parameter changes, the engine tracks the last touched parameter, and the UI offers "create automation clip".
- Digitakt II: two input paths, both supported eventually: (1) the Overbridge plugin hosted in the engine for per-track USB audio and parameter control/automation, and (2) the Digitakt as a multichannel USB audio device. The first workflow is record long grooves, then let the agent cut, arrange, mix and master them. Agent-driven re-recording in real time comes later.
- Effects: GrooveKit's own seven-band EQ, compressor, multiband compressor (three or four Linkwitz-Riley bands, each compressing down and lifting up with its own stereo width, then a master compressor and a ceiling), reverb, delay, chorus, distortion, exciter (analog-style stages with generated harmonics), bitcrusher, stereo field and mastering limiter, Tracktion's phaser and filters, and the user's VST3/AU plugins. Effects that draw live measurements (gain reduction, band levels, the stereo image) keep them for `mixer.meter`.
- Triad (`plugins/triad`): GrooveKit's three-oscillator synth, a JUCE VST3 built with the engine. The app carries it in `Resources/Plug-Ins` and the engine registers it without a scan (the build tree's copy during development); its symbols are hidden so its JUCE never mixes with the engine's. Every insert effect sits between a tap and a blend that mix the dry signal back in (the slot's mix), delay-compensated for the effect's latency.
- Ableton devices cannot be hosted; FL plugins only where Image-Line ships them as VST/AU.

## Crash handling

- Plugins run in-process at first.
- The UI is a separate process and survives engine crashes.
- The engine auto-restarts and rebuilds state from the history log.
- Out-of-process plugin sandboxing later, only if needed.

## AI layer

- **Eyes:** a compact project summary (tempo, channels, clips with length and loudness, arrangement, mixer) with drill-down on demand.
- **Hands:** the engine commands exposed as MCP tools, grouped into a handful of tools rather than one tool per command, to keep the agent's context small. The MCP server is a separate TypeScript process that connects to the engine over WebSocket like any other client.
- **Ears:** offline render of a section plus analysis (LUFS, spectral balance, masking between tracks, stereo width, true peak, optional reference-track comparison). The agent adjusts and re-checks.
- **Library:** the agent can search the indexed sample library (with analysis such as type, key, tempo, loudness) and build a complete song from scratch without the Digitakt: pick a kick loop and a bass sample, place them, layer hats on top, add filters and automation.
- **Listening** (`engine/src/listening`): names say little about how a sample sounds, so GrooveKit hears it. After a one-time download (ONNX Runtime, loaded at run time, plus two models, about 400 MB in `<app data>/models/listening`), every library sample gets a CLAP embedding (LAION's larger_clap_music_and_speech, quantised): sounds that feel alike, and words that describe them, land close together. The index lives in `<app data>/sounds.bin`; only new or changed files are heard. A style is a folder of reference tracks: six stretches of each are split into drums, bass and the rest with htdemucs, and each part, the drums' low and high end, single hits and the whole mix are embedded, with tempo, swing, density, loudness, spectral balance and part levels measured (`<app data>/styles/<name>.json`; the audio itself isn't kept). Searches rank by fit to a style (nearest reference sounds in the sample's part, scaled so 0 is an average library sample and 100 as close as the references are to each other), by likeness to words or another sample, and by the user's verdicts (`<app data>/taste.json`). Words for a sound are the descriptive words it's most distinctively close to, compared with the library. Comparing a song with a style renders the mix and hears it exactly as the references were heard, then reports fit per part and the biggest measurable differences. Tempo comes from the kick band and swing and density from the hat band, where the 16ths are. Everything runs on the Mac; nothing is uploaded.
- Mastering: deterministic chain (EQ, glue compression, limiter to target LUFS) parameterized by the agent, plus reference matching.
- Clients: Claude Code via MCP, and an in-app chat panel (existing chat UI, provider-agnostic layer such as the Vercel AI SDK) connected to the same MCP server.
- Providers: Claude, OpenAI/GPT and local models out of the box. Local models are marked experimental for long tool-calling tasks.
- Personal use can run on a Claude subscription through the Agent SDK; the product should not depend on users' subscriptions.

## Platforms

- Desktop: Mac first, Windows later.
- iPad (future): engine embedded in the app (no separate process on iOS), AUv3 plugins only. Alternative: iPad as a remote control for the desktop engine over Wi-Fi.

## Testing

- Render tests: small JSON songs rendered offline (48 kHz, 24-bit by default) and checked against analysis values within a tolerance (onset times, peak and RMS per segment, LUFS). Exact golden-file comparisons only on the same platform.
- CI: GitHub Actions on macOS runners.
- Undo tests: random command sequences undone fully must return to the starting state.
- Sanitizers in CI for C++ memory bugs.
- Playwright for the React UI.

## Packaging

- Tauri app bundling the engine as a sidecar.
- macOS: Apple developer signing and notarization; entitlements for audio input (Digitakt recording) and loading third-party plugins.
- Windows: can ship unsigned initially (SmartScreen warning).

## Licensing

- AGPLv3 open source. JUCE 8 is AGPLv3, Tracktion Engine is GPLv3; the combined engine is distributed under AGPLv3, whose network clause also covers remote clients.
- A closed or paid version would require commercial JUCE and Tracktion licenses.

## Repository layout

- `engine/` — C++ engine (Tracktion Engine as a submodule in `engine/external/`).
- `schema/` — JSON Schema for the project model and messages, plus code generation.
- `mcp/` — TypeScript MCP server.
- `ui/` — Tauri + React app (from v1).
- `tests/` — render test songs and expected analysis values.
