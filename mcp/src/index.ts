#!/usr/bin/env node
// GrooveKit MCP server (stdio).
//
//   groovekit-mcp [--url ws://127.0.0.1:7878]
//   groovekit-mcp --engine <path to groovekit-engine> --project <folder> [--port 7878]
//
// With --engine and --project it starts the engine itself and stops it on exit;
// otherwise it connects to an engine that is already serving.

import { spawn, type ChildProcess } from "node:child_process";
import { parseArgs } from "node:util";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { EngineClient } from "./engine.ts";
import { instructions, registerTools } from "./tools.ts";

const { values } = parseArgs({
  options: {
    url: { type: "string" },
    engine: { type: "string" },
    project: { type: "string" },
    port: { type: "string", default: "7878" },
  },
});

let engineProcess: ChildProcess | undefined;

if (values.engine || values.project) {
  if (!values.engine || !values.project) throw new Error("--engine and --project go together");

  // stdout belongs to MCP, so the engine's output goes to stderr.
  engineProcess = spawn(values.engine, ["serve", values.project, "--port", values.port!], { stdio: ["ignore", "pipe", "inherit"] });
  engineProcess.stdout?.pipe(process.stderr);
  engineProcess.on("exit", (code) => console.error(`groovekit-engine exited (${code})`));
}

const url = values.url ?? `ws://127.0.0.1:${values.port}`;
const engine = new EngineClient(url, "agent");

if (engineProcess) {
  // Give the engine a moment to start listening.
  for (let i = 0; i < 50; i++) {
    try {
      await engine.connect();
      break;
    } catch {
      await new Promise((r) => setTimeout(r, 100));
    }
  }
}

const server = new McpServer({ name: "groovekit", version: "0.1.0" }, { instructions });
registerTools(server, engine);
await server.connect(new StdioServerTransport());

const shutdown = () => {
  engine.close();
  engineProcess?.kill("SIGTERM");
  process.exit(0);
};

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
process.stdin.on("close", shutdown);
