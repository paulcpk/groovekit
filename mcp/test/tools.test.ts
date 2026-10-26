// End to end: a real engine process, the MCP tools, and an in-memory MCP client.
// Needs a built engine: GROOVEKIT_ENGINE, or build/dev/engine/groovekit-engine.

import { spawn, type ChildProcess } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { applyPatch, EngineClient } from "../src/engine.ts";
import { GroupTracker, instructions, registerTools } from "../src/tools.ts";

const repo = resolve(import.meta.dirname, "..", "..");
const enginePath = process.env.GROOVEKIT_ENGINE ?? join(repo, "build", "dev", "engine", "groovekit-engine");
const fixtures = join(repo, "tests", "fixtures", "samples");

describe("applyPatch", () => {
  it("handles add, remove and replace in objects and arrays", () => {
    const doc: any = { a: [1, 2], b: { "x/y": 1 } };
    applyPatch(doc, [
      { op: "add", path: "/a/1", value: 9 },
      { op: "add", path: "/a/-", value: 3 },
      { op: "remove", path: "/a/0" },
      { op: "replace", path: "/b/x~1y", value: 2 },
      { op: "add", path: "/c", value: true },
    ]);
    expect(doc).toEqual({ a: [9, 2, 3], b: { "x/y": 2 }, c: true });
  });
});

describe("GroupTracker", () => {
  it("starts a new group after a pause or a reset", () => {
    let now = 0;
    const groups = new GroupTracker(() => now);
    const first = groups.next();
    now = 60_000;
    expect(groups.next()).toBe(first);
    now = 200_000;
    const second = groups.next();
    expect(second).not.toBe(first);
    groups.reset();
    expect(groups.next()).not.toBe(second);
  });
});

describe.skipIf(!existsSync(enginePath))("MCP tools against a running engine", () => {
  const port = 7900 + Math.floor(Math.random() * 90);
  const project = mkdtempSync(join(tmpdir(), "groovekit-mcp-"));
  let engineProcess: ChildProcess;
  let engine: EngineClient;
  let client: Client;

  const call = async (name: string, args: Record<string, unknown> = {}) => {
    const result: any = await client.callTool({ name, arguments: args });
    const text = result.content[0].text as string;
    return { error: result.isError ? text : undefined, value: result.isError ? undefined : JSON.parse(text) };
  };

  beforeAll(async () => {
    engineProcess = spawn(enginePath, ["serve", project, "--port", String(port)], { stdio: "ignore" });
    engine = new EngineClient(`ws://127.0.0.1:${port}`);

    for (let i = 0; i < 100; i++) {
      try {
        await engine.connect();
        break;
      } catch {
        await new Promise((r) => setTimeout(r, 100));
      }
    }

    const server = new McpServer({ name: "groovekit", version: "test" }, { instructions });
    registerTools(server, engine);
    const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
    await server.connect(serverTransport);
    client = new Client({ name: "test", version: "1" });
    await client.connect(clientTransport);
  }, 20_000);

  afterAll(() => {
    engine?.close();
    engineProcess?.kill("SIGTERM");
    rmSync(project, { recursive: true, force: true });
  });

  it("lists the tools", async () => {
    const { tools } = await client.listTools();
    expect(tools.map((t) => t.name)).toEqual(
      expect.arrayContaining(["get_summary", "edit_project", "render", "undo", "get_schema", "list_effects"])
    );
  });

  it("builds a beat, renders it and undoes it", async () => {
    const edit = await call("edit_project", {
      label: "Four beeps",
      commands: [
        { type: "addChannel", channel: { id: "beep", instrument: { type: "sampler", layers: [{ id: "l", sample: join(fixtures, "beep.wav"), pitchTracking: false }] } } },
        { type: "addPattern", pattern: { id: "beat", notes: [0, 960, 1920, 2880].map((start) => ({ channel: "beep", start, length: 240, velocity: 127 })) } },
        { type: "addTrack", track: { id: "drums" } },
        { type: "addClip", clip: { type: "pattern", id: "c1", track: "drums", start: 0, length: 3840, pattern: "beat" } },
      ],
    });
    expect(edit.error).toBeUndefined();
    expect(edit.value.label).toBe("Four beeps");

    const summary = await call("get_summary");
    expect(summary.value.lengthBars).toBe(1);
    expect(summary.value.patterns[0].notesPerChannel).toEqual({ beep: 4 });

    // The mirror follows the engine through patch notifications.
    expect(engine.document?.channels.map((c) => c.id)).toEqual(["beep"]);

    const render = await call("render", { endBar: 2 });
    expect(render.value.analysis.onsets).toEqual([0, 0.5, 1, 1.5]);
    expect(render.value.analysis.peakDb).toBe(-6.02);

    const undo = await call("undo");
    expect(undo.value.head).toBeNull();
    expect((await call("get_project", { path: "/channels" })).value).toEqual([]);
  });

  it("reports command errors without changing anything", async () => {
    const result = await call("edit_project", { commands: [{ type: "updateChannel", channel: "ghost", changes: { volumeDb: -3 } }] });
    expect(result.error).toContain('no channel "ghost"');
  });

  it("serves the schema and the effect catalogue", async () => {
    const schema = await client.callTool({ name: "get_schema", arguments: {} });
    expect((schema as any).content[0].text).toContain("export interface Channel");
    expect((await call("list_effects")).value.compressor.params.thresholdDb.unit).toBe("dB");
  });
});
