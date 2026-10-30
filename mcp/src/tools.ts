// The GrooveKit MCP tools: a few coarse tools over the engine API, in modules by what they're for.

import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { EngineClient } from "./engine.ts";
import type { Reference } from "./reference.ts";
import { registerAudioTools } from "./tools/audio.ts";
import { registerLibraryTools } from "./tools/library.ts";
import { registerMixTools } from "./tools/mix.ts";
import { GroupTracker, makeBarRange, type Tools } from "./tools/shared.ts";
import { registerSongTools } from "./tools/song.ts";
import { registerStyleTools } from "./tools/style.ts";

export { instructions } from "./instructions.ts";
export { GroupTracker } from "./tools/shared.ts";

/** The agent's tools, on an MCP server (or anything with the same registerTool, like the app's own
    agent). reference describes the data model and commands (reference-files.ts in Node). */
export function registerTools(server: McpServer, engine: EngineClient, reference: Reference, groups = new GroupTracker()) {
  const tools: Tools = { server, engine, reference, groups, barRange: makeBarRange(engine) };
  registerSongTools(tools);
  registerAudioTools(tools);
  registerMixTools(tools);
  registerLibraryTools(tools);
  registerStyleTools(tools);
}
