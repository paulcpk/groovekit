// The in-app agent: Claude with GrooveKit's tools, called from this web view with the user's own
// API key. Conversations (what's shown, and the transcript sent to the API) are kept by the engine
// in the app data folder; opening a project brings back its latest one, and any can be reopened.

import Anthropic from "@anthropic-ai/sdk";
import { create } from "zustand";
import { engine as uiEngine, useEngine } from "@/state/engine";
import { useUi } from "@/state/ui";
import { guideForAgent } from "./guides";
import { notesForAgent } from "./notes";
import { createTools, instructions, type AgentTool } from "./tools";

const MODEL = "claude-opus-5-5";
const KEY_STORAGE = "groovekit.anthropicApiKey";
const CHOICES_STORAGE = "groovekit.agentChoices";

const SYSTEM = `${instructions}

You're working inside GrooveKit's own agent panel, where the song's style guide and the user's notes that apply here are part of these instructions (below, if there are any), so there's no need to read or list them. The user watches the song change as you work: your edits are outlined in violet, listed in the Agent tab, and one request undoes with a single ⌘Z. After an edit, keep the reply short and concrete: what you changed, and what to listen for. In a discussion, take the room you need, but stay concrete and about this song. Ask when a request is ambiguous in a way that matters musically; otherwise make a good choice and say what it was. Use transport play to let the user hear a result when it helps, and render when you need numbers.`;

export type ChatItem =
  | { kind: "user"; text: string }
  | { kind: "assistant"; text: string }
  | { kind: "note"; text: string }
  | { kind: "tool"; id: string; name: string; input: unknown; status: "running" | "done" | "error"; result?: string }
  | { kind: "error"; text: string };

interface AgentState {
  /** The conversation shown: it's saved under this id once it has a message. */
  chatId: string;
  title: string;
  created?: number;
  items: ChatItem[];
  running: boolean;
  /** The transcript as the API sees it; appended to only (thinking blocks must stay as they were). */
  history: Anthropic.Beta.BetaMessageParam[];
  /** The open project, where the conversation is saved as last used. */
  project?: string;
  /** A conversation reopened from another song: the next message says so. */
  movedFrom?: string;
}

const newId = () => crypto.randomUUID();

export const useAgent = create<AgentState>(() => ({ chatId: newId(), title: "", items: [], running: false, history: [] }));
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

/** How many sounds the agent puts to the user when they pick by ear: 5 unless set (2 to 10). */
export const DEFAULT_CHOICES = 5;

function loadChoices() {
  try {
    const n = Number(localStorage.getItem(CHOICES_STORAGE));
    return n >= 2 && n <= 20 ? n : DEFAULT_CHOICES;
  } catch {
    return DEFAULT_CHOICES;
  }
}

export const useAgentChoices = create<{ choices: number }>(() => ({ choices: loadChoices() }));

export function setAgentChoices(choices: number) {
  try {
    localStorage.setItem(CHOICES_STORAGE, String(choices));
  } catch {
    // Kept until the app closes.
  }
  useAgentChoices.setState({ choices });
}

//==============================================================================
// Conversations, kept by the engine.

export interface ChatSummary {
  id: string;
  title: string;
  project: string;
  projectName: string;
  created: number;
  updated: number;
  /** How many messages the user sent. */
  messages: number;
}

export const useChats = create<{ chats: ChatSummary[] }>(() => ({ chats: [] }));

export async function refreshChats() {
  const r = await uiEngine.call<{ chats: ChatSummary[] }>("chats.list");
  useChats.setState({ chats: r.chats });
}

const projectName = (project: string) => useEngine.getState().doc?.name || project.split("/").filter(Boolean).at(-1) || project;

async function save() {
  const { chatId, title, created, project, items, history } = useAgent.getState();
  if (!project || items.length === 0) return;
  try {
    const saved = await uiEngine.call<ChatSummary>("chats.save", {
      chat: { id: chatId, title, project, projectName: projectName(project), ...(created ? { created } : {}), items, history },
    });
    if (useAgent.getState().chatId === chatId) set({ created: saved.created });
    void refreshChats();
  } catch {
    // The engine is away: the conversation is saved after the next message.
  }
}

