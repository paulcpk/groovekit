// The song: reading it, changing it, its history and its project folder.

import { z } from "zod";
import { attempt, bounded, defined, PAGE, text, type Tools } from "./shared.ts";

export function registerSongTools({ server, engine, reference, groups }: Tools) {
  const { commandReference, typeDefinitions } = reference;

  server.registerTool(
    "get_summary",
    {
      description: "Compact overview of the song: tempo, key, style, channels and their samples, patterns with note counts, the playlist with clip positions in bars (1-based), and the mixer.",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("getSummary"))
  );

  server.registerTool(
    "get_project",
    {
      description: `The exact song.json, or the part at a JSON Pointer (e.g. /patterns/0/notes, /mixer/inserts/2). Use it to see notes and full settings before editing. Arrays longer than ${PAGE} are cut, with how many follow: read on with offset.`,
      inputSchema: {
        path: z.string().optional().describe("JSON Pointer; omit for the whole document"),
        offset: z.number().int().min(0).optional().describe("When the value is an array: start here"),
        limit: z.number().int().min(1).max(2000).optional().describe(`When the value is an array: at most this many (default ${PAGE})`),
      },
      annotations: { readOnlyHint: true },
    },
    ({ path, offset, limit }) => attempt(async () => bounded(await engine.call("get", { path: path ?? "" }), path ?? "", offset ?? 0, limit ?? PAGE))
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
      attempt(async () => {
        const group = groups.next();
        const result = await engine.call<Record<string, unknown>>("execute", defined({ commands, group, label }));

        // New channels get their part's frequency slot straight away, in the same undo step.
        const added = commands.flatMap((c) => {
          const channel = (c as { channel?: { id?: unknown } }).channel;
          return c.type === "addChannel" && typeof channel?.id === "string" ? [channel.id] : [];
        });
        if (added.length === 0) return result;

        try {
          const slots = await engine.call<{ channels: unknown[] }>("mix.slots", { channels: added, apply: true, onlyNew: true, group });
          return { ...result, slots: slots.channels, slotsNote: "Each new channel got its own insert with a slot EQ for its part, first on the insert. If a part is wrong, mix_slots set it." };
        } catch {
          return result;
        }
      })
  );

  const steps = z.number().int().min(1).max(50).optional().describe("Groups; default 1");

  server.registerTool(
    "undo",
    {
      description: "Undoes the most recent group of changes (all edits made close together count as one group). Repeat to go further back.",
      inputSchema: { steps },
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
      inputSchema: { steps },
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
      description: "The change history, newest first: who changed what and when, and the current position (head).",
      inputSchema: { limit: z.number().int().min(1).max(1000).optional().describe("Entries (default 30)") },
      annotations: { readOnlyHint: true },
    },
    ({ limit }) =>
      attempt(async () => {
        const { entries, head } = await engine.call<{ entries: unknown[]; head: unknown }>("getHistory");
        const n = limit ?? 30;
        return { head, total: entries.length, entries: entries.slice(-n).reverse() };
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
        if (action === "current") return engine.call("project.recent");
        if (!path) throw new Error(`${action} needs a path`);
        return engine.call(`project.${action}`, { path });
      })
  );
}
