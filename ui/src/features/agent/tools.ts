// The in-app agent's tools: the same ones Claude Code gets from the MCP server (mcp/src/tools.ts),
// run here against the engine on a connection of their own (origin "agent"), so the agent's
// changes show up and undo like Claude Code's.

import type Anthropic from "@anthropic-ai/sdk";
import { EngineClient } from "@groovekit/mcp/src/engine.ts";
import { makeReference } from "@groovekit/mcp/src/reference.ts";
import { instructions, registerTools } from "@groovekit/mcp/src/tools.ts";
import protocolSchema from "@groovekit/schema/protocol.schema.json";
import projectTypes from "@groovekit/schema/project?raw";
import protocolTypes from "@groovekit/schema/protocol?raw";
import { z } from "zod";

export { instructions };

interface ToolResult {
  content: { type: string; text?: string }[];
  isError?: boolean;
}

export interface AgentTool {
  definition: Anthropic.Beta.BetaTool;
  schema: z.ZodObject;
  run: (input: Record<string, unknown>) => Promise<{ text: string; isError: boolean }>;
}

const reference = makeReference({ protocolSchema, projectTypes, protocolTypes });

/** The tools, connected to the engine at url. */
export function createTools(url: string): { tools: AgentTool[]; engine: EngineClient } {
  const engine = new EngineClient(url, "agent");
  const tools: AgentTool[] = [];

  // Stands in for the MCP server: registerTools only calls registerTool.
  const collector = {
    registerTool(
      name: string,
      config: { description?: string; inputSchema?: z.ZodRawShape },
      handler: (...args: unknown[]) => Promise<ToolResult> | ToolResult,
    ) {
      const schema = z.object(config.inputSchema ?? {});
      const { $schema: _, ...inputSchema } = z.toJSONSchema(schema) as Record<string, unknown>;

      tools.push({
        definition: {
          name,
          description: config.description ?? "",
          input_schema: inputSchema as Anthropic.Beta.BetaTool.InputSchema,
          // Large inputs (edit_project commands) stream as they're written; validated before running.
          eager_input_streaming: true,
        },
        schema,
        run: async (input) => {
          // Tools without inputs are called as the MCP server would: with no arguments.
          const result = await (config.inputSchema ? handler(input, {}) : handler({}));
          const text = result.content.map((c) => c.text ?? "").join("\n");
          return { text, isError: !!result.isError };
        },
      });
    },
  };

  registerTools(collector as unknown as Parameters<typeof registerTools>[0], engine, reference);
  return { tools, engine };
}
