# GrooveKit — Engine API

The engine serves JSON-RPC 2.0 over a WebSocket on `ws://127.0.0.1:7878` (`groovekit-engine serve <project-folder> --port <port>`). Every client, whether UI, MCP server or script, uses the same API.

Types live in the schemas: [project.schema.json](../schema/project.schema.json) for the song and [protocol.schema.json](../schema/protocol.schema.json) for commands and history entries. Generated TypeScript types are in `@groovekit/schema/project` and `@groovekit/schema/protocol`.

## Connecting

On connect the engine sends a `snapshot` notification with the full document. After that, every change from any client arrives as a `patch` notification. Apply `ops` (JSON Patch) to the previous document to stay in sync.

```json
{ "jsonrpc": "2.0", "method": "snapshot", "params": { "document": { "...": "song.json" }, "head": 12 } }
{ "jsonrpc": "2.0", "method": "patch", "params": {
    "kind": "execute", "entry": 13, "head": 13, "origin": "agent", "label": "Set tempo to 128",
    "ops": [ { "op": "add", "path": "/tempo", "value": 128 } ] } }
```

`kind` is `execute`, `undo` or `redo`. For undo, `ops` are the entry's inverse.

## Methods

| Method | Params | Result |
| --- | --- | --- |
| `hello` | `origin?`: name recorded in history for this connection's changes (`ui`, `agent`, ...) | `{ server, protocol }` |
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
| `library.status` | | `{ folders, progress: { running, filesFound, analysed, unchanged, failed, samples } }` |
| `library.addFolder` / `library.removeFolder` | `path`, `scan?` (default true) | same as status |
| `library.scan` | | progress; `libraryProgress` notifications follow |
| `library.search` | `query?`, `category?`, `loop?`, `tempo?`, `key?`, `minDuration?`, `maxDuration?`, `limit?` | `{ results: [{ path, description, category, loop, tempo?, key?, durationSeconds, rootKey? }], librarySize }` |
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
