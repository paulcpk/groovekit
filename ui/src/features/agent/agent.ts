// The in-app agent: Claude with GrooveKit's tools, called from this web view with the user's own
// API key. The conversation (what's shown, and the transcript sent to the API) is kept per project.

import Anthropic from "@anthropic-ai/sdk";
import { create } from "zustand";
import { engine as uiEngine, useEngine } from "@/state/engine";
import { useUi } from "@/state/ui";
import { createTools, instructions, type AgentTool } from "./tools";

const MODEL = "claude-opus-5-5";
const KEY_STORAGE = "groovekit.anthropicApiKey";

const SYSTEM = `${instructions}

You're working inside GrooveKit's own agent panel. The user watches the song change as you work: your edits are outlined in violet, listed in the Agent tab, and one request undoes with a single ⌘Z. Keep replies short and concrete: what you changed, and what to listen for. Ask when a request is ambiguous in a way that matters musically; otherwise make a good choice and say what it was. Use transport play to let the user hear a result when it helps, and render when you need numbers.`;

export type ChatItem =
  | { kind: "user"; text: string }
  | { kind: "assistant"; text: string }
  | { kind: "note"; text: string }
  | { kind: "tool"; id: string; name: string; input: unknown; status: "running" | "done" | "error"; result?: string }
  | { kind: "error"; text: string };

interface AgentState {
  items: ChatItem[];
  running: boolean;
  /** The transcript as the API sees it; appended to only (thinking blocks must stay as they were). */
  history: Anthropic.Beta.BetaMessageParam[];
  project?: string;
}

export const useAgent = create<AgentState>(() => ({ items: [], running: false, history: [] }));
const set = useAgent.setState;

//==============================================================================
export function getApiKey() {
  try {
    return localStorage.getItem(KEY_STORAGE) ?? "";
  } catch {
    return "";
  }
}

export function setApiKey(key: string) {
  try {
    if (key) localStorage.setItem(KEY_STORAGE, key.trim());
    else localStorage.removeItem(KEY_STORAGE);
  } catch {
    // Without storage the key lasts until the app closes.
  }
  useApiKey.setState({ key: key.trim() });
}

export const useApiKey = create<{ key: string }>(() => ({ key: getApiKey() }));

//==============================================================================
// One conversation per project, kept in this web view's storage.
const storageKey = (project: string) => `groovekit.agent.${project}`;

function save() {
  const { project, items, history } = useAgent.getState();
  if (!project) return;
  try {
    localStorage.setItem(storageKey(project), JSON.stringify({ items, history }));
  } catch {
    // Too large or unavailable: the conversation still lasts this session.
  }
}

/** Shows the conversation of the open project. */
export function loadConversation(project: string | undefined) {
  if (!project || project === useAgent.getState().project) return;
  stop();
  let saved: Pick<AgentState, "items" | "history"> = { items: [], history: [] };
  try {
    const raw = localStorage.getItem(storageKey(project));
    if (raw) saved = JSON.parse(raw);
  } catch {
    // Start over.
  }
  set({ project, items: saved.items, history: saved.history, running: false });
}

export function clearConversation() {
  stop();
  set({ items: [], history: [] });
  save();
}

//==============================================================================
let tools: { url: string; list: AgentTool[]; disconnect: () => void } | undefined;
let abort: AbortController | undefined;

function toolsFor(url: string) {
  if (tools?.url !== url) {
    tools?.disconnect();
    const created = createTools(url);
    tools = { url, list: created.tools, disconnect: () => created.engine.close() };
  }
  return tools.list;
}

function push(item: ChatItem) {
  set((s) => ({ items: [...s.items, item] }));
}

function updateLast<K extends ChatItem["kind"]>(kind: K, change: (item: Extract<ChatItem, { kind: K }>) => ChatItem) {
  set((s) => {
    const items = [...s.items];
    const last = items[items.length - 1];
    if (last?.kind === kind) items[items.length - 1] = change(last as Extract<ChatItem, { kind: K }>);
    return { items };
  });
}

function updateTool(id: string, change: Partial<Extract<ChatItem, { kind: "tool" }>>) {
  set((s) => ({ items: s.items.map((i) => (i.kind === "tool" && i.id === id ? { ...i, ...change } : i)) }));
}

/** What the user is looking at, so "this pattern" and "the kick" mean something. */
function appState() {
  const { doc, transport } = useEngine.getState();
  const { selectedPattern, selectedChannel, selectedClips } = useUi.getState();
  const name = (list: { id: string; name?: string }[] | undefined, id?: string) => {
    const found = list?.find((x) => x.id === id);
    return found ? `${found.name || found.id} (${found.id})` : "none";
  };
  return [
    `selected pattern: ${name(doc?.patterns, selectedPattern)}`,
    `selected channel: ${name(doc?.channels, selectedChannel)}`,
    selectedClips.length ? `selected clips: ${selectedClips.join(", ")}` : "",
    `transport: ${transport.playing ? "playing" : "stopped"} in ${transport.mode ?? "song"} mode`,
  ].filter(Boolean).join("; ");
}

export function stop() {
  abort?.abort();
  abort = undefined;
}

/** Picks up after an error: the agent carries on from where the conversation stopped. */
export function retry() {
  const last = useAgent.getState().history.at(-1);
  return last?.role === "user" ? send("", true) : send("Carry on where you stopped.");
}

