// The GrooveKit MCP tools: a few coarse tools over the engine API.

import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";
import { EngineClient, ticksPerBar } from "./engine.ts";
import { commandReference, typeDefinitions } from "./reference.ts";

export const instructions = `GrooveKit is a music production app (a DAW) that you operate through these tools. The human directs; you arrange, mix and master.

Model: channels (instruments: the built-in sampler with one or more sample layers) feed mixer inserts; patterns hold notes for any channels; the playlist has free tracks holding pattern, audio and automation clips. Inserts chain effects, sends and a fader, and feed other inserts or master.

Units: time is integer ticks at 960 per quarter note (in 4/4 a 16th is 240, a beat 960, a bar 3840). Volumes in dB, pan -1..1, velocity 1-127, keys are MIDI numbers (60 = C4). Sample paths are absolute or relative to the project folder.

Sounds: search_samples finds samples in the user's library by words, category, loop, tempo and key, with a description of each sound. Use one-shots as sampler layers; set rootKey from the result's rootKey so pitched samples play in tune, and pitchTracking false for drums. Use loops as audio clips (their tempo must match the song's for now). describe_sample gives full measurements (spectrum bands, attack, pitch, loudness). Paths in results are absolute.

Plugins: the user's VST3/AU plugins are listed by plugins (scan first if the list is empty). describe_plugin gives a pluginRef and parameter names; use it in a channel instrument {"type":"plugin","plugin":pluginRef} or an insert effect {"type":"plugin","id":...,"plugin":pluginRef}, set "params" by name (normalized 0-1), and automate them with effect-scope automation clips.

Recording: the record tool captures hardware like the user's Digitakt II per track into clips, with GrooveKit as MIDI clock master. After a recording, the takes are audio clips you can cut (updateClip offset/length), copy and arrange like any other.

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

export function registerTools(server: McpServer, engine: EngineClient, groups = new GroupTracker()) {
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
      description: "Renders the song (or bars of it) offline to a 48 kHz WAV and returns its path with peak, true peak, RMS, integrated/short-term/momentary LUFS, loudness range, spectral balance (10 bands, dB share), stereo correlation and onset times. Use this to check levels, tone and timing after changes.",
      inputSchema: {
        startBar: z.number().min(1).optional().describe("First bar, 1-based; default the start"),
        endBar: z.number().min(1).optional().describe("Bar to stop before; default the end of the last clip"),
        tailSeconds: z.number().min(0).max(30).optional().describe("Extra time for reverb and release tails"),
      },
    },
    ({ startBar, endBar, tailSeconds }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("render", {
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
          ...(tailSeconds !== undefined ? { tailSeconds } : {}),
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
      description: "Plays the song through the user's speakers, stops it, or moves the playhead, so the user can listen. loop repeats bars until stopped. Rendering doesn't need this.",
      inputSchema: {
        action: z.enum(["play", "stop", "state"]),
        fromBar: z.number().min(1).optional().describe("For play: start here (1-based)"),
        loopBars: z.tuple([z.number().min(1), z.number().min(1)]).optional().describe("For play: [first bar, bar to stop before] to loop"),
      },
    },
    ({ action, fromBar, loopBars }) =>
      attempt(async () => {
        await engine.connect();
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
Opening or creating one switches the user's window too, so only do it when the user asks.`,
      inputSchema: {
        action: z.enum(["current", "open", "new"]),
        path: z.string().optional().describe("For open and new: the project folder (absolute)"),
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
- start: records the given inputs (or "all": Main, Track 1-16 and the Delay, Reverb and Chorus returns; per-track outputs are dry) from fromBar for bars bars, each into its own insert and playlist track (created when missing, named after the input). Silent takes are skipped, and each take's clip gain is set so it plays at about -20 LUFS (hardware outputs arrive quiet). Returns at once; the takes become clips when it stops.
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
            for (const name of names) {
              const id = slug(name);
              if (!doc.mixer.inserts.some((i) => i.id === id)) commands.push({ type: "addInsert", insert: { id, name } });
              if (!doc.playlist.tracks.some((t) => t.id === id)) commands.push({ type: "addTrack", track: { id, name } });
            }
            if (commands.length) await engine.call("execute", { commands, group: groups.next(), label: "Add recording tracks" });
            return engine.call("record.start", {
              targets: names.map((name) => ({ input: name, insert: slug(name), track: slug(name) })),
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
      description: "Renders the mix plus one stem per channel (and per insert with audio clips) and reports: the mix's loudness, true peak, loudness range, spectral balance and stereo image; each source's loudness and share of every frequency band; and masking, i.e. pairs of sources that both carry a big share of the same band (e.g. kick and bass at 60-120 Hz). Slower than render: one render per source.",
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
      description: "Searches the user's sample library. Every word in query must match the file name, its folders or its category. Results give the path, a description of the sound, category, loop, tempo, key, duration and rootKey (detected pitch) where known.",
      inputSchema: {
        query: z.string().optional().describe("Words, e.g. \"dark kick\" or \"vinyl hat\""),
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
      description: "Full measurements of one audio file: duration, loudness, attack and decay, spectral centroid and band energy (sub, low, mid, high), detected pitch, onset count and tempo, plus its classification. Works for files outside the library too.",
      inputSchema: { path: z.string() },
      annotations: { readOnlyHint: true },
    },
    ({ path }) => attempt(() => engine.call("library.describe", { path }))
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
      description: "Built-in effects (eq, compressor, reverb, delay, phaser, chorus, lowpass, highpass) with every parameter's id, unit, range, default and whether it can be automated.",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("listEffects"))
  );
}
