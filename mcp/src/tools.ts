// The GrooveKit MCP tools: a few coarse tools over the engine API.

import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";
import { EngineClient, ticksPerBar } from "./engine.ts";
import { commandReference, typeDefinitions } from "./reference.ts";

export const instructions = `GrooveKit is a DAW with FL Studio semantics that you operate through these tools. The human directs; you arrange, mix and master.

Model: channels (instruments: the built-in sampler with one or more sample layers) feed mixer inserts; patterns hold notes for any channels; the playlist has free tracks holding pattern, audio and automation clips. Inserts chain effects, sends and a fader, and feed other inserts or master.

Units: time is integer ticks at 960 per quarter note (in 4/4 a 16th is 240, a beat 960, a bar 3840). Volumes in dB, pan -1..1, velocity 1-127, keys are MIDI numbers (60 = C4). Sample paths are absolute or relative to the project folder.

Sounds: search_samples finds samples in the user's library by words, category, loop, tempo and key, with a description of each sound. Use one-shots as sampler layers; set rootKey from the result's rootKey so pitched samples play in tune, and pitchTracking false for drums. Use loops as audio clips (their tempo must match the song's for now). describe_sample gives full measurements (spectrum bands, attack, pitch, loudness). Paths in results are absolute.

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
      description: "Renders the song (or bars of it) offline to a 48 kHz WAV and returns its path with peak, RMS, integrated LUFS and onset times. Use this to check levels and timing after changes.",
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
    "analyze_audio",
    {
      description: "Peak, RMS, integrated LUFS and onsets of an audio file, optionally of a time range (seconds).",
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
    "list_effects",
    {
      description: "Built-in effects (eq, compressor, reverb, delay, phaser, chorus, lowpass, highpass) with every parameter's id, unit, range, default and whether it can be automated.",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("listEffects"))
  );
}
