import { ArrowUp, Check, ChevronRight, KeyRound, Loader2, Sparkles, Square, Trash2, X } from "lucide-react";
import { useEffect, useLayoutEffect, useRef, useState } from "react";
import Markdown, { type Components } from "react-markdown";
import remarkGfm from "remark-gfm";
import { Button, IconButton, Tabs } from "@/components/controls";
import { Activity } from "@/features/history/Activity";
import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { clearConversation, send, setApiKey, stop, useAgent, useApiKey, type ChatItem } from "./agent";

const SUGGESTIONS = [
  "Make a 4-bar techno groove at 128 BPM with kick, clap and hats",
  "Add a bassline that follows the kick",
  "Mix the drums so the kick cuts through",
  "Master the song to −14 LUFS",
];

const TOOL_LABELS: Record<string, string> = {
  get_summary: "Read the song",
  get_project: "Read the song's details",
  get_schema: "Read the song format",
  edit_project: "Edit the song",
  search_samples: "Search samples",
  describe_sample: "Listen to a sample",
  render: "Render and measure",
  analyze_mix: "Analyse the mix",
  analyze_audio: "Analyse audio",
  master: "Master",
  transport: "Transport",
  undo: "Undo",
  redo: "Redo",
  get_history: "Read the history",
  plugins: "Plugins",
  describe_plugin: "Read a plugin",
  record: "Record",
  list_effects: "List effects",
  sample_library: "Sample library",
  project: "Project",
};

/** One line about what a tool call was for. */
function toolSummary(input: unknown) {
  if (!input || typeof input !== "object") return "";
  const i = input as Record<string, unknown>;
  const value = i.label ?? i.query ?? i.action ?? i.path ?? i.category;
  return typeof value === "string" ? value : "";
}

// Replies are Markdown; images show their alt text only, so text that reached the model can't
// make the panel load a URL.
const MARKDOWN: Components = {
  img: ({ alt }) => <>{alt}</>,
  p: ({ children }) => <p className="my-1.5 first:mt-0 last:mb-0">{children}</p>,
  ul: ({ children }) => <ul className="my-1.5 list-disc space-y-0.5 pl-4">{children}</ul>,
  ol: ({ children }) => <ol className="my-1.5 list-decimal space-y-0.5 pl-4">{children}</ol>,
  code: ({ children }) => <code className="rounded-[3px] bg-bg px-1 font-mono text-[11px]">{children}</code>,
  pre: ({ children }) => <pre className="my-2 overflow-x-auto rounded-[4px] bg-bg p-2 text-[11px]">{children}</pre>,
  a: ({ children, href }) => <a href={href} target="_blank" rel="noreferrer" className="text-accent underline">{children}</a>,
  h1: ({ children }) => <p className="mt-2 mb-1 font-semibold">{children}</p>,
  h2: ({ children }) => <p className="mt-2 mb-1 font-semibold">{children}</p>,
  h3: ({ children }) => <p className="mt-2 mb-1 font-semibold">{children}</p>,
  table: ({ children }) => <div className="my-2 overflow-x-auto"><table className="text-[11px]">{children}</table></div>,
  th: ({ children }) => <th className="border-b border-line px-1.5 py-0.5 text-left">{children}</th>,
  td: ({ children }) => <td className="border-b border-line/50 px-1.5 py-0.5">{children}</td>,
};

export function AgentPanel() {
  const [view, setView] = useState<"chat" | "changes">("chat");
  const items = useAgent((s) => s.items);
  const running = useAgent((s) => s.running);

  return (
    <div className="flex h-full flex-col">
      <div className="flex h-8 shrink-0 items-center gap-2 border-b border-line px-2">
        <Tabs<"chat" | "changes"> value={view} onChange={setView} items={[{ value: "chat", label: "Chat" }, { value: "changes", label: "Changes" }]} />
        <div className="flex-1" />
        {view === "chat" && items.length > 0 && (
          <IconButton size="sm" title="New conversation" disabled={running} onClick={clearConversation}><Trash2 size={12} /></IconButton>
        )}
      </div>
      <div className="min-h-0 flex-1">{view === "chat" ? <Chat /> : <Activity />}</div>
    </div>
  );
}

