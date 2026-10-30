// What every tool module uses: results, errors, the undo-group tracker, bar conversion, progress
// for long calls, and the schemas several tools share.

import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { RequestHandlerExtra } from "@modelcontextprotocol/sdk/shared/protocol.js";
import type { CallToolResult, ServerNotification, ServerRequest } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";
import { EngineClient, EngineError, ticksPerBar } from "../engine.ts";
import type { Reference } from "../reference.ts";

/** What a tool handler gets besides its arguments: the client's cancellation and progress token.
    The app's own agent passes an empty object. */
export type Extra = Partial<RequestHandlerExtra<ServerRequest, ServerNotification>>;

/** What the tool modules register with. */
export interface Tools {
  server: McpServer;
  engine: EngineClient;
  reference: Reference;
  groups: GroupTracker;
  /** Bars (1-based, as the user counts them) to ticks, from the song's time signature. */
  barRange(startBar?: number, endBar?: number): Promise<{ start?: number; end?: number }>;
}

const GROUP_IDLE_MS = 120_000;

/** A result for the model: compact JSON (no indentation: a note costs half the tokens), or text as is. */
export function text(value: unknown): CallToolResult {
  return { content: [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value) }] };
}

export function failure(error: unknown): CallToolResult {
  const message = error instanceof EngineError ? `${error.message} (engine error ${error.code})` : error instanceof Error ? error.message : String(error);
  return { content: [{ type: "text", text: `Error: ${message}` }], isError: true };
}

export async function attempt(fn: () => Promise<unknown>): Promise<CallToolResult> {
  try {
    return text(await fn());
  } catch (e) {
    return failure(e);
  }
}

/** The object without its undefined fields, for engine params that must not carry them. */
export function defined<T extends Record<string, unknown>>(object: T): { [K in keyof T]: Exclude<T[K], undefined> } {
  return Object.fromEntries(Object.entries(object).filter(([, v]) => v !== undefined)) as never;
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

export function makeBarRange(engine: EngineClient): Tools["barRange"] {
  return async (startBar, endBar) => {
    if (startBar === undefined && endBar === undefined) return {};
    await engine.connect();
    const toTicks = (bar: number) => Math.round((bar - 1) * ticksPerBar(engine.document));
    return defined({ start: startBar === undefined ? undefined : toTicks(startBar), end: endBar === undefined ? undefined : toTicks(endBar) });
  };
}

const PROGRESS_EVERY_MS = 5_000;

/**
 * Runs a call that can take minutes (renders, hearings, waiting for the user): while it runs, the
 * client gets a progress notification every few seconds, which keeps its request timeout from
 * firing, and when the client cancels the request the call is given up (signal).
 */
export async function longRunning<T>(extra: Extra | undefined, message: string, fn: (signal: AbortSignal) => Promise<T>): Promise<T> {
  const token = extra?._meta?.progressToken;
  const send = extra?.sendNotification;
  let ticks = 0;
  const timer = token !== undefined && send
    ? setInterval(() => {
        send({ method: "notifications/progress", params: { progressToken: token, progress: ++ticks, message } }).catch(() => {});
      }, PROGRESS_EVERY_MS)
    : undefined;

  try {
    return await fn(extra?.signal ?? new AbortController().signal);
  } finally {
    if (timer) clearInterval(timer);
  }
}

/** The parts a channel can play, as the engine's mix slots know them. */
export const partEnum = z.enum(["kick", "rumble", "drums", "bass", "percussion", "clap", "hats", "synth", "vocal", "fx"]);

/** A note as addNotes takes it: ticks from the start of the pattern, 960 a beat. */
export const noteSchema = z.object({
  start: z.number().int().min(0),
  length: z.number().int().min(1),
  key: z.number().int().min(0).max(127).optional(),
  velocity: z.number().int().min(1).max(127).optional(),
});

/** Default size of an array the model gets in one piece; longer ones are cut with a note on how to read on. */
export const PAGE = 300;

/**
 * Cuts long arrays anywhere in a value (a whole song's patterns, a history), so one read can't
 * flood the context: the first `limit` items stay, then one entry saying how many follow and the
 * pointer to read them with offset. `offset` applies to the value itself when it's an array; `limit` to every array in it.
 */
export function bounded(value: unknown, pointer = "", offset = 0, limit = PAGE): unknown {
  if (Array.isArray(value)) {
    const page = value.slice(offset, offset + limit).map((item, i) => bounded(item, `${pointer}/${offset + i}`, 0, limit));
    const rest = value.length - offset - page.length;
    if (rest > 0) page.push({ truncated: rest, next: `get_project path ${pointer || "/"} offset ${offset + page.length}` });
    return page;
  }
  if (value && typeof value === "object") {
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(value)) out[k] = bounded(v, `${pointer}/${k.replace(/~/g, "~0").replace(/\//g, "~1")}`, 0, limit);
    return out;
  }
  return value;
}
