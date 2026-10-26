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

While playing, `transport` notifications (`{ playing, recording, position, seconds }`) arrive about 30 times a second. While there's signal, `levels` notifications (`{ inserts: { id: [left, right] }, channels: { id: peak } }`, linear peaks since the last one) arrive at the same rate, and once more when it stops.

A serving engine registers in `~/Library/Application Support/GrooveKit/engines/<pid>.json` (`{ url, port, pid, project }`, rewritten when it opens another project), so other clients can connect to it rather than start another engine on the same project; an engine refuses to serve a project another one serves.

## Methods

| Method | Params | Result |
| --- | --- | --- |
| `hello` | `origin?`: name recorded in history for this connection's changes (`ui`, `agent`, ...) | `{ server, protocol, project }` |
| `execute` | `commands`: Command[], `group?`, `label?` | `{ entry, label }` |
| `undo` / `redo` | `wholeGroup?` (default true) | `{ changed, head }` |
| `get` | `path?`: JSON Pointer into song.json (default: everything) | the value |
| `getSummary` | | compact overview with bars, note counts and effect chains |
| `getHistory` | | `{ entries: [{ id, parent?, time, origin, group?, label }], head }` |
| `render` | `start?`, `end?` (ticks), `tailSeconds?`, `path?` | `{ path, analysis }` (the detailed analysis, as for `analyze`) |
| `analyze` | `path`, `start?`, `end?` (seconds) | `{ peakDb, truePeakDb, rmsDb, crestDb, integratedLufs, momentaryMaxLufs, shortTermMaxLufs, loudnessRangeLu, spectrum, stereo, onsets }` |
| `master` | `targetLufs?` (default -14), `ceilingDb?` (-1), `releaseMs?`, `start?`, `end?`, `group?` | `{ inputGainDb, integratedLufs, truePeakDb, reachedTarget, renders }`; sets the master limiter as one history entry |
| `analyzeMix` | `start?`, `end?` (ticks), `tailSeconds?` | `{ mix, bands, stems: [{ source, integratedLufs, peakDb, bandShare }], masking: [{ a, b, bands }] }` |
| `listEffects` | | built-in effects with parameter names, units and ranges |
| `transport.state` / `transport.play` / `transport.stop` | | `{ playing, recording, position (ticks), seconds, mode, pattern? }`; stop keeps the position |
| `transport.setPosition` | `position` (ticks) | transport state |
| `transport.setLoop` | `start`, `end` (ticks); omit both to stop looping | transport state; in pattern mode it's kept for the song |
| `transport.setMode` | `mode` (`song` or `pattern`), `pattern` (for pattern mode) | transport state; pattern mode plays one pattern on a loop (positions within it), and either way playback goes back to the start. Recording switches to song mode |
| `record.inputs` | | `{ inputs: [name] }`; channels named "X L"/"X R" (e.g. the Digitakt's) are paired into stereo inputs named "X" |
| `record.start` | `targets: [{ input, track, insert? }]` (insert defaults to master), `fromBar?`, `bars?` (stops by itself), `dropSilent?`, `normalizeLufs?` (sets each take's clip gain) | transport state |
| `record.calibrate` | `input?` (default "Track 1"), `bars?` (2) | `{ alignmentMs, spreadMs, hits, device }`: records clean hits in time with GrooveKit (e.g. a kick on clock-synced hardware) and stores how much later that device's takes are placed |
| `record.alignment` | `ms?` | `{ alignmentMs }` for the current input device |
| `midi.devices` | | `{ outputs: [{ name, enabled, clock }], inputs }` |
| `midi.setClock` | `output`, `enabled?` (true) | same as `midi.devices`; sends clock, start and stop so hardware follows GrooveKit |
| `record.stop` | `group?` | `{ clips: [id], transport }`; the takes become audio clips (files in `<project>/recordings`) |
| `audio.peaks` | `path` (absolute or relative to the project), `resolution?` (peaks a second, default 100) | `{ durationSeconds, sampleRate, channels, resolution, peaks: [0..1] }`, the highest level of each slice over all channels, for drawing waveforms |
| `preview.play` / `preview.stop` | `path`, `gainDb?` (-6) | `{ playing }`; auditions a file on the audio device, independent of the song |
| `audio.devices` | | `{ types: [{ type, outputs, inputs }], current: { type, output, input, bluetoothInput, sampleRate, bufferSize } }` |
| `audio.setDevice` | `type?`, `output?`, `input?` (`""` for none), `sampleRate?`, `bufferSize?` | same as `audio.devices`. A Bluetooth headset's microphone (which switches it to call quality) is only opened when chosen as `input`; the engine closes one that macOS made the default |
| `plugins.scan` / `plugins.status` | | `{ running, scanned, total, current, plugins }`; `pluginScanProgress` notifications while scanning |
| `plugins.list` | `query?`, `instrument?` | `{ plugins: [{ uid, name, manufacturer, format, instrument, category, inputs, outputs }] }` |
| `plugins.describe` | `uid`, `query?`, `limit?` | the plugin with `parameterCount`, `parameters: [{ id, name, value, text }]` and a `pluginRef` for commands |
| `plugins.openEditor` / `plugins.closeEditor` | `channel`, or `insert` and `effect` | `{ open }`; shows or closes a hosted plugin's own window. Closing it there saves its state into the project (as `captureState`); its state also carries over when the song is rebuilt |
| `plugins.captureState` | `channel`, or `insert` and `effect` | `{ state, bytes, changed }`; saves the live plugin's state to `<project>/plugins/` and points the model at it (undoable) |
| `project.recent` | | `{ projects: [{ path, name, modified }], current }`: project folders opened lately, most recent first (`modified` in ms since 1970) |
| `project.open` | `path` | `{ project }`; switches the engine (and every client) to that project folder, which must hold a song.json. Refused if another engine serves it |
| `project.new` | `path` | `{ project }`; creates a project in a new or empty folder and switches to it |
| `project.saveAs` | `path` | `{ project }`; copies the project folder (song, history, recordings, plugin states) to a new or empty folder and switches to the copy. Every change is already written to song.json as it's made, so there's no separate save |
| `library.status` | | `{ folders, autoScan, progress: { running, filesFound, analysed, unchanged, failed, samples } }` |
| `library.setAutoScan` | `enabled` | same as status; whether the engine scans for new and changed files when it starts |
| `library.addFolder` / `library.removeFolder` | `path`, `scan?` (default true) | same as status |
| `library.scan` | | progress; `libraryProgress` notifications follow |
| `library.search` | `query?`, `category?`, `loop?`, `tempo?`, `key?`, `minDuration?`, `maxDuration?`, `limit?` | `{ results: [{ path, description, category, loop, tempo?, key?, durationSeconds, rootKey? }], librarySize }` |
| `library.list` | `path?` | `{ folders: [{ path, name, samples }], samples: [...] }`: the indexed subfolders (with how many samples are below each) and samples directly in a folder, in Finder order; without `path`, the library folders |
| `library.describe` | `path` | the entry with full `features` and `classification` |

## Commands

`execute` applies its commands atomically. If the resulting project fails the schema or the reference checks, nothing changes and the error says where:

```json
{ "jsonrpc": "2.0", "id": 3, "error": { "code": 1, "message": "/channels/0: unknown property \"volme\"" } }
```

Commands add, update or remove channels, sampler layers, patterns, notes, playlist tracks, clips, inserts, effects and sends. `update*` commands take a `changes` object; `null` removes an optional field. Removals cascade: removing a channel removes its notes and the automation targeting it. A raw `patch` command (RFC 6902 add/remove/replace) covers everything else.

## History

Each `execute` creates one history entry: the commands, the forward ops and the inverse ops. Entries form a tree: undo moves to the parent, and a change after an undo starts a new branch. Redo follows the newest branch. Entries sharing a `group` undo and redo together. The MCP server uses this so one agent request undoes in one step.

The project folder holds `song.json` (rewritten after every change) and `history.jsonl` (appended), so a restarted engine continues where it left off.

## Error codes

| Code | Meaning |
| --- | --- |
| -32700 / -32600 / -32601 / -32602 | JSON-RPC parse error, invalid request, unknown method, invalid params |
| 1 | The command couldn't be applied; nothing changed |
| 2 | Another operation failed (render, file access) |
