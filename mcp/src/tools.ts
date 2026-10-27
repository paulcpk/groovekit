// The GrooveKit MCP tools: a few coarse tools over the engine API.

import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";
import { EngineClient, ticksPerBar } from "./engine.ts";
import type { Reference } from "./reference.ts";

export const instructions = `GrooveKit is a music production app (a DAW) that you operate through these tools. The human directs; you arrange, mix and master.

Model: channels (instruments: the built-in sampler with one or more sample layers, or a plugin) feed mixer inserts; patterns hold notes for any channels; the playlist holds pattern, audio and automation clips on numbered tracks (0, 1, 2, ...: any clip on any track, no setup needed; tracks only arrange, with a name, mute and solo). How a clip sounds and where it plays is set by its channel and the mixer, never by its track. Inserts chain effects, sends and a fader, and feed other inserts or master.

Units: time is integer ticks at 960 per quarter note (in 4/4 a 16th is 240, a beat 960, a bar 3840). Volumes in dB, pan -1..1, velocity 1-127, keys are MIDI numbers (60 = C4). Sample paths are absolute or relative to the project folder.

Sounds: search_samples finds samples in the user's library by words, category, loop, tempo and key, with a description of each sound. Use one-shots as sampler layers; set rootKey from the result's rootKey so pitched samples play in tune, and pitchTracking false for drums. Use loops as audio clips: an audio clip plays the first layer of a sampler channel, so add a channel ({"group":"arrangement","instrument":{"type":"sampler","layers":[{"id":"sample","sample":path,"sourceTempo":loopTempo}]}}; sourceTempo stretches it to the song), then clips naming it. The layer's start/end, reverse, transpose and fine tune (pitch-shifted, length kept), gain, pan and envelope, and the channel's filter, volume, pan and insert apply to all its clips; a clip itself only has its position, offset and fades. group "arrangement" lists the channel apart from the pattern channels in the rack. describe_sample gives full measurements (spectrum bands, attack, pitch, loudness). Paths in results are absolute.

Plugins: the user's VST3/AU plugins are listed by plugins (scan first if the list is empty). GrooveKit ships its own synth, Triad (manufacturer GrooveKit, always listed): three oscillators (sine, triangle, square, saw, soft saw, pulse, noise) each with coarse and fine tuning, level, pan, phase and stereo detune, the second and third able to mix in, FM or ring-modulate the one before; unison; a filter (low-pass 24 or 12, high-pass, band-pass) with its own envelope; an amp envelope; an LFO; poly, mono or legato with glide. Set its parameters by name like any plugin's ("Osc 1 shape", "Cutoff"); a good starting point for basses, stabs, leads and pads before reaching for samples. describe_plugin gives a pluginRef and parameter names; use it in a channel instrument {"type":"plugin","plugin":pluginRef} or an insert effect {"type":"plugin","id":...,"plugin":pluginRef}, set "params" by name (normalized 0-1), and automate them with automation clips (an instrument's: scope channel with param its parameter id or name; an effect's: scope effect). A sampler channel's filterCutoff, filterResonance and filterEnvelopeAmount are automatable too, like any channel's volume and pan. Every insert effect, built-in or plugin, has a mix from 0 (the dry signal) to 1 (only the effect): mix reverbs and delays in rather than adding sends for everything; it is automatable as the effect parameter "mix".

Listening: GrooveKit can hear its sample library and the user's reference tracks (once its listening models are downloaded, a 400 MB download the user starts in the Browser). Choose sounds by how they sound, never by their names: two files called "stab" can have nothing in common. A style is a folder of reference tracks the user loves (styles); styles describe gives its tempo range, swing, loudness, the levels of drums, bass and music against the mix, and words for what each part sounds like. search_samples with style ranks the library by fit to that style for each sample's part (category), 0 being an average sample of the user's library and 100 as close as the references are to each other: build from the top (fit 50 and above), use sound (words like "dusty warm chord stab") to steer within the style, and like (a path) to find sounds that belong with one you chose, so the kit hangs together. Results say in words what each sample sounds like. After arranging, compare_to_style renders the song and reports how far each part is from the references and what differs most (tempo, swing, busy or sparse drums, balance, levels); fix the biggest differences and compare again. When the user says a sound fits or doesn't, record it with rate_sample: their verdicts steer later searches.

Recording: the record tool captures hardware like the user's Digitakt II per track into clips, with GrooveKit as MIDI clock master. After a recording, each take is an audio clip with its own sampler channel; cut (updateClip offset/length), copy and arrange them like any other.

Workflow: get_summary to see the song, get_schema once for exact shapes, edit_project to change it (commands apply atomically; errors name the offending field), render to hear it as numbers (LUFS, peak, onsets), then adjust and re-render. Every edit is undoable; edits made close together form one group that undo reverts in one step.`;

