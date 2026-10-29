# GrooveKit — Engine API

The engine serves JSON-RPC 2.0 over a WebSocket on `ws://127.0.0.1:7878` (`groovekit-engine serve <project-folder> --port <port>`). Every client, whether UI, MCP server or script, uses the same API.

Types live in the schemas: [project.schema.json](../schema/project.schema.json) for the song and [protocol.schema.json](../schema/protocol.schema.json) for commands and history entries. Generated TypeScript types are in `@groovekit/schema/project` and `@groovekit/schema/protocol`.

## Connecting

On connect the engine sends a `snapshot` notification with the full document. After that, every change from any client arrives as a `patch` notification. Apply `ops` (JSON Patch) to the previous document to stay in sync. When a client opens another project (`project.open` or `project.new`), every client gets a new `snapshot`, whose `project` names the folder now open; start over from it.

```json
{ "jsonrpc": "2.0", "method": "snapshot", "params": { "document": { "...": "song.json" }, "head": 12, "project": "/Users/you/Music/GrooveKit/Scratch" } }
{ "jsonrpc": "2.0", "method": "patch", "params": {
    "kind": "execute", "entry": 13, "head": 13, "origin": "agent", "label": "Set tempo to 128",
    "ops": [ { "op": "add", "path": "/tempo", "value": 128 } ] } }
```

`kind` is `execute`, `undo` or `redo`. For undo, `ops` are the entry's inverse.

While the listening models download, the library is heard or reference songs are heard into maps (and when a map or a choice changes), `listening` notifications carry the same status as `listening.status`.

While playing, `transport` notifications (`{ playing, recording, position, seconds }`) arrive about 30 times a second. While there's signal, `levels` notifications (`{ inserts: { id: [left, right] }, channels: { id: peak } }`, linear peaks since the last one) arrive at the same rate, and once more when it stops. When "Automate" is pressed in the bar above a plugin's window, a `createAutomation` notification (`{ target, value }`) asks the UI to make the automation clip.

A serving engine registers in `~/Library/Application Support/GrooveKit/engines/<pid>.json` (`{ url, port, pid, project }`, rewritten when it opens another project), so other clients can connect to it rather than start another engine on the same project; an engine refuses to serve a project another one serves.

## Methods