function Chat() {
  const key = useApiKey((s) => s.key);
  const items = useAgent((s) => s.items);
  const running = useAgent((s) => s.running);
  const connected = useEngine((s) => s.status === "connected");
  const [draft, setDraft] = useState("");
  const list = useRef<HTMLDivElement>(null);
  const following = useRef(true);

  // Follows the conversation while it's scrolled to the bottom.
  useLayoutEffect(() => {
    const el = list.current;
    if (el && following.current) el.scrollTop = el.scrollHeight;
  }, [items]);

  if (!key) return <KeySetup />;

  const submit = (text = draft) => {
    if (!text.trim() || running || !connected) return;
    following.current = true;
    setDraft("");
    void send(text.trim());
  };

  const last = items.at(-1);
  const working = running && last?.kind !== "assistant";

  return (
    <div className="flex h-full flex-col">
      <div ref={list} className="min-h-0 flex-1 overflow-y-auto px-3 py-3"
        onScroll={(e) => {
          const el = e.currentTarget;
          following.current = el.scrollHeight - el.scrollTop - el.clientHeight < 60;
        }}>
        {items.length === 0 ? (
          <div className="flex flex-col gap-3 pt-2">
            <div className="flex items-center gap-2 text-sm text-fg"><Sparkles size={14} className="text-agent" /> What should we make?</div>
            <p className="text-xs leading-relaxed text-dim">
              The agent arranges, mixes and masters with the same tools as Claude Code, in this project. Its changes show up live,
              outlined in violet, and one ⌘Z undoes a request.
            </p>
            <div className="flex flex-col gap-1.5">
              {SUGGESTIONS.map((s) => (
                <button key={s} type="button" disabled={!connected} onClick={() => submit(s)}
                  className="rounded-[6px] bg-raised px-2.5 py-2 text-left text-xs text-dim ring-1 ring-line hover:text-fg hover:ring-agent/50">
                  {s}
                </button>
              ))}
            </div>
          </div>
        ) : (
          <div className="flex flex-col gap-2.5">
            {items.map((item, i) => <Item key={i} item={item} />)}
            {working && (
              <div className="flex items-center gap-2 text-xs text-faint"><Loader2 size={12} className="animate-spin text-agent" /> Working…</div>
            )}
          </div>
        )}
      </div>
      <Composer value={draft} onChange={setDraft} onSend={() => submit()} onStop={stop} running={running} disabled={!connected} />
    </div>
  );
}

function Item({ item }: { item: ChatItem }) {
  const [open, setOpen] = useState(false);

  switch (item.kind) {
    case "user":
      return <div className="ml-6 self-end rounded-[10px] rounded-br-[3px] bg-control px-2.5 py-1.5 text-xs whitespace-pre-wrap text-fg">{item.text}</div>;
    case "assistant":
      return <div className="text-xs leading-relaxed text-fg"><Markdown remarkPlugins={[remarkGfm]} components={MARKDOWN}>{item.text}</Markdown></div>;
    case "note":
      return <div className="border-l-2 border-agent/40 pl-2 text-[11px] leading-relaxed text-faint italic">{item.text}</div>;
    case "error":
      return <div className="rounded-[6px] bg-rec/10 px-2.5 py-1.5 text-xs text-rec ring-1 ring-rec/30">{item.text}</div>;
    case "tool": {
      const summary = toolSummary(item.input);
      return (
        <div className="rounded-[6px] bg-raised ring-1 ring-line">
          <button type="button" onClick={() => setOpen(!open)} className="flex w-full items-center gap-1.5 px-2 py-1.5 text-left text-[11px]">
            {item.status === "running" ? <Loader2 size={11} className="shrink-0 animate-spin text-agent" />
              : item.status === "done" ? <Check size={11} className="shrink-0 text-ok" /> : <X size={11} className="shrink-0 text-rec" />}
            <span className="shrink-0 text-fg">{TOOL_LABELS[item.name] ?? item.name}</span>
            {summary && <span className="min-w-0 flex-1 truncate text-faint">{summary}</span>}
            <ChevronRight size={11} className={cn("ml-auto shrink-0 text-faint transition-transform", open && "rotate-90")} />
          </button>
          {open && (
            <div className="border-t border-line px-2 py-1.5 font-mono text-[10px] leading-snug text-dim">
              <pre className="max-h-40 overflow-auto whitespace-pre-wrap break-all">{JSON.stringify(item.input, null, 1)}</pre>
              {item.result !== undefined && (
                <pre className="mt-1.5 max-h-48 overflow-auto border-t border-line/60 pt-1.5 whitespace-pre-wrap break-all">
                  {item.result.length > 4000 ? `${item.result.slice(0, 4000)}…` : item.result}
                </pre>
              )}
            </div>
          )}
        </div>
      );
    }
  }
}

