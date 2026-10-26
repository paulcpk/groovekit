#!/usr/bin/env node
// GrooveKit MCP server (stdio).
//
//   groovekit-mcp [--project <folder>] [--engine <path>] [--port 7878]
//   groovekit-mcp --url ws://127.0.0.1:7878
//
// By default it works on the project folder from --project, GROOVEKIT_PROJECT or
// ~/Music/GrooveKit/Scratch. If an engine is already serving that project (the GrooveKit app,
// found through the engines' registrations in the app data folder), it connects to it, so the app shows the
// agent's changes live. Otherwise it starts the engine itself (the repository's debug build)
// and stops it on exit. With --url it connects to that engine instead.

import { spawn, type ChildProcess } from "node:child_process";
import { readFileSync, readdirSync, realpathSync } from "node:fs";
import { createServer } from "node:net";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import { parseArgs } from "node:util";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { EngineClient } from "./engine.ts";
import { fileReference } from "./reference-files.ts";
import { instructions, registerTools } from "./tools.ts";

const { values } = parseArgs({
  options: {
    url: { type: "string" },
    engine: { type: "string" },
    project: { type: "string" },
    port: { type: "string" },
  },
});

/** A port nothing is listening on, so a stale engine from another session can't answer for ours. */
function freePort(): Promise<number> {
  return new Promise((resolvePort, reject) => {
    const server = createServer();
    server.unref();
    server.on("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const { port } = server.address() as { port: number };
      server.close(() => resolvePort(port));
    });
  });
}

/** The URL of a live engine already serving this project, from the registrations serving engines
    write (one file per engine in the app data folder). */
function runningEngine(project: string): string | undefined {
  const registry = join(homedir(), "Library", "Application Support", "GrooveKit", "engines");
  let files: string[] = [];
  try {
    files = readdirSync(registry);
  } catch {
    return undefined;
  }

  for (const file of files) {
    try {
      const info = JSON.parse(readFileSync(join(registry, file), "utf8"));
      process.kill(info.pid, 0); // throws if it's gone
      if (resolve(realpathSync(info.project)) === resolve(realpathSync(project))) return info.url;
    } catch {
      // Gone, unreadable or another project.
    }
  }
  return undefined;
}

// Resolved here rather than in .mcp.json, so nothing depends on the working directory or shell expansion.
const project = resolve(values.project ?? process.env.GROOVEKIT_PROJECT ?? join(homedir(), "Music", "GrooveKit", "Scratch"));
const sharedUrl = values.url ?? (values.port ? undefined : runningEngine(project));

let engineProcess: ChildProcess | undefined;
const port = values.port ?? String(sharedUrl ? 7878 : await freePort());

if (!sharedUrl) {
  const enginePath = values.engine ?? resolve(import.meta.dirname, "..", "..", "build", "dev", "engine", "groovekit-engine");

  // stdout belongs to MCP, so the engine's output goes to stderr.
  engineProcess = spawn(enginePath, ["serve", project, "--port", port, "--parent-pid", String(process.pid)],
                        { stdio: ["ignore", "pipe", "inherit"] });
  engineProcess.stdout?.pipe(process.stderr);
  engineProcess.on("exit", (code) => console.error(`groovekit-engine exited (${code})`));
}

const url = sharedUrl ?? `ws://127.0.0.1:${port}`;
const engine = new EngineClient(url, "agent");

if (engineProcess) {
  // Give the engine time to start listening (loading a large sample library index takes a few seconds).
  for (let i = 0; i < 300; i++) {
    try {
      await engine.connect();
      break;
    } catch {
      await new Promise((r) => setTimeout(r, 100));
    }
  }
}

const server = new McpServer({ name: "groovekit", version: "0.1.0" }, { instructions });
registerTools(server, engine, fileReference);
await server.connect(new StdioServerTransport());

const shutdown = () => {
  engine.close();
  engineProcess?.kill("SIGTERM");
  process.exit(0);
};

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
process.stdin.on("close", shutdown);