| Method | Params | Result |
| --- | --- | --- |
| `hello` | `origin?`: name recorded in history for this connection's changes (`ui`, `agent`, ...) | `{ server, protocol, project }`; `protocol` goes up when clients need methods an older engine lacks (2: song maps and sound matching), so a client that joined an older engine can say so |
| `execute` | `commands`: Command[], `group?`, `label?` | `{ entry, label }` |
| `undo` / `redo` | `wholeGroup?` (default true) | `{ changed, head }` |
| `get` | `path?`: JSON Pointer into song.json (default: everything) | the value |
| `getSummary` | | compact overview with bars, note counts and effect chains |
| `getHistory` | | `{ entries: [{ id, parent?, time, origin, group?, label }], head }` |
| `render` | `start?`, `end?` (ticks), `tailSeconds?`, `path?`, `format?` (`wav`, `flac` or `mp3`), `sampleRate?` (48000; MP3 44100 or 48000), `bitDepth?` (WAV 16, 24 or 32 float; FLAC 16 or 24), `bitrate?` (MP3 kbit/s, 96-320, default 320), `dither?` (true: triangular dither at 16 bits), `silenceBefore?`, `silenceAfter?` (seconds), `normalize?` (peak dB), `stems?` | `{ path, format, analysis, gainDb?, stems? }` (the detailed analysis, as for `analyze`, of the file as written; `gainDb` when normalised); with `stems`, each insert but master is also rendered on its own as `<name>-<insert>.<format>`, with the same silence and gain |
| `analyze` | `path`, `start?`, `end?` (seconds) | `{ peakDb, truePeakDb, rmsDb, crestDb, integratedLufs, momentaryMaxLufs, shortTermMaxLufs, loudnessRangeLu, spectrum, stereo, onsets }` |
| `master` | `targetLufs?` (default -14), `ceilingDb?` (-1), `releaseMs?`, `start?`, `end?`, `group?` | `{ inputGainDb, integratedLufs, truePeakDb, reachedTarget, renders }`; sets the master limiter as one history entry |
| `analyzeMix` | `start?`, `end?` (ticks), `tailSeconds?` | `{ mix, bands, stems: [{ source, integratedLufs, peakDb, bandShare }], masking: [{ a, b, bands }] }` |
| `listEffects` | | built-in effects with parameter names, units, ranges and `choices` (for parameters that pick an option by index), `defaultMix`, and their factory `presets` |
| `presets.list` | `kind` (an effect kind, or `sampler` for channel filter and envelope presets) | `{ factory: [preset], user: [preset] }`; an effect preset is `{ name, params, mix? }`, a channel preset `{ name, filter?, envelope? }` |
| `presets.save` / `presets.delete` | `kind`, and `preset` (with a `name`) or `name` | the list again; the user's presets are files in `<app data>/presets/<kind>/` |
| `voice.status` | `prepare?` (also load the model in the background, which takes a few seconds the first time) | `{ ready, downloading, progress, modelBytes, error? }`: whether Whisper's speech model is on this Mac |
| `voice.download` | | the status; downloads the model (190 MB) once into `<app data>/models`, with `voiceModel` notifications as it goes |
| `voice.transcribe` | `audio` (16-bit PCM at 16 kHz, mono, base64), `language?` (two letters; detected when absent) | `{ job }` at once; a `voiceTranscript` notification `{ job, text }` or `{ job, error }` follows |
| `mixer.spectrum` | `insert`, `effect` | `{ bins, fromHz, toHz }`: what the effect slot outputs (after its mix) as 160 dB values, log-spaced from 20 Hz to 20 kHz |
| `mixer.waveform` | `insert`, `effect`, `samples?` (1024) | `{ samples }`: the slot's latest output, mono |
| `mixer.meter` | `insert`, `effect`, `since?`, `samples?` | What one of GrooveKit's effects measures, for its window; `{}` for others. Compressor, multiband and exciter: `{ position, frames }`, a frame of numbers per 5 ms written after `since` (a `position` from before), or the last five seconds: compressor `[inputDb, outputDb, gainDb]`; multiband `[inputDb, outputDb]` then per band (always four) `inputDb, outputDb, gainDb` (gain negative when pulled down, positive when lifted); exciter `[inputDb, outputDb]` as RMS. Stereo field: `{ left, right }`, the latest `samples` (512) output samples. |
| `transport.state` / `transport.play` / `transport.stop` | | `{ playing, recording, position (ticks), seconds, mode, pattern? }`; stop keeps the position |
| `transport.setPosition` | `position` (ticks) | transport state |
| `transport.setLoop` | `start`, `end` (ticks); omit both to stop looping | transport state; in pattern mode it's kept for the song |
| `transport.setMetronome` | `enabled?`, `countInBars?` (0-2) | transport state (with `metronome`, `countInBars`); the click while playing, and the bars recordings count in with |
| `live.setChannel` | `channel` (or null) | `{ channel }`; MIDI keyboards (enabled ones) and the app's keyboard play this channel live |
| `live.noteOn` / `live.noteOff` | `key`, `velocity?`, `channel?` | `{}`; plays a note on the live channel from the app (piano roll preview, computer keyboard) |
| `record.notes` | `pattern`, `channel`, `quantize?` (ticks) | transport state; loops the pattern and records what's played on the channel into it after the count-in, until `record.stop` or `transport.stop` |
| `midi.setInput` | `input`, `enabled` | same as `midi.devices`, which lists `enabledInputs` too |
| `transport.setMode` | `mode` (`song` or `pattern`), `pattern` (for pattern mode) | transport state; pattern mode plays one pattern on a loop (positions within it), and either way playback goes back to the start. Recording switches to song mode |
| `record.inputs` | | `{ inputs: [name] }`; channels named "X L"/"X R" (e.g. the Digitakt's) are paired into stereo inputs named "X" |
| `record.start` | `targets: [{ input, track, insert? }]` (insert defaults to master), `fromBar?`, `bars?` (stops by itself), `dropSilent?`, `normalizeLufs?` (sets each take's clip gain) | transport state |
| `record.calibrate` | `input?` (default "Track 1"), `bars?` (2) | `{ alignmentMs, spreadMs, hits, device }`: records clean hits in time with GrooveKit (e.g. a kick on clock-synced hardware) and stores how much later that device's takes are placed |
| `record.alignment` | `ms?` | `{ alignmentMs }` for the current input device |
| `midi.devices` | | `{ outputs: [{ name, enabled, clock }], inputs }` |
| `midi.setClock` | `output`, `enabled?` (true) | same as `midi.devices`; sends clock, start and stop so hardware follows GrooveKit |
| `record.stop` | `group?` | `{ clips: [id], transport }`; each take becomes an audio clip with a sampler channel of its own (group "arrangement") (files in `<project>/recordings`) |
| `audio.peaks` | `path` (absolute or relative to the project), `resolution?` (peaks a second, default 100, at most 1000), `signed?` (also the lowest and highest values, and up to 48000 slices a second), `process?`, `punch?`, `tail?` (a sampler layer's processing: the wave as the layer plays it) | `{ durationSeconds, sampleRate, channels, resolution, timeScale, peaks: [0..1], min?: [-1..0], max?: [0..1] }`, the highest level of each slice over all channels (and with `signed`, the lowest and highest values), for drawing waveforms; `timeScale` is how much longer processing made the sound (a time in the file is that much later in the wave) |
| `preview.play` / `preview.stop` | `path`, `gainDb?` (-6), `from?` (seconds into the file), `under?` (a second file played in sync beneath it), `underLevel?` (0-1, default 1) | `{ playing }`; auditions a file on the audio device, independent of the song |
| `preview.setUnderLevel` | `level` (0-1) | `{ level }`; the level of the file playing beneath, changed while it plays (the pick window's mix slider) |
| `audio.devices` | | `{ types: [{ type, outputs, inputs }], current: { type, output, input, bluetoothInput, sampleRate, bufferSize } }` |
| `audio.setDevice` | `type?`, `output?`, `input?` (`""` for none), `sampleRate?`, `bufferSize?` | same as `audio.devices`. A Bluetooth headset's microphone (which switches it to call quality) is only opened when chosen as `input`; the engine closes one that macOS made the default |
| `plugins.scan` / `plugins.status` | | `{ running, scanned, total, current, plugins }`; `pluginScanProgress` notifications while scanning |
| `plugins.list` | `query?`, `instrument?` | `{ plugins: [{ uid, name, manufacturer, format, instrument, category, inputs, outputs }] }` |
| `plugins.describe` | `uid`, `query?`, `limit?` | the plugin with `parameterCount`, `parameters: [{ id, name, value, text }]` and a `pluginRef` for commands |
| `plugins.openEditor` / `plugins.closeEditor` | `channel`, or `insert` and `effect` | `{ open }`; shows or closes a hosted plugin's own window. Closing it there saves its state into the project (as `captureState`); its state also carries over when the song is rebuilt |
| `plugins.captureState` | `channel`, or `insert` and `effect` | `{ state, bytes, changed }`; saves the live plugin's state to `<project>/plugins/` and points the model at it (undoable) |
| `project.importFile` | `path` | `{ path }`; copies an audio file into `<project>/samples` (or finds it there) and returns the path to use, relative to the project |
| `project.importData` | `name`, `data` (base64) | `{ path }`; the same for a file's bytes, e.g. dropped from Finder into the web view |
| `project.collectSamples` | | `{ copied, updated }`; copies every sample from outside the project into `samples/` and points the song at the copies (one undoable change) |
| `project.recent` | | `{ projects: [{ path, name, modified }], current }`: project folders opened lately, most recent first (`modified` in ms since 1970) |
| `project.open` | `path` | `{ project }`; switches the engine (and every client) to that project folder, which must hold a song.json. Refused if another engine serves it |
| `project.new` | `path` | `{ project }`; creates a project in a new or empty folder and switches to it |
| `project.saveAs` | `path` | `{ project }`; copies the project folder (song, history, recordings, plugin states) to a new or empty folder and switches to the copy, saved |
| `project.save` | | `{ saved, head }`; every change is already written to song.json as it's made: saving marks the history entry the song was saved at. Every client gets a `saved` notification with the same fields; the `snapshot` carries `saved` too, and the song has unsaved changes while `head` differs from it |
| `project.revert` | | `{ saved, head }`; goes back to where the song was saved, as undo and redo steps (the changes since stay in the history, so they can be redone) |
| `library.status` | | `{ folders, autoScan, progress: { running, filesFound, analysed, unchanged, failed, samples } }` |
| `library.setAutoScan` | `enabled` | same as status; whether the engine scans for new and changed files when it starts |
| `library.addFolder` / `library.removeFolder` | `path`, `scan?` (default true) | same as status |
| `library.scan` | | progress; `libraryProgress` notifications follow |
| `library.starter` | | `{ installed, downloading, progress, totalBytes, files, folder, error? }`: the starter samples (public-domain drum machines, one-shots and loops; `samples/README.md`), whether they're in `folder` (`~/Music/Samples/GrooveKit Starter`) at the current version |
| `library.downloadStarter` | | the same; downloads and unpacks the pack into its folder, adds the folder to the library (unless a library folder holds it) and scans it. `starter` notifications carry the status while it downloads, the last with `installed` true |
| `library.search` | `query?`, `category?`, `loop?`, `tempo?`, `key?`, `fitsKey?` (`"song"` or a key: samples that go with it), `minDuration?`, `maxDuration?`, `limit?`; to rank by how samples sound (needs the listening models): `style?`, `sound?` (words), `like?` (a path), `role?` (`kick`, `hats`, `drums`, `bass`, `music`, `kickHit`, `hatHit` or `percHit`, if not the sample's category's) | `{ results: [{ path, description, category, loop, tempo?, sourceTempo? (a loop's tempo made exact by its length: the layer's `sourceTempo`), key?, keySource?, heardKey?, durationSeconds, rootKey?, keyFit?, sounds?, fit?, match?, rating?, taste? }], librarySize, heard?, taste? }`. `key` is the name's key, else the one heard (`keySource`: `name`, `sound` or `name and sound`; `heardKey` when the sound disagrees with the name); `keyFit` (with `fitsKey`): `{ fits, transpose }`, the semitones that put the sample exactly in the key (to the tonic for one-shots). `sounds` are words for what a sample sounds like; `fit` is its fit to the style in its part (0 an average library sample in that part, 100 as close as the style's tracks are to each other); `match` the share of the library it beats for likeness to the words or sample (0-100); `rating` the user's verdict for the style; `taste` (from two samples rated as fitting) how likely the user is to say the sample fits (0-100), learned from their verdicts, with the top-level `taste: { verdicts, confidence }` saying how much it has to go on. Ranked results only include samples the index has heard |
| `library.list` | `path?` | `{ folders: [{ path, name, samples }], samples: [...] }`: the indexed subfolders (with how many samples are below each) and samples directly in a folder, in Finder order; without `path`, the library folders |
| `library.describe` | `path`, `style?` | the entry with full `features` and `classification`, plus `sounds` (words) and with `style` its `fit` once GrooveKit can listen |
| `library.rate` | `path`, `fits` (true, false or null to forget), `style?` (none: any style) | `{ path, style, fits }`; the verdict ranks the sample, and samples that sound almost the same, up or down in searches with that style. Kept in `<app data>/taste.json` |
| `library.ratings` | | `{ ratings: [{ path, style, fits }] }` |
| `chats.list` | | `{ chats: [{ id, title, project, projectName, created, updated, messages }] }`: the in-app agent's conversations, most recently updated first (`project` is where each was last used; `messages` counts the user's) |
| `chats.get` | `id` | the conversation as saved: `{ id, title, project, projectName, created, updated, items, history }` |
| `chats.save` | `chat` (`id`: letters, digits and dashes, plus anything else the UI keeps) | the summary; saved to `<app data>/chats/<id>.json` with `updated` set to now |
| `chats.delete` | `id` | `{ deleted }` |
| `notes.list` | | `{ notes: [{ id, text, enabled, project?, projectName?, here, created, updated }] }`: the user's notes for the agent, oldest first; `project` is the song folder a note is for (none: every song), `here` whether it applies to the open song. Kept in `<app data>/agent-notes.json` |
| `notes.add` | `text`, `scope?` (`all`, the default, or `song`: the open song only) | the note. Every change sends a `notes` notification with the whole list |
| `notes.update` | `id`, `text?`, `enabled?`, `scope?` | the note |
| `notes.delete` | `id` | `{ deleted }` |
| `guides.list` | | `{ guides: [{ id, name, summary, builtIn, edited, song }], style }`: the style guides, built in and the user's own, by name; `edited`: the user's version (of a built-in one) or their own; `song`: the open song's style |
| `guides.get` | `id?` (an id or a name; none: the song's style) | `{ id, name, summary, text, targets, builtIn, edited }` |
| `guides.save` | `text`, `id?` (none: a new guide, its id from its name) | the guide. Kept in `<app data>/guides/<id>.md`, in place of a built-in one. Every change sends a `guides` notification with the list |
| `guides.remove` | `id` | `{ removed }`: `restored` (an edited built-in guide is back as it shipped), `deleted` (one of the user's own) or `nothing` |
| `guides.delete` | `id` | `{ removed }`: `deleted` for one of the user's own, `hidden` for a built-in one (listed with `hidden`, still found by id or name, so songs in its style keep it) |
| `guides.unhide` | `id` | puts a hidden built-in guide back in the list |
| `melody.voice` | `recording?` (a voice message's id, default the latest), `from?`, `to?` (seconds, else the sung part found), `tempo?` (`"song"`: hummed at the song's tempo), `key?`, `register?` (`bass`, `chords`, `lead`, `high`), `lowest?`, `highest?`, `snapToScale?` (default true) | `{ notes: [{ start, length, key, velocity }], length, heardTempo, heardKey?, key?, transposed, recording, from, to }`: the melody hummed or sung in a voice message, on the song's grid (ticks from its start) in the song's key (or `key`), moved by octaves into the register. `voiceTranscript` notifications carry `recording` and, when a sung part was heard, `melody: { from, to, notes }` |
| `melody.reference` | `track` (words of its file name), `style?`, `from?`, `length?` (seconds, default 24), `part?` (`lead`, `chords` or `bass`), `key?`, `register?`, `lowest?`, `highest?`, `snapToScale?` (default false) | the same, with `track`, `from`, `seconds` and `part`: a stretch of a style's reference track split into stems and transcribed (Basic Pitch, its 230 KB model downloaded next to the listening models the first time), its lead the top line of everything but drums and bass |
| `listening.status` | | `{ models: { ready, downloading, progress, totalBytes, error? }, index: { running, total, embedded, failed, samples, librarySize }, kicks: { running, total, measured, samples }, styles: { running, paused, names, queued, secondsPerSong, mapsVersion, style?, tracks?, analysed?, failed?, current?: { id, style, title, progress, started }, error? } }`; `mapsVersion` goes up with every change to a map or a choice |
| `listening.download` | | the status; downloads the listening models (ONNX Runtime, CLAP and htdemucs, about 400 MB) once into `<app data>/models/listening`, then hears the library. `listening` notifications follow |
| `listening.index` | | the status; hears library samples that haven't been yet (the engine also does this when it starts and after scans) |
| `styles.list` | | `{ styles: [{ name, folder, tracks, tempo?, guide }], progress }`: the styles with song maps in use; `guide`: the style guide with the same name, or null |
| `styles.references` | | `{ folder, exists, folders: [{ name, path, files, style, unheard, gone, guide }] }`: the references folder (`~/Music/GrooveKit/References`, or `GROOVEKIT_REFERENCES`) and each style folder in it (not `Albums`, `Mixes` or `Not used`): its audio files, the style heard from it, tracks new or changed since (`unheard`) and gone, and the guide with its name. Tracks are recognised by name, size and date, so a moved folder isn't heard again |
| `styles.createReferences` | | the same, after creating the folder |
| `styles.add` | `path` (a folder of reference tracks), `name?` (default: the folder's) | `{ name, progress }`; queues the folder's tracks not heard yet or changed since (not skipped ones) to be heard into maps |
| `maps.list` | | `{ folder, references, songs: [{ id, style, title, file?, status, inUse, error?, map?: { source, heardAt, analysis, tempo, lufs } }] }`: every song in the references folder's style folders and every map. `id` is `<style>/<title>` (the file name without its extension). `status`: `new`, `changed` (since it was heard), `older` (heard by an older hearing), `heard`, `queued`, `hearing`, `failed`, `skipped`, `imported` (someone's map; the audio isn't here) or `gone` (heard here, the audio since removed) |
| `maps.status` | | `{ running, paused, ready, current?: { id, style, title, progress, started }, queued: [{ id, style, title }], recent: [{ id, style, title, ok, seconds, at, error? }], secondsPerSong }`: the hearing queue (`ready`: the models are in; without them it waits) |
| `maps.get` | `id` | the map: `{ id, style, title, source, heardAt, inUse, current, score?, track: { path, analysis, tempo, swing?, density, lufs, balance, levels, sounds, arrangement? } }` (`sounds`: how many fingerprints per part). `arrangement` (heard from the whole track, hearing 6 on): `{ tempo, downbeat (s), seconds, bars, parts: { kick, rumble, bass, hats, percussion, music, vocal: { level: [dB per bar], on: "0110…" } }, loudness, low, brightness, density (per bar), sections: [{ start, end, role }], changes: [{ bar, part, what }] }`, bars from 0 and a section's end excluded; roles intro, groove, build, break, drop, outro. `score`: the arrangement as text |
| `maps.setInUse` | `ids`, `inUse` | `{ changed }`; a style learns from the maps in use (all, until unticked). While songs are being heard, the queue follows: unticked songs leave it (the one being heard stops and keeps the map it had), ticked ones join it |
| `maps.hear` | `ids` | `{ queued }`; queues the songs (with audio here) to be heard, again if they were, and stops skipping them |
| `maps.hearNew` | `style?` | `{ queued }`; queues every new, changed or failed song (of a style) |
| `maps.pause` / `maps.resume` | | hearing stops after the stretch it's on (the song starts over on resume); the queue is kept on disk and carries on after a restart unless paused |
| `maps.unqueue` | `ids` | takes songs out of the queue (the one being heard stops and keeps the map it had); `maps.clearQueue` takes all of them |
| `maps.forget` | `ids` | `{ forgotten }`; deletes the maps and skips the songs |
| `maps.unskip` | `ids` | stops skipping them |
| `maps.removeReferences` | `ids` | `{ moved }`; moves the songs' audio to `Not used/<style>` beside the style folders (nothing is deleted) and drops their maps |
| `match.kicks` | `id` (a song map) or `path` (a library sample) as the reference, `loops?` (true only loops, false only one-shots), `limit?` (20) | `{ reference: { id?, title?, style?, bars?, clean?, from?, to?, file?, kick, tempo? }, results: [{ path, name, category, loop, tempo?, score, measured, fingerprint?, differences, kick }], searched, measuring }`: the library's kicks and drum loops (measured in the background: `listening.status` `kicks`) ranked by how close their kick and rumble are to the reference's, measured 75% and fingerprint 25%. A map's kick is measured where it plays with least around it (`bars`, 1-based; `clean` when nothing but the low end plays there) |
| `match.audition` | the reference, `paths` (1-10), `tempo?` (the reference's), `bars?` (4) | `{ reference, tempo, results: [{ path, name, audition, score?, differences?, kick?, changes?, error? }] }`: each sample played in a groove (a one-shot on the reference's pattern, a loop stretched to the tempo), written to `audition` (a WAV in the cache) and measured like the reference; `changes`: what to do to come closer (`{ what, how }`) |
| `match.channel` | the reference, `channel`, `start?`, `end?` (ticks; default the first 8 bars) | `{ score, differences, notes, kick, reference, changes }`: the song's channel rendered alone and measured against the reference |
| `mix.slots` | `channels?` (default all), `apply?`, `onlyNew?` (skip channels with a slot), `style?` (default the song's), `group?` | `{ channels: [{ channel, name, part, range, guessed, insert, slot, proposed }], applied, style, fromReferences }`: each channel's part and frequency slot (`{ lowCutHz, highCutHz, steep?, topShelf?: { db, hz }, from }`); with `apply`, gives channels their part's proposed slot: its own insert when it shares one or plays into master, an EQ with id `slot` first on it, and `part` set, as one undo step per `group` |
| `mix.setSlot` | `channel`, `part?`, `lowCutHz?`, `highCutHz?` (0 or null for none), `steep?`, `topShelfDb?`, `topShelfHz?`, `group?` | the channel as in `mix.slots`; a new part starts from that part's slot |
| `mix.check` | `start?`, `end?` (ticks), `style?`, `listen?` (false: no rendering, only the arrangement, slots and keys), `maxParts?` (6) | `{ problems: [{ severity, kind, channels, what, fix }], channels: [{ channel, part, integratedLufs, peakDb, holds }], mix, references, style }`: renders each channel and lists what goes wrong between the parts, worst first; kinds: `noSlot`, `lowEnd`, `lowShared`, `masking`, `brightTop`, `dullTop`, `tooMuch`, `tooLittle`, `staticTop`, `key`, and from the arrangement over the whole song `tooManyParts` and `entersTogether`; `partsPerBar`: the parts heard in each bar (a bed of effects under most of the song not counted) |
| `mix.audition` | `channel?` (a channel; without one, each candidate plays in a phantom slot of its own through its part's slot: a loop back to back over the bars at its exact tempo, a one-shot or instrument playing notes), `paths` (1-10) or `candidates` (1-10: `{ path?, instrument?, notes?, part? }`), `notes?` and `part?` (for phantom slots; without notes, a bar of the part's usual figure), `start?`, `end?` (ticks; default the first 8 bars) | `{ channel, part, bars, current, candidates: [{ path, name, file, alone, rest, fit, covers, holds, levelDb, gainDb, key? }], note? }`: the song rendered with each candidate as the channel's first layer (`alone` and `rest`: the candidate alone and the song without it, for fading one under the other; `file`: both together), at the loudness of the sample it replaces (`gainDb`), and as it is (WAVs in the cache); `key` (`{ fits, transpose }`) for pitched parts when the song has a key; `fit` 0-100 is how much of the candidate's energy the rest leaves to it |
| `picks.offer` | `title`, `candidates` (1-10: `{ path?, instrument?, notes?, part?, audition?, label? }`: a sample or a whole instrument, with notes for a phantom slot), `channel?`, `notes?` and `part?` (for candidates in phantom slots), `start?`, `end?`, `style?`, `reference?` (`{ file, from?, title? }`) | `{ id }`; a candidate whose audition comes from `mix.audition` gets its `alone` and `rest` layers; the others are tried in the song first (when it has clips, or `channel` is given) as `mix.audition` would, over `start?`-`end?`; puts a choice to the user (a `picks` notification with the offer; the app opens a window to play and pick) |
| `picks.get` / `picks.list` | `id` / | the offer with `status` (`waiting` or `chosen`), `choice` and `note` (Other); the offers still waiting |
| `picks.choose` | `id`, `choice` (an index, or -1 for none), `note?` (with -1: Other, what the user wants instead) | `{ id, choice, note? }`; the pick fits the offer's style from then on (none: none of them does; Other is no verdict); a `picks` notification with the choice closes the windows |
| `maps.export` | `ids`, `path` (a .zip) | `{ path, count }`; the maps, one folder per style, with file names in place of paths and an `index.json` |
| `maps.import` | `path` | `{ added, kept }`; adds a zip's maps as imported; a song heard here keeps its own |
| `styles.describe` | `name` | `{ name, folder, tracks, trackNames, tempo: { median, min, max }, swing, onsetsPerBeat, lufs, balance (ten bands, dB of the mix), levels (each part against the mix, dB), sounds: { part: words } }` |
| `styles.compare` | `name`, `start?`, `end?` (ticks) | `{ style, parts: { part: { fit, sounds, referencesSound } }, tempo, groove: { swing?, hitsPerBeat }, referencesGroove, loudness, balance, levels, channels, notes }`: renders the mix and hears it as the style's tracks were heard (split into drums, bass and the rest), then compares part by part; `channels` lists which channels mostly play which part (by their first sample's category), and `notes` name the biggest differences first |

## Commands

`execute` applies its commands atomically. If the resulting project fails the schema or the reference checks, nothing changes and the error says where:

```json
{ "jsonrpc": "2.0", "id": 3, "error": { "code": 1, "message": "/channels/0: unknown property \"volme\"" } }
```

Commands add, update or remove channels, sampler layers, patterns, notes, clips, inserts, effects and sends; `updateInstrument` changes a sampler's own settings (polyphony, filter). Arrangement rows are numbers: `updateRow` names, colours, mutes or solos any of them, `insertRow` and `removeRow` move the rows below (and their clips) down or up, and `moveRows` moves rows with their clips and settings, so nothing sounds different. `update*` commands take a `changes` object; `null` removes an optional field. Removals cascade: removing a channel removes its notes, its audio clips and the automation targeting it. A raw `patch` command (RFC 6902 add/remove/replace) covers everything else.

## History

Each `execute` creates one history entry: the commands, the forward ops and the inverse ops. Entries form a tree: undo moves to the parent, and a change after an undo starts a new branch. Redo follows the newest branch. Entries sharing a `group` undo and redo together. The MCP server uses this so one agent request undoes in one step.

The project folder holds `song.json` (rewritten after every change) and `history.jsonl` (appended), so a restarted engine continues where it left off.

## Error codes

| Code | Meaning |
| --- | --- |
| -32700 / -32600 / -32601 / -32602 | JSON-RPC parse error, invalid request, unknown method, invalid params |
| 1 | The command couldn't be applied; nothing changed |
| 2 | Another operation failed (render, file access) |