/** Sends a message (or, resuming, nothing new) and lets the agent work until it's done or stopped. */
export async function send(text: string, resume = false) {
  const apiKey = useApiKey.getState().key;
  if (!apiKey || useAgent.getState().running || (!resume && !text.trim())) return;

  const url = uiEngine.url;
  const list = toolsFor(url);
  const client = new Anthropic({ apiKey, dangerouslyAllowBrowser: true });
  const controller = new AbortController();
  abort = controller;

  if (resume) {
    set({ running: true });
  } else {
    push({ kind: "user", text });
    set((s) => ({
      running: true,
      history: [...s.history, { role: "user", content: `${text}\n\n[GrooveKit: ${appState()}]` }],
    }));
  }

  let jsonRetries = 0;

  try {
    while (!controller.signal.aborted) {
      const stream = client.beta.messages.stream(
        {
          model: MODEL,
          max_tokens: 64000,
          betas: ["server-side-fallback-2026-07-01", "thinking-display-updates-2026-08-18"],
          // Declined requests are retried on another model inside the same call.
          fallbacks: "default",
          // Progress notes between tool calls, shown as they come.
          thinking: { type: "adaptive", display: "updates" },
          output_config: { effort: "medium" },
          cache_control: { type: "ephemeral" },
          system: SYSTEM,
          tools: list.map((t) => t.definition),
          messages: useAgent.getState().history,
        },
        { signal: controller.signal },
      );

      let writing = false;
      stream.on("text", (delta) => {
        if (!writing) {
          push({ kind: "assistant", text: "" });
          writing = true;
        }
        updateLast("assistant", (i) => ({ ...i, text: i.text + delta }));
      });
      stream.on("thinking", (delta) => {
        if (!delta) return;
        writing = false;
        const last = useAgent.getState().items.at(-1);
        if (last?.kind === "note") updateLast("note", (i) => ({ ...i, text: i.text + delta }));
        else push({ kind: "note", text: delta });
      });

      let message: Anthropic.Beta.BetaMessage;
      try {
        message = await stream.finalMessage();
        jsonRetries = 0;
      } catch (err) {
        // A tool input that couldn't be parsed at all: ask again (a few times).
        if (err instanceof Anthropic.APIError || controller.signal.aborted || jsonRetries++ >= 2) throw err;
        continue;
      }

      set((s) => ({ history: [...s.history, { role: "assistant", content: message.content }] }));

      if (message.stop_reason === "refusal") {
        push({ kind: "error", text: "Claude declined this request." });
        break;
      }
      if (message.stop_reason === "pause_turn") continue;

      const uses = message.content.filter((b): b is Anthropic.Beta.BetaToolUseBlock => b.type === "tool_use");
      if (uses.length === 0) break;

      if (message.stop_reason === "max_tokens") {
        push({ kind: "error", text: "The reply was cut off before its tool calls were complete." });
        break;
      }

      // Tools run one after another: edits depend on what came before.
      const results: Anthropic.Beta.BetaToolResultBlockParam[] = [];
      for (const use of uses) {
        const tool = list.find((t) => t.definition.name === use.name);
        push({ kind: "tool", id: use.id, name: use.name, input: use.input, status: "running" });
        const parsed = tool?.schema.safeParse(use.input);

        if (!tool || !parsed?.success) {
          const error = tool ? `Invalid input: ${parsed?.error?.message}` : `No tool "${use.name}"`;
          updateTool(use.id, { status: "error", result: error });
          results.push({ type: "tool_result", tool_use_id: use.id, is_error: true, content: error });
          continue;
        }

        const { text: output, isError } = await tool.run(parsed.data as Record<string, unknown>);
        updateTool(use.id, { status: isError ? "error" : "done", result: output });
        results.push({ type: "tool_result", tool_use_id: use.id, ...(isError ? { is_error: true } : {}), content: output });
      }

      set((s) => ({ history: [...s.history, { role: "user", content: results }] }));
    }
  } catch (err) {
    if (!controller.signal.aborted) push({ kind: "error", text: describeError(err) });
  } finally {
    if (abort === controller) abort = undefined;
    // A stopped turn can leave tool calls without results; the next request needs them answered.
    closeOpenToolCalls();
    set({ running: false });
    save();
  }
}

/** Answers tool calls a stop left unanswered, so the transcript stays valid. */
function closeOpenToolCalls() {
  const history = useAgent.getState().history;
  const last = history.at(-1);
  if (last?.role !== "assistant" || typeof last.content === "string") return;
  const open = last.content.filter((b): b is Anthropic.Beta.BetaToolUseBlock => b.type === "tool_use");
  if (!open.length) return;
  set({
    history: [...history, { role: "user", content: open.map((b) => ({ type: "tool_result" as const, tool_use_id: b.id, is_error: true, content: "Stopped by the user." })) }],
    items: useAgent.getState().items.map((i) => (i.kind === "tool" && i.status === "running" ? { ...i, status: "error", result: "Stopped" } : i)),
  });
}

function describeError(err: unknown) {
  if (err instanceof Anthropic.AuthenticationError) return "The API key was rejected. Check it in Settings → Agent.";
  if (err instanceof Anthropic.PermissionDeniedError) return "This API key can't use the model. Check your Anthropic account.";
  if (err instanceof Anthropic.RateLimitError) return "Rate limited by the API. Wait a moment and try again.";
  if (err instanceof Anthropic.APIConnectionError) return "Couldn't reach the Anthropic API. Check your connection.";
  if (err instanceof Anthropic.APIError) return `API error ${err.status ?? ""}: ${err.message}`;
  return err instanceof Error ? err.message : String(err);
}