/** Conversations from before the engine kept them (in this web view's storage), moved over once. */
async function migrateStoredChats() {
  const prefix = "groovekit.agent.";
  let keys: string[] = [];
  try {
    keys = Object.keys(localStorage).filter((k) => k.startsWith(prefix));
  } catch {
    return;
  }
  for (const key of keys) {
    try {
      const saved = JSON.parse(localStorage.getItem(key) ?? "{}") as Partial<Pick<AgentState, "items" | "history">>;
      const items = saved.items ?? [];
      if (items.length) {
        const project = key.slice(prefix.length);
        const first = items.find((i) => i.kind === "user");
        await uiEngine.call("chats.save", {
          chat: { id: newId(), title: titleOf(first?.text ?? ""), project, projectName: project.split("/").filter(Boolean).at(-1) ?? project, items, history: saved.history ?? [] },
        });
      }
      localStorage.removeItem(key);
    } catch {
      // Left for the next time.
    }
  }
}

const titleOf = (text: string) => {
  const line = text.trim().split("\n")[0];
  return line.length > 60 ? `${line.slice(0, 57).trimEnd()}…` : line;
};

let loading = 0;

/** Shows the open project's latest conversation, or a new one. */
export async function loadConversation(project: string | undefined) {
  if (!project || project === useAgent.getState().project) return;
  stop();
  const token = ++loading;
  set({ project, chatId: newId(), title: "", created: undefined, items: [], history: [], running: false, movedFrom: undefined });

  try {
    await migrateStoredChats();
    await refreshChats();
    const latest = useChats.getState().chats.find((c) => c.project === project);
    if (latest && token === loading) await openChat(latest.id);
  } catch {
    // Without the engine there's nothing to bring back.
  }
}

/** Reopens a conversation, here: it carries on in the open project. */
export async function openChat(id: string) {
  if (useAgent.getState().running) return;
  const chat = await uiEngine.call<{ id: string; title?: string; project?: string; projectName?: string; created?: number; items?: ChatItem[]; history?: Anthropic.Beta.BetaMessageParam[] }>("chats.get", { id });
  const here = useAgent.getState().project;
  set({
    chatId: chat.id,
    title: chat.title ?? "",
    created: chat.created,
    items: chat.items ?? [],
    history: chat.history ?? [],
    running: false,
    movedFrom: chat.project && chat.project !== here ? chat.projectName || chat.project : undefined,
  });
}

/** Starts a new conversation; the current one stays in the list. */
export function newChat() {
  stop();
  set({ chatId: newId(), title: "", created: undefined, items: [], history: [], running: false, movedFrom: undefined });
}

export async function deleteChat(id: string) {
  await uiEngine.call("chats.delete", { id });
  if (useAgent.getState().chatId === id) newChat();
  await refreshChats();
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
    `song: ${doc?.name || "untitled"}`,
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
    // A conversation carried on in another song: its ids and names may not exist here.
    const moved = useAgent.getState().movedFrom;
    const context = moved ? `this conversation began in the song "${moved}"; the open song is another one, so look at it before editing. ` : "";
    push({ kind: "user", text });
    set((s) => ({
      running: true,
      movedFrom: undefined,
      title: s.title || titleOf(text),
      history: [...s.history, { role: "user", content: `${text}\n\n[GrooveKit: ${context}${appState()}]` }],
    }));
    void save();
  }

  let jsonRetries = 0;

  try {
    const [notes, guide] = await Promise.all([notesForAgent(), guideForAgent()]);
    const choices = `When the user picks sounds by ear (offer_choice), offer ${useAgentChoices.getState().choices} candidates, as they've set in GrooveKit's settings; fewer only when there aren't that many good ones. When they ask for another round, offer that many more, all new.`;
    const system = [SYSTEM, choices, guide, notes].filter(Boolean).join("\n\n");

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
          system,
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
    void save();
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
