# GrooveKit

An agent-first music production app, with AI agents as first-class producers. The human is the director; the agent arranges, mixes, automates and masters.

Songs are built from channels (a built-in multi-layer sampler, GrooveKit's own Triad synth, or hosted VST3/AU instruments), patterns of notes that any number of channels share, a playlist of free tracks holding pattern, audio and automation clips, and a mixer of inserts with effects, sends and routing. Everything the UI can do, an agent can do through the same API, and every change is undoable. Hum a melody into a voice message, or point at a reference track, and the agent turns it into notes in the song's key; every sample in the library has a key or a root note to match it against.

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

The agent panel has a Notes tab for what the agent should always keep in mind ("prefer loops over one-shots", what a style should sound like), for every song or just the open one; switch a note off to set it aside. The in-app agent reads the notes that apply before every request, and Claude Code gets them through the `notes` tool.

Songs are JSON files described by [schema/project.schema.json](schema/project.schema.json). The examples in [tests/songs](tests/songs) double as render tests.

### Listening

File names say little about how a sample sounds, so GrooveKit can listen. In the Browser, under Listening, download the listening models once (about 400 MB) and GrooveKit hears every sample in your library. Then add a style: a folder of tracks you love. GrooveKit splits them into drums, bass and the rest and learns what each part sounds like, and how the tracks move (tempo, swing, density) and balance. The Browser can then rank samples by how well they fit the style, search by describing a sound ("dusty warm chord stab"), and find sounds like one you picked; thumbs up or down teaches it your taste: from a couple of verdicts GrooveKit learns what the sounds you like have in common and ranks the rest of your library by it. The agent does the same: it builds songs from sounds that fit the style and compares the result with your references to see what's off. Everything runs on your Mac.

### Effects and the Triad synth

Every built-in effect has a window that shows what it does as it does it. The compressor draws its curve with the sound riding it and the last seconds of level and gain reduction; the multiband compressor splits the spectrum into three or four bands you drag, each with its waveform scrolling past, its thresholds to drag, its own pull-down and lift-up and its stereo width, then a master compressor and a ceiling; the exciter is a warm tube, tape, transformer or console stage with a VU meter; the bitcrusher shows its steps on a little LCD; the stereo field routes each side to each side and shows the image on a vectorscope. Each comes with presets. The sampler draws its sample in detail as it plays (reversed, faded, looped), with envelopes for volume, cutoff, resonance, pan and pitch that a played note runs through, an LFO for each, and transient shaping. Its Grains tab plays any sample granularly: grains read at the key's pitch while a playhead moves through the sample at its own speed, so the pitch and the timing are apart (a key up sounds higher but not faster, half speed is twice as long at the same pitch, zero holds a texture), with size, density, spray, detune, width and shape, from stutters with space between the grains to smooth clouds.

Triad is GrooveKit's own synth: three oscillators with FM and ring modulation between them, unison, a filter with its own envelope, an LFO and glide, with presets from sub bass to chord stabs. It's a VST3 that comes with the app, ready in any channel (+ Channel → Triad synth).

## Repository layout

- `engine/` — C++ engine: model, commands and history, API server, compiler to Tracktion, sampler, mixer, effects, renderer, analysis, CLI
- `schema/` — JSON Schemas for the project and the command protocol, and the C++/TypeScript generator (`pnpm codegen`)
- `plugins/triad/` — the Triad synth, a JUCE VST3
- `mcp/` — the MCP server for agents
- `ui/` — the app: React UI and the Tauri desktop shell
- `tests/` — render test songs and the fixture samples they use

## Privacy

GrooveKit runs on your Mac and sends nothing anywhere by itself: no account, analytics, telemetry or update checks, and the engine only accepts connections from your own computer.

The agent is the exception, when you use it. The agent panel in the app sends your messages, and what the agent reads of the song to carry them out (track and sample names, notes, mix settings, sample file paths, analysis results), to Anthropic's API with your own key; the key and the conversations are stored on your Mac (conversations in GrooveKit's app data folder, where you can reopen or delete them from the agent panel). Notes you leave for the agent in its Notes tab ("prefer loops over one-shots") are sent with every request they apply to. Voice messages are transcribed on your Mac by Whisper (whisper.cpp); only the text is sent, like a typed message. The speech model is downloaded once from Hugging Face, when you first use the microphone and agree to it. Listening (hearing your samples and reference tracks) also runs on your Mac: its models are downloaded once, from Hugging Face and GitHub, when you ask for them, and your audio isn't sent anywhere. Search results the agent reads include words for how samples sound. With Claude Code or another MCP client, the same goes to whichever AI provider that client uses. See [groovekit.org/privacy](https://groovekit.org/privacy/).

## License

AGPLv3 (Settings → About → Licenses in the app lists the third-party software it includes). GrooveKit builds on [JUCE 8](https://juce.com) (AGPLv3), [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) (GPLv3), [Rubber Band](https://breakfastquay.com/rubberband/) (GPLv2+) and [IXWebSocket](https://github.com/machinezone/IXWebSocket) (BSD).

Bug reports and feature requests are welcome as [issues](https://github.com/paulcpk/groovekit/issues/new/choose); pull requests aren't accepted. See [CONTRIBUTING.md](CONTRIBUTING.md).