function Composer({ value, onChange, onSend, onStop, running, disabled }: {
  value: string; onChange: (v: string) => void; onSend: () => void; onStop: () => void; running: boolean; disabled: boolean;
}) {
  const input = useRef<HTMLTextAreaElement>(null);

  // Grows with its text, up to a point.
  useLayoutEffect(() => {
    const el = input.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(160, el.scrollHeight)}px`;
  }, [value]);

  return (
    <div className="shrink-0 border-t border-line p-2">
      <div className="flex items-end gap-1.5 rounded-[8px] bg-bg px-2 py-1.5 ring-1 ring-line focus-within:ring-agent/60">
        <textarea
          ref={input}
          rows={1}
          value={value}
          disabled={disabled}
          placeholder={disabled ? "Waiting for the engine…" : "Ask the agent…"}
          onChange={(e) => onChange(e.target.value)}
          onKeyDown={(e) => {
            e.stopPropagation();
            // Enter sends; Shift+Enter starts a new line.
            if (e.key === "Enter" && !e.shiftKey && !e.nativeEvent.isComposing) {
              e.preventDefault();
              onSend();
            }
          }}
          className="max-h-40 min-h-[20px] flex-1 resize-none bg-transparent text-xs leading-5 text-fg outline-none placeholder:text-faint"
        />
        {running ? (
          <IconButton size="sm" title="Stop" onClick={onStop} className="bg-control text-fg"><Square size={10} fill="currentColor" /></IconButton>
        ) : (
          <IconButton size="sm" title="Send (Enter)" disabled={disabled || !value.trim()} onClick={onSend}
            className={cn(value.trim() && "bg-agent text-black hover:bg-agent")}>
            <ArrowUp size={12} />
          </IconButton>
        )}
      </div>
    </div>
  );
}

function KeySetup() {
  const [key, setKey] = useState("");

  return (
    <div className="flex flex-col gap-3 p-3 text-xs">
      <div className="flex items-center gap-2 text-sm text-fg"><KeyRound size={14} className="text-agent" /> Connect Claude</div>
      <p className="leading-relaxed text-dim">
        The agent uses Claude with your own Anthropic API key. Create one in the Anthropic Console under API keys; use is billed to your
        Anthropic account. The key stays on this Mac.
      </p>
      <form className="flex flex-col gap-2" onSubmit={(e) => {
        e.preventDefault();
        if (key.trim()) setApiKey(key);
      }}>
        <input type="password" autoComplete="off" value={key} onChange={(e) => setKey(e.target.value)} onKeyDown={(e) => e.stopPropagation()}
          placeholder="sk-ant-…" className="h-7 rounded-[4px] bg-bg px-2 text-xs text-fg outline-none ring-1 ring-line focus:ring-agent/60" />
        <Button size="sm" type="submit" disabled={!key.trim()} className="self-start bg-agent text-black hover:bg-agent">Save key</Button>
      </form>
      <p className="text-faint">You can also use Claude Code: the agent tools are its MCP server (see the README).</p>
    </div>
  );
}

/** Loads the open project's conversation. */
export function useAgentConversation() {
  const project = useEngine((s) => s.project);
  useEffect(() => {
    void import("./agent").then((m) => m.loadConversation(project));
  }, [project]);
}