const GROUP_IDLE_MS = 120_000;

function text(value: unknown): CallToolResult {
  return { content: [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value, null, 1) }] };
}

function failure(error: unknown): CallToolResult {
  return { content: [{ type: "text", text: `Error: ${error instanceof Error ? error.message : String(error)}` }], isError: true };
}

async function attempt(fn: () => Promise<unknown>): Promise<CallToolResult> {
  try {
    return text(await fn());
  } catch (e) {
    return failure(e);
  }
}

/** Groups edits that happen close together, so one agent request undoes in one step. */
export class GroupTracker {
  private current = "";
  private last = 0;
  private count = 0;
  private readonly now: () => number;

  constructor(now: () => number = Date.now) {
    this.now = now;
  }

  next(): string {
    const t = this.now();
    if (!this.current || t - this.last > GROUP_IDLE_MS) this.current = `agent-${t}-${++this.count}`;
    this.last = t;
    return this.current;
  }

  /** Makes the next edit start a new group. */
  reset() {
    this.current = "";
  }
}

/** The agent's tools, on an MCP server (or anything with the same registerTool, like the app's own
    agent). reference describes the data model and commands (reference-files.ts in Node). */
export function registerTools(server: McpServer, engine: EngineClient, reference: Reference, groups = new GroupTracker()) {
  const { commandReference, typeDefinitions } = reference;
  const barToTicks = (bar: number) => Math.round((bar - 1) * ticksPerBar(engine.document));

  server.registerTool(
    "get_summary",
    {
      description: "Compact overview of the song: tempo, channels and their samples, patterns with note counts, the playlist with clip positions in bars (1-based), and the mixer.",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("getSummary"))
  );

  server.registerTool(
    "get_project",
    {
      description: "The exact song.json, or the part at a JSON Pointer (e.g. /patterns/0/notes, /mixer/inserts/2). Use it to see notes and full settings before editing.",
      inputSchema: { path: z.string().optional().describe("JSON Pointer; omit for the whole document") },
      annotations: { readOnlyHint: true },
    },
    ({ path }) => attempt(() => engine.call("get", { path: path ?? "" }))
  );

  server.registerTool(
    "get_schema",
    {
      description: "TypeScript definitions of the project model (Channel, Pattern, Note, Clip, Insert, Effect...) and of the edit commands, with field docs and defaults. Read once before building commands.",
      inputSchema: { which: z.enum(["project", "commands"]).optional().describe("Default: project") },
      annotations: { readOnlyHint: true },
    },
    async ({ which }) => text(typeDefinitions(which === "commands" ? "protocol" : "project"))
  );

  server.registerTool(
    "edit_project",
    {
      description: `Applies commands to the song as one atomic, undoable change. If any command fails or the result is invalid, nothing changes and the error names the field. Each command is an object with "type" plus the fields below (shapes in get_schema):\n${commandReference()}\n\nExample: [{"type":"updateChannel","channel":"kick","changes":{"volumeDb":-3}},{"type":"addNotes","pattern":"beat","notes":[{"channel":"hat","start":480,"length":120}]}]`,
      inputSchema: {
        commands: z.array(z.object({ type: z.string() }).passthrough()).min(1).describe("Commands, applied in order"),
        label: z.string().optional().describe("Short description for the history, e.g. \"Add hi-hat groove\""),
      },
    },
    ({ commands, label }) =>
      attempt(() => engine.call("execute", { commands, group: groups.next(), ...(label ? { label } : {}) }))
  );

  server.registerTool(
    "undo",
    {
      description: "Undoes the most recent group of changes (all edits made close together count as one group). Repeat to go further back.",
      inputSchema: { steps: z.number().int().min(1).max(50).optional().describe("Groups to undo; default 1") },
    },
    ({ steps }) =>
      attempt(async () => {
        let result: unknown;
        for (let i = 0; i < (steps ?? 1); i++) result = await engine.call("undo");
        groups.reset();
        return result;
      })
  );

  server.registerTool(
    "redo",
    {
      description: "Redoes the most recently undone group of changes.",
      inputSchema: { steps: z.number().int().min(1).max(50).optional() },
    },
    ({ steps }) =>
      attempt(async () => {
        let result: unknown;
        for (let i = 0; i < (steps ?? 1); i++) result = await engine.call("redo");
        groups.reset();
        return result;
      })
  );

  server.registerTool(
    "get_history",
    {
      description: "The change history: who changed what and when, and the current position.",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("getHistory"))
  );

  server.registerTool(
    "render",
    {
      description: "Renders the song (or bars of it) offline to a WAV (48 kHz 24-bit unless chosen) and returns its path with peak, true peak, RMS, integrated/short-term/momentary LUFS, loudness range, spectral balance (10 bands, dB share), stereo correlation and onset times. Use this to check levels, tone and timing after changes. With stems, each mixer insert is also rendered on its own (soloed) next to the mix, for the user to take elsewhere.",
      inputSchema: {
        startBar: z.number().min(1).optional().describe("First bar, 1-based; default the start"),
        endBar: z.number().min(1).optional().describe("Bar to stop before; default the end of the last clip"),
        tailSeconds: z.number().min(0).max(30).optional().describe("Extra time for reverb and release tails"),
        path: z.string().optional().describe("Where to write the WAV; default <project>/renders/"),
        sampleRate: z.number().min(8000).max(192000).optional(),
        bitDepth: z.union([z.literal(16), z.literal(24), z.literal(32)]).optional(),
        stems: z.boolean().optional(),
      },
    },
    ({ startBar, endBar, tailSeconds, path, sampleRate, bitDepth, stems }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("render", {
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
          ...(tailSeconds !== undefined ? { tailSeconds } : {}),
          ...(path !== undefined ? { path } : {}),
          ...(sampleRate !== undefined ? { sampleRate } : {}),
          ...(bitDepth !== undefined ? { bitDepth } : {}),
          ...(stems !== undefined ? { stems } : {}),
        });
      })
  );

  server.registerTool(
    "master",
    {
      description: "Brings the song to a target loudness with the master limiter (added last on master as \"master-limiter\" if missing): renders, measures integrated LUFS and adjusts the limiter's input gain until within 0.2 LU. Shape the tone first with your own chain on master, e.g. eq (gentle shelves) then compressor (ratio 2, thresholdDb -18, attackMs 30, releaseMs 100) for glue. Typical targets: -14 LUFS streaming, -9 to -7 club. Undoable like any edit.",
      inputSchema: {
        targetLufs: z.number().min(-40).max(-3).optional().describe("Default -14"),
        ceilingDb: z.number().min(-12).max(0).optional().describe("Limiter ceiling; default -1"),
        releaseMs: z.number().min(1).max(1000).optional(),
        startBar: z.number().min(1).optional().describe("Measure only from here; default the whole song"),
        endBar: z.number().min(1).optional(),
      },
    },
    ({ targetLufs, ceilingDb, releaseMs, startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("master", {
          ...(targetLufs !== undefined ? { targetLufs } : {}),
          ...(ceilingDb !== undefined ? { ceilingDb } : {}),
          ...(releaseMs !== undefined ? { releaseMs } : {}),
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
          group: groups.next(),
        });
      })
  );

  server.registerTool(
    "transport",
    {
      description: "Plays the song (or one pattern on a loop) through the user's speakers, stops it, or moves the playhead, so the user can listen. loop repeats bars until stopped. Stop keeps the position. Rendering doesn't need this.",
      inputSchema: {
        action: z.enum(["play", "stop", "state"]),
        fromBar: z.number().min(1).optional().describe("For play: start here (1-based)"),
        loopBars: z.tuple([z.number().min(1), z.number().min(1)]).optional().describe("For play: [first bar, bar to stop before] to loop"),
        pattern: z.string().optional().describe("For play: loop this pattern instead of the song (pattern mode, as in the user's PAT switch)"),
      },
    },
    ({ action, fromBar, loopBars, pattern }) =>
      attempt(async () => {
        await engine.connect();
        if (action === "play" && pattern) {
          await engine.call("transport.setMode", { mode: "pattern", pattern });
          return engine.call("transport.play");
        }
        if (action === "play" && (fromBar !== undefined || loopBars)) await engine.call("transport.setMode", { mode: "song" });
        if (action === "play") {
          await engine.call("transport.setLoop", loopBars ? { start: barToTicks(loopBars[0]), end: barToTicks(loopBars[1]) } : {});
          if (fromBar !== undefined || loopBars) await engine.call("transport.setPosition", { position: barToTicks(fromBar ?? loopBars![0]) });
        }
        return engine.call(`transport.${action}`);
      })
  );

  server.registerTool(
    "project",
    {
      description: `The project (a folder holding song.json, its history and recordings) that the engine has open, shared with the user's GrooveKit window.
- current: its folder and the recently opened ones.
- open: opens another project folder.
- new: creates a project in a new, empty folder and opens it.
- saveAs: copies this project to a new, empty folder and opens the copy. (Every change is saved as it's made; there's no separate save.)
Opening, creating or saving as switches the user's window too, so only do it when the user asks.`,
      inputSchema: {
        action: z.enum(["current", "open", "new", "saveAs"]),
        path: z.string().optional().describe("For open, new and saveAs: the project folder (absolute)"),
      },
    },
    ({ action, path }) =>
      attempt(async () => {
        await engine.connect();
        if (action === "current") return engine.call("project.recent");
        if (!path) throw new Error(`${action} needs a path`);
        return engine.call(`project.${action}`, { path });
      })
  );

  server.registerTool(
    "record",
    {
      description: `Records audio inputs into clips, e.g. the user's Digitakt II over USB (16 tracks; each can be an audio or a MIDI track, so some outputs are silent).
- devices: lists audio input devices, inputs and MIDI outputs.
- setup: selects the input device (e.g. "Digitakt II") and turns on MIDI clock to a MIDI output (e.g. "Elektron Digitakt II"), so the hardware starts and stops with GrooveKit and follows its tempo.
- start: records the given inputs (or "all": Main, Track 1-16 and the Delay, Reverb and Chorus returns; per-track outputs are dry) from fromBar for bars bars, each into its own insert and playlist track (named after the input). Silent takes are skipped, and each take's channel gain is set so it plays at about -20 LUFS (hardware outputs arrive quiet). Returns at once; the takes become clips when it stops.
- stop: stops early.
- calibrate: once per setup, with the hardware playing clean hits on the given input (a kick track), measures the round trip so takes land on the grid.`,
      inputSchema: {
        action: z.enum(["devices", "setup", "start", "stop", "calibrate"]),
        inputDevice: z.string().optional().describe("For setup"),
        clockOutput: z.string().optional().describe("For setup: MIDI output to send clock to"),
        inputs: z.union([z.literal("all"), z.array(z.string())]).optional().describe("For start; default all"),
        fromBar: z.number().min(1).optional().describe("For start; default 1"),
        bars: z.number().min(1).optional().describe("For start (default 8) and calibrate (default 2)"),
        input: z.string().optional().describe("For calibrate; default Track 1"),
      },
    },
    (args) =>
      attempt(async () => {
        await engine.connect();
        switch (args.action) {
          case "devices": {
            const audio = await engine.call<any>("audio.devices");
            return { audio, inputs: (await engine.call<any>("record.inputs")).inputs, midi: await engine.call("midi.devices") };
          }
          case "setup": {
            if (args.inputDevice) await engine.call("audio.setDevice", { input: args.inputDevice });
            if (args.clockOutput) await engine.call("midi.setClock", { output: args.clockOutput, enabled: true });
            return { inputs: (await engine.call<any>("record.inputs")).inputs, alignment: await engine.call("record.alignment") };
          }
          case "start": {
            const names: string[] = !args.inputs || args.inputs === "all"
              ? ["Main", ...Array.from({ length: 16 }, (_, i) => `Track ${i + 1}`), "Delay", "Reverb", "Chorus"]
              : args.inputs;
            const slug = (n: string) => "rec-" + n.toLowerCase().replace(/[^a-z0-9]+/g, "");
            const doc = engine.document!;
            const commands: unknown[] = [];
            // Each input records onto the playlist track of its name, or the next free one below the song.
            const used = new Set<number>([...doc.playlist.clips.map((c) => c.track), ...doc.playlist.tracks.flatMap((t, i) => (t.name ? [i] : []))]);
            let next = Math.max(-1, ...used) + 1;
            const tracks = new Map<string, number>();
            for (const name of names) {
              const id = slug(name);
              if (!doc.mixer.inserts.some((i) => i.id === id)) commands.push({ type: "addInsert", insert: { id, name } });
              const existing = doc.playlist.tracks.findIndex((t) => t.name === name);
              const track = existing >= 0 ? existing : next++;
              if (existing < 0) commands.push({ type: "updateTrack", track, changes: { name } });
              tracks.set(name, track);
            }
            if (commands.length) await engine.call("execute", { commands, group: groups.next(), label: "Add recording tracks" });
            return engine.call("record.start", {
              targets: names.map((name) => ({ input: name, insert: slug(name), track: tracks.get(name) })),
              fromBar: args.fromBar ?? 1,
              bars: args.bars ?? 8,
              dropSilent: true,
              normalizeLufs: -20,
            });
          }
          case "stop":
            return engine.call("record.stop", { group: groups.next() });
          case "calibrate":
            return engine.call("record.calibrate", { input: args.input ?? "Track 1", bars: args.bars ?? 2 });
        }
      })
  );

  server.registerTool(
    "analyze_mix",
    {
      description: "Renders the mix plus one stem per channel (with its audio clips) and reports: the mix's loudness, true peak, loudness range, spectral balance and stereo image; each source's loudness and share of every frequency band; and masking, i.e. pairs of sources that both carry a big share of the same band (e.g. kick and bass at 60-120 Hz). Slower than render: one render per source.",
      inputSchema: {
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
    },
    ({ startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("analyzeMix", {
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
        });
      })
  );

  server.registerTool(
    "analyze_audio",
    {
      description: "The same measurements as render, for any audio file or a time range of it (seconds).",
      inputSchema: {
        path: z.string(),
        startSeconds: z.number().min(0).optional(),
        endSeconds: z.number().min(0).optional(),
      },
      annotations: { readOnlyHint: true },
    },
    ({ path, startSeconds, endSeconds }) => attempt(() => engine.call("analyze", { path, start: startSeconds, end: endSeconds }))
  );

  server.registerTool(
    "search_samples",
    {
      description: "Searches the user's sample library. Every word in query must match the file name, its folders or its category. With style, sound or like (needs the listening models), results are ranked by how they sound: fit (to the style, for each sample's part: 0 an average library sample, 100 as close as the style's references are to each other) and match (to the words or the sample: the share of the library it beats, 0-100); combine them with the filters, e.g. category and loop. Results give the path, a description of the sound, sounds (words for what it sounds like), category, loop, tempo, key, duration, rootKey (detected pitch) where known, and the user's rating.",
      inputSchema: {
        query: z.string().optional().describe("Words in the file name or folders, e.g. \"kick\" or \"vinyl\""),
        style: z.string().optional().describe("A style name (styles list): rank by fit to it"),
        sound: z.string().optional().describe("What it should sound like, in words, e.g. \"dusty warm chord stab\" or \"rumbling distorted kick\""),
        like: z.string().optional().describe("Path of a sample (or any audio file) it should sound like"),
        role: z.enum(["kick", "hats", "drums", "bass", "music", "kickHit", "hatHit", "percHit"]).optional().describe("The part to judge fit for, if not the sample's category (loops: kick, hats, drums, bass, music; one-shots: kickHit, hatHit, percHit)"),
        category: z.enum(["kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "bass", "synth", "vocal", "fx", "drums", "other"]).optional(),
        loop: z.boolean().optional().describe("true for loops only, false for one-shots only"),
        tempo: z.number().optional().describe("BPM; also matches half and double time"),
        key: z.string().optional().describe("e.g. \"Am\", \"F# minor\""),
        minDuration: z.number().optional().describe("Seconds"),
        maxDuration: z.number().optional().describe("Seconds"),
        limit: z.number().int().min(1).max(100).optional().describe("Default 20"),
      },
      annotations: { readOnlyHint: true },
    },
    (args) => attempt(() => engine.call("library.search", args))
  );

  server.registerTool(
    "describe_sample",
    {
      description: "Full measurements of one audio file: duration, loudness, attack and decay, spectral centroid and band energy (sub, low, mid, high), detected pitch, onset count and tempo, plus its classification and, once GrooveKit can listen, words for what it sounds like and (with style) its fit. Works for files outside the library too.",
      inputSchema: { path: z.string(), style: z.string().optional().describe("A style name: also give its fit") },
      annotations: { readOnlyHint: true },
    },
    ({ path, style }) => attempt(() => engine.call("library.describe", { path, ...(style ? { style } : {}) }))
  );

  server.registerTool(
    "styles",
    {
      description: "The user's styles: folders of reference tracks GrooveKit has listened to. list (with listening status: models, library indexing, a running analysis), describe (tempo range, swing, onsets per beat, loudness, spectral balance, each part's level against the mix, and words for what each part sounds like), add (analyses a folder of tracks in the background, about 15 s per track; adding it again picks up new tracks), remove. download fetches the listening models (about 400 MB): only when the user has agreed to it.",
      inputSchema: {
        action: z.enum(["list", "describe", "add", "remove", "download"]),
        name: z.string().optional().describe("The style, for describe and remove; for add, a name (default: the folder's)"),
        path: z.string().optional().describe("Folder of reference tracks, for add"),
      },
    },
    ({ action, name, path }) =>
      attempt(async () => {
        if (action === "download") return engine.call("listening.download", {});
        if (action === "add") return engine.call("styles.add", { path, ...(name ? { name } : {}) });
        if (action === "list") return { ...((await engine.call("styles.list", {})) as object), listening: await engine.call("listening.status", {}) };
        return engine.call(`styles.${action}`, { name });
      })
  );

  server.registerTool(
    "compare_to_style",
    {
      description: "Listens to the song against a style: renders the mix and hears it exactly as the style's references were heard (six stretches split into drums, bass and the rest), then reports each part's fit to the references with words for how both sound (mix, drums, kick, hats, bass, music, and single kick, hat and percussion hits), tempo, swing and hits per beat of the hats, loudness, spectral balance and each part's level against the mix, with notes on the biggest differences, most important first. channels says which channels mostly play which part. Takes up to a minute.",
      inputSchema: {
        style: z.string(),
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
      annotations: { readOnlyHint: true },
    },
    ({ style, startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("styles.compare", {
          name: style,
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
        });
      })
  );

  server.registerTool(
    "rate_sample",
    {
      description: "Records the user's verdict on a sample: it fits (true) or doesn't (false) a style (or any style without one); null forgets it. Later searches with that style rank it, and sounds like it, up or down.",
      inputSchema: {
        path: z.string(),
        fits: z.boolean().nullable(),
        style: z.string().optional(),
      },
    },
    ({ path, fits, style }) => attempt(() => engine.call("library.rate", { path, fits, ...(style ? { style } : {}) }))
  );

  server.registerTool(
    "sample_library",
    {
      description: "Manages the sample library: status (folders, sample count, scan progress), addFolder (indexes a folder and scans it in the background), removeFolder, or scan (rescans changed files).",
      inputSchema: {
        action: z.enum(["status", "addFolder", "removeFolder", "scan"]),
        path: z.string().optional().describe("Folder, for addFolder and removeFolder"),
      },
    },
    ({ action, path }) => attempt(() => engine.call(`library.${action}`, path ? { path } : {}))
  );

  server.registerTool(
    "plugins",
    {
      description: "The user's VST3 and AU plugins: list (filtered by words in name, manufacturer or category, and by instrument or effect), scan (finds newly installed plugins, takes about a minute; progress via status), or status.",
      inputSchema: {
        action: z.enum(["list", "scan", "status"]).optional().describe("Default list"),
        query: z.string().optional(),
        instrument: z.boolean().optional().describe("true for instruments, false for effects"),
      },
      annotations: { readOnlyHint: false },
    },
    ({ action, query, instrument }) =>
      attempt(() =>
        engine.call(`plugins.${action ?? "list"}`, {
          ...(query ? { query } : {}),
          ...(instrument !== undefined ? { instrument } : {}),
        })
      )
  );

  server.registerTool(
    "describe_plugin",
    {
      description: "Loads a plugin and lists its parameters (id, name, normalized value, display text), plus a pluginRef to use in commands. Instruments like the Digitakt have thousands of parameters: filter with query (words in the name, e.g. \"T1 filter\").",
      inputSchema: {
        uid: z.string().describe("From plugins"),
        query: z.string().optional(),
        limit: z.number().int().min(1).max(1000).optional().describe("Default 200"),
      },
      annotations: { readOnlyHint: true },
    },
    ({ uid, query, limit }) => attempt(() => engine.call("plugins.describe", { uid, ...(query ? { query } : {}), ...(limit ? { limit } : {}) }))
  );

  server.registerTool(
    "list_effects",
    {
      description: "Built-in effects (eq, compressor, multiband, limiter, reverb, delay, phaser, chorus, distortion, exciter, bitcrusher, stereo, lowpass, highpass) with every parameter's id, unit, range, default, choices (for parameters that pick an option by index) and whether it can be automated; defaultMix is where a new one's mix usually starts; presets are starting points (copy their params, and mix, into the effect).",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("listEffects"))
  );
}
