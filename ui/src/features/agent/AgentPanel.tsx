import { AlertTriangle, ArrowDown, ArrowUp, Check, ChevronRight, Copy, Download, History, KeyRound, Loader2, Mic, RotateCcw, Sparkles, Square, SquarePen, Trash2, X } from "lucide-react";
import { Fragment, useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import Markdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { Button, IconButton, Tabs } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuTrigger } from "@/components/menu";
import { Activity } from "@/features/history/Activity";
import { cn } from "@/lib/cn";
import { toast, useEngine } from "@/state/engine";
import { deleteChat, loadConversation, newChat, openChat, refreshChats, retry, send, setApiKey, stop, useAgent, useApiKey, useChats, type ChatItem, type ChatSummary } from "./agent";
import { MARKDOWN } from "./markdown";
import { NotesView } from "./NotesView";
import { StylesView } from "./StylesView";
import { MapsView } from "./MapsView";
import { useAgentView, type AgentView } from "./maps";
import { BAR_COUNT, downloadVoiceModel, loadVoiceModel, RECORDER_ERRORS, transcribe, transcriptText, useRecording, useVoiceModel, useVoiceRecorder, useVoiceRequest } from "./voice";

const SUGGESTIONS = [
  "Make a 4-bar techno groove at 128 BPM with kick, clap and hats",
  "Add a bassline that follows the kick",
  "What would make this groove more hypnotic?",
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
  notes: "Notes",
  style_guide: "Style guide",
  melody: "Hear a melody",
};

const VOICE_SHORTCUT = "⌘⇧M";

/** One line about what a tool call was for. */
function toolSummary(input: unknown) {
  if (!input || typeof input !== "object") return "";
  const i = input as Record<string, unknown>;
  const value = i.label ?? i.query ?? i.action ?? i.path ?? i.category;
  return typeof value === "string" ? value.replace(/^.*\//, "") : "";
}

export function AgentPanel() {
  const view = useAgentView((s) => s.view);
  const recording = useRecording();
  const setView = (v: AgentView) => useAgentView.setState({ view: v });
  const items = useAgent((s) => s.items);
  const running = useAgent((s) => s.running);

  return (
    <div className="flex h-full flex-col">
      <div className="flex h-8 shrink-0 items-center gap-1 border-b border-line px-2">
        <Tabs<Exclude<AgentView, "chats">> value={view === "chats" ? "chat" : view} onChange={setView}
          items={[{ value: "chat", label: <span className="flex items-center gap-1">Chat{recording && view !== "chat" && <RecordingDot />}</span>,
            title: recording && view !== "chat" ? "A voice message is being recorded" : undefined }, { value: "notes", label: "Notes", title: "What the agent should always keep in mind" },
            { value: "styles", label: "Styles", title: "How the agent makes each style" },
            { value: "maps", label: "Maps", title: "Your reference tracks, heard into song maps" }, { value: "changes", label: "Changes" }]} />
        <div className="flex-1" />
        {(view === "chat" || view === "chats") && (
          <div className="flex shrink-0 items-center gap-1">
            <IconButton size="sm" title="Conversations" onClick={() => setView(view === "chats" ? "chat" : "chats")}
              className={cn(view === "chats" && "bg-control text-fg")}><History size={12} /></IconButton>
            <IconButton size="sm" title="New conversation" disabled={running || (items.length === 0 && view === "chat")}
              onClick={() => {
                newChat();
                setView("chat");
              }}><SquarePen size={12} /></IconButton>
          </div>
        )}
      </div>
      <div className="min-h-0 flex-1">
        {view === "chat" ? <Chat /> : view === "chats" ? <Conversations onOpen={() => setView("chat")} /> : view === "notes" ? <NotesView /> : view === "styles" ? <StylesView /> : view === "maps" ? <MapsView /> : <Activity />}
      </div>
    </div>
  );
}

/** A voice message is being recorded (shown where the message box isn't). */
export function RecordingDot() {
  return <span className="h-1.5 w-1.5 shrink-0 animate-pulse rounded-full bg-rec shadow-[0_0_6px_var(--color-rec)]" />;
}

function ago(ms: number) {
  const minutes = Math.round((Date.now() - ms) / 60000);
  if (minutes < 1) return "just now";
  if (minutes < 60) return `${minutes} min ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours} h ago`;
  const days = Math.round(hours / 24);
  if (days < 7) return `${days} d ago`;
  return new Date(ms).toLocaleDateString(undefined, { day: "numeric", month: "short", year: days > 300 ? "numeric" : undefined });
}

/** Every conversation: this song's first, then the other songs'. Any can be reopened here, where
    it carries on with this song; deleting one asks once more. */
function Conversations({ onOpen }: { onOpen: () => void }) {
  const chats = useChats((s) => s.chats);
  const project = useAgent((s) => s.project);
  const current = useAgent((s) => s.chatId);
  const running = useAgent((s) => s.running);
  const connected = useEngine((s) => s.status === "connected");
  const [confirming, setConfirming] = useState<string>();

  useEffect(() => {
    if (connected) void refreshChats().catch(() => {});
  }, [connected]);

  const here = chats.filter((c) => c.project === project);
  const elsewhere = chats.filter((c) => c.project !== project);

  const row = (c: ChatSummary, showSong: boolean) => (
    <div key={c.id}
      onClick={() => {
        if (running) return;
        void openChat(c.id).then(onOpen, (e) => toast(`Couldn't open the conversation: ${e instanceof Error ? e.message : e}`));
      }}
      className={cn("group flex cursor-default items-start gap-2 rounded-[6px] px-2 py-1.5", c.id === current ? "bg-control" : "hover:bg-raised", running && "opacity-60")}>
      <div className="min-w-0 flex-1">
        <div className="truncate text-xs text-fg">{c.title || "Untitled"}</div>
        <div className="truncate text-2xs text-faint">
          {showSong ? `${c.projectName} · ` : ""}{c.messages} {c.messages === 1 ? "message" : "messages"} · {ago(c.updated)}
        </div>
      </div>
      {confirming === c.id ? (
        <div className="flex shrink-0 items-center gap-1" onClick={(e) => e.stopPropagation()}>
          <button type="button" className="rounded-[4px] bg-rec px-1.5 py-0.5 text-2xs text-black" onClick={() => {
            setConfirming(undefined);
            void deleteChat(c.id);
          }}>Delete</button>
          <button type="button" className="text-faint hover:text-fg" title="Keep it" onClick={() => setConfirming(undefined)}><X size={11} /></button>
        </div>
      ) : (
        <button type="button" title="Delete this conversation" disabled={running && c.id === current}
          onClick={(e) => {
            e.stopPropagation();
            setConfirming(c.id);
          }}
          className="mt-0.5 shrink-0 text-faint opacity-0 hover:text-rec group-hover:opacity-100"><Trash2 size={11} /></button>
      )}
    </div>
  );

  return (
    <div className="h-full overflow-y-auto overscroll-contain px-2 py-2">
      {chats.length === 0 && <div className="px-2 py-6 text-center text-xs text-faint">No conversations yet.</div>}
      {here.length > 0 && (
        <>
          <div className="px-2 pb-1 text-2xs uppercase tracking-wide text-faint">This song</div>
          {here.map((c) => row(c, false))}
        </>
      )}
      {elsewhere.length > 0 && (
        <>
          <div className={cn("px-2 pb-1 text-2xs uppercase tracking-wide text-faint", here.length > 0 && "mt-3")}>Other songs</div>
          {elsewhere.map((c) => row(c, true))}
        </>
      )}
    </div>
  );
}

//==============================================================================
type ToolItem = Extract<ChatItem, { kind: "tool" }>;
type Block = { kind: "tools"; tools: ToolItem[]; key: number } | { kind: "item"; item: ChatItem; key: number };

/** Consecutive tool calls show as one block, like a step of work. */
function toBlocks(items: ChatItem[]): Block[] {
  const blocks: Block[] = [];
  items.forEach((item, i) => {
    const last = blocks.at(-1);
    if (item.kind === "tool" && last?.kind === "tools") last.tools.push(item);
    else if (item.kind === "tool") blocks.push({ kind: "tools", tools: [item], key: i });
    else blocks.push({ kind: "item", item, key: i });
  });
  return blocks;
}

function Chat() {
  const key = useApiKey((s) => s.key);
  const items = useAgent((s) => s.items);
  const running = useAgent((s) => s.running);
  const connected = useEngine((s) => s.status === "connected");
  const [draft, setDraft] = useState("");
  const list = useRef<HTMLDivElement>(null);
  const following = useRef(true);
  const [atBottom, setAtBottom] = useState(true);
  const blocks = useMemo(() => toBlocks(items), [items]);

  const toBottom = useCallback((behavior: ScrollBehavior = "auto") => {
    const el = list.current;
    if (el) el.scrollTo({ top: el.scrollHeight, behavior });
  }, []);

  // Follows the conversation while it's scrolled to the bottom.
  useLayoutEffect(() => {
    if (following.current) toBottom();
  }, [items, toBottom]);

  if (!key) return <KeySetup />;

  const submit = (text = draft) => {
    if (!text.trim() || running || !connected) return;
    following.current = true;
    setDraft("");
    void send(text.trim());
    requestAnimationFrame(() => toBottom("smooth"));
  };

  const last = items.at(-1);
  const working = running && last?.kind !== "assistant" && !(last?.kind === "tool" && last.status === "running");

  return (
    <div className="flex h-full flex-col">
      <div className="relative min-h-0 flex-1">
        <div ref={list} className="h-full overflow-y-auto overscroll-contain px-3 py-3"
          onScroll={(e) => {
            const el = e.currentTarget;
            const bottom = el.scrollHeight - el.scrollTop - el.clientHeight < 60;
            following.current = bottom;
            setAtBottom(bottom);
          }}>
          {items.length === 0 ? (
            <Welcome connected={connected} onPick={submit} />
          ) : (
            <div className="flex flex-col gap-3">
              {blocks.map((block, i) => (
                <Fragment key={block.key}>
                  {block.kind === "tools"
                    ? <ToolGroup tools={block.tools} />
                    : <Item item={block.item} last={i === blocks.length - 1} running={running} />}
                </Fragment>
              ))}
              {working && <div className="text-shimmer flex items-center gap-2 py-0.5 text-xs">Working…</div>}
            </div>
          )}
        </div>
        <button type="button" title="Jump to the latest"
          className={cn("absolute bottom-2 left-1/2 flex h-7 w-7 -translate-x-1/2 items-center justify-center rounded-full bg-raised text-dim shadow-lg ring-1 ring-line transition-opacity hover:text-fg",
            atBottom ? "pointer-events-none opacity-0" : "opacity-100")}
          tabIndex={atBottom ? -1 : 0}
          onClick={() => {
            following.current = true;
            toBottom("smooth");
          }}>
          <ArrowDown size={13} />
        </button>
      </div>
      <Composer value={draft} onChange={setDraft} onSend={submit} onStop={stop} running={running} disabled={!connected} />
    </div>
  );
}

function Welcome({ connected, onPick }: { connected: boolean; onPick: (text: string) => void }) {
  return (
    <div className="flex animate-rise flex-col gap-3 pt-2">
      <div className="flex items-center gap-2 text-sm text-fg"><Sparkles size={14} className="text-agent" /> What should we make?</div>
      <p className="text-xs leading-relaxed text-dim">
        The agent arranges, mixes and masters with the same tools as Claude Code, in this project. Its changes show up live,
        outlined in violet, and one ⌘Z undoes a request. Type, or press <Kbd>{VOICE_SHORTCUT}</Kbd> and just say it.
      </p>
      <div className="flex flex-col gap-1.5">
        {SUGGESTIONS.map((s) => (
          <button key={s} type="button" disabled={!connected} onClick={() => onPick(s)}
            className="rounded-[8px] bg-raised px-2.5 py-2 text-left text-xs text-dim ring-1 ring-line transition-colors hover:text-fg hover:ring-agent/50">
            {s}
          </button>
        ))}
      </div>
    </div>
  );
}

function Kbd({ children }: { children: React.ReactNode }) {
  return <kbd className="rounded-[3px] bg-control px-1 py-px font-sans text-[10px] text-dim ring-1 ring-line">{children}</kbd>;
}

function Item({ item, last, running }: { item: ChatItem; last: boolean; running: boolean }) {
  switch (item.kind) {
    case "user":
      return (
        <div className="group ml-8 flex max-w-[90%] animate-rise items-end gap-1 self-end">
          <CopyButton text={item.text} />
          <Message text={item.text} className="rounded-[12px] rounded-br-[4px] bg-control px-3 py-2 text-xs leading-relaxed whitespace-pre-wrap break-words text-fg">
            {item.text}
          </Message>
        </div>
      );
    case "assistant":
      return (
        <div className="group relative">
          <Message text={item.text} className="text-xs leading-relaxed break-words text-fg">
            <Markdown remarkPlugins={[remarkGfm]} components={MARKDOWN}>{item.text}</Markdown>
          </Message>
          {!(running && last) && item.text && <CopyButton text={item.text} />}
        </div>
      );
    case "note":
      return (
        <div className="group relative">
          <Message text={item.text} className="border-l-2 border-agent/40 pl-2 text-[11px] leading-relaxed text-faint italic">{item.text}</Message>
          {!(running && last) && <CopyButton text={item.text} />}
        </div>
      );
    case "error":
      return (
        <div className="group flex items-start gap-2 rounded-[8px] bg-rec/10 px-2.5 py-2 text-xs text-rec ring-1 ring-rec/30">
          <AlertTriangle size={13} className="mt-px shrink-0" />
          <Message text={item.text} className="min-w-0 flex-1">{item.text}</Message>
          <CopyButton text={item.text} />
          {last && !running && (
            <button type="button" onClick={() => void retry()} className="flex shrink-0 items-center gap-1 rounded-[4px] px-1.5 py-0.5 text-fg hover:bg-rec/20">
              <RotateCcw size={11} /> Try again
            </button>
          )}
        </div>
      );
    case "tool":
      return <ToolGroup tools={[item]} />;
  }
}

const copy = (text: string) => navigator.clipboard?.writeText(text);

/** Text in the conversation that can be selected, with the menu a right click is expected to
    bring up: copy what's selected, copy the whole message, select it. */
function Message({ text, className, children }: { text: string; className?: string; children: React.ReactNode }) {
  const box = useRef<HTMLDivElement>(null);
  const [selected, setSelected] = useState("");

  // What's selected inside this message when the menu opens.
  const selectionHere = () => {
    const selection = window.getSelection();
    const el = box.current;
    if (!selection || selection.isCollapsed || !el) return "";
    return el.contains(selection.anchorNode) || el.contains(selection.focusNode) ? selection.toString() : "";
  };

  return (
    <ContextMenu onOpenChange={(open) => open && setSelected(selectionHere())}>
      <ContextMenuTrigger asChild>
        <div ref={box} className={cn("cursor-text select-text", className)}>{children}</div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem disabled={!selected} shortcut="⌘C" onSelect={() => void copy(selected)}>Copy</ContextMenuItem>
        <ContextMenuItem onSelect={() => void copy(text)}>Copy message</ContextMenuItem>
        <ContextMenuItem onSelect={() => {
          const el = box.current;
          const selection = window.getSelection();
          if (!el || !selection) return;
          const range = document.createRange();
          range.selectNodeContents(el);
          selection.removeAllRanges();
          selection.addRange(range);
        }}>Select message</ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}

function CopyButton({ text }: { text: string }) {
  const [copied, setCopied] = useState(false);
  return (
    <button type="button" title={copied ? "Copied" : "Copy"}
      className="mt-1 flex h-5 w-5 shrink-0 items-center justify-center rounded-[4px] text-faint opacity-0 transition-opacity group-hover:opacity-100 hover:bg-hover hover:text-fg focus-visible:opacity-100"
      onClick={() => void copy(text)?.then(() => {
        setCopied(true);
        setTimeout(() => setCopied(false), 1500);
      })}>
      {copied ? <Check size={11} /> : <Copy size={11} />}
    </button>
  );
}

/** A run of tool calls: what's happening now while it works, a count once it's done, and each
    call's input and result when opened. */
function ToolGroup({ tools }: { tools: ToolItem[] }) {
  const [open, setOpen] = useState(false);
  const current = tools.find((t) => t.status === "running");
  const failed = tools.filter((t) => t.status === "error").length;
  const label = (t: ToolItem) => TOOL_LABELS[t.name] ?? t.name;

  return (
    <div className="rounded-[8px] bg-raised/60 ring-1 ring-line">
      <button type="button" onClick={() => setOpen(!open)} className="flex w-full items-center gap-2 rounded-[8px] px-2.5 py-1.5 text-left text-[11px]">
        {current ? <Loader2 size={12} className="shrink-0 animate-spin text-agent" />
          : failed ? <AlertTriangle size={12} className="shrink-0 text-accent" /> : <Check size={12} className="shrink-0 text-ok" />}
        {current ? (
          <span className="text-shimmer min-w-0 flex-1 truncate">{label(current)}{toolSummary(current.input) ? ` · ${toolSummary(current.input)}` : ""}</span>
        ) : (
          <span className="min-w-0 flex-1 truncate text-dim">
            {tools.length === 1 ? label(tools[0]) : `${tools.length} steps`}
            {tools.length === 1 && toolSummary(tools[0].input) && <span className="text-faint"> · {toolSummary(tools[0].input)}</span>}
            {failed > 0 && <span className="text-accent"> · {failed} didn't work</span>}
          </span>
        )}
        <ChevronRight size={11} className={cn("shrink-0 text-faint transition-transform", open && "rotate-90")} />
      </button>
      {open && (
        <div className="flex flex-col border-t border-line py-1">
          {tools.map((t) => <ToolRow key={t.id} tool={t} />)}
        </div>
      )}
    </div>
  );
}

function ToolRow({ tool }: { tool: ToolItem }) {
  const [open, setOpen] = useState(false);
  const summary = toolSummary(tool.input);

  return (
    <div>
      <button type="button" onClick={() => setOpen(!open)} className="flex w-full items-center gap-1.5 px-2.5 py-1 text-left text-[11px] hover:bg-hover/50">
        {tool.status === "running" ? <Loader2 size={10} className="shrink-0 animate-spin text-agent" />
          : tool.status === "done" ? <Check size={10} className="shrink-0 text-ok" /> : <X size={10} className="shrink-0 text-rec" />}
        <span className="shrink-0 text-fg">{TOOL_LABELS[tool.name] ?? tool.name}</span>
        {summary && <span className="min-w-0 flex-1 truncate text-faint">{summary}</span>}
      </button>
      {open && (
        <div className="mx-2.5 mb-1.5 rounded-[6px] bg-bg px-2 py-1.5 font-mono text-[10px] leading-snug text-dim">
          <Message text={JSON.stringify(tool.input, null, 1)}>
            <pre className="max-h-40 overflow-auto whitespace-pre-wrap break-all">{JSON.stringify(tool.input, null, 1)}</pre>
          </Message>
          {tool.result !== undefined && (
            <Message text={tool.result}>
              <pre className={cn("mt-1.5 max-h-48 overflow-auto border-t border-line/60 pt-1.5 whitespace-pre-wrap break-all", tool.status === "error" && "text-rec")}>
                {tool.result.length > 4000 ? `${tool.result.slice(0, 4000)}…` : tool.result}
              </pre>
            </Message>
          )}
        </div>
      )}
    </div>
  );
}

//==============================================================================
const clock = (ms: number) => {
  const seconds = Math.floor(ms / 1000);
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
};

/** The message box: a field that grows with its text, voice messages (live level bars, then
    cancel, check the text first, or send it straight away), send, and stop while the agent works. */
function Composer({ value, onChange, onSend, onStop, running, disabled }: {
  value: string; onChange: (v: string) => void; onSend: (text?: string) => void; onStop: () => void; running: boolean; disabled: boolean;
}) {
  const input = useRef<HTMLTextAreaElement>(null);
  const recorder = useVoiceRecorder();
  const model = useVoiceModel();
  const [transcribing, setTranscribing] = useState(false);
  const [offerModel, setOfferModel] = useState(false);
  const recording = recorder.state !== "idle";
  const level = recorder.levels[recorder.levels.length - 1] ?? 0;

  // Grows with its text, up to a point.
  useLayoutEffect(() => {
    const el = input.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(180, el.scrollHeight)}px`;
  }, [value, recording, transcribing]);

  useEffect(() => {
    if (!disabled) void loadVoiceModel(true).catch(() => {});
  }, [disabled]);

  useEffect(() => {
    if (recorder.error) toast(RECORDER_ERRORS[recorder.error], "error");
  }, [recorder.error]);

  // The model finished downloading: say so once.
  const wasDownloading = useRef(false);
  useEffect(() => {
    if (wasDownloading.current && model?.ready) {
      setOfferModel(false);
      void loadVoiceModel(true).catch(() => {});
      toast(`Voice is ready: click the microphone or press ${VOICE_SHORTCUT}`);
    }
    wasDownloading.current = !!model?.downloading;
  }, [model?.ready, model?.downloading]);

  const startVoice = useCallback(() => {
    if (disabled || transcribing) return;
    if (!model?.ready) {
      setOfferModel(true);
      return;
    }
    void loadVoiceModel(true).catch(() => {});
    void recorder.start();
  }, [disabled, transcribing, model?.ready, recorder]);

  /** Stops recording and turns it into text: into the field to check, or sent as it is. */
  const accept = useCallback(async (sendNow: boolean) => {
    const audio = await recorder.stop();
    if (!audio) return;
    setTranscribing(true);
    try {
      const text = transcriptText(await transcribe(audio));
      if (!text) {
        toast("Didn't catch that. Try again a little closer to the microphone.");
        return;
      }
      const combined = value.trim() ? `${value.trimEnd()} ${text}` : text;
      if (sendNow && !running) onSend(combined);
      else {
        onChange(combined);
        requestAnimationFrame(() => {
          input.current?.focus();
          input.current?.setSelectionRange(combined.length, combined.length);
        });
      }
    } catch (e) {
      toast(e instanceof Error ? e.message : String(e), "error");
    } finally {
      setTranscribing(false);
    }
  }, [recorder, value, running, onSend, onChange]);

  // The voice shortcut: starts recording, or finishes it.
  const request = useVoiceRequest((s) => s.pending);
  useEffect(() => {
    if (!request) return;
    useVoiceRequest.setState({ pending: false });
    if (recorder.state === "recording") void accept(false);
    else if (recorder.state === "idle") startVoice();
  }, [request, recorder.state, accept, startVoice]);

  // While recording: Enter keeps the text to check, ⌘Enter sends it, Escape throws it away.
  useEffect(() => {
    if (!recording) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== "Enter" && e.key !== "Escape") return;
      e.preventDefault();
      e.stopPropagation();
      if (e.key === "Escape") recorder.cancel();
      else void accept(e.metaKey || e.ctrlKey);
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [recording, recorder, accept]);

  const canSend = !!value.trim() && !disabled && !transcribing;

  return (
    <div className="shrink-0 p-2 pt-1">
      {offerModel && !model?.ready && <VoiceModelOffer onClose={() => setOfferModel(false)} />}
      <div
        className={cn(
          "relative rounded-[12px] bg-raised ring-1 transition-[box-shadow,ring-color] duration-150",
          recording ? "ring-agent/60" : "ring-line focus-within:ring-agent/50",
        )}
        // Glows with the voice while recording.
        style={recording ? { boxShadow: `0 0 ${6 + level * 26}px ${level * 6}px color-mix(in srgb, var(--color-agent) ${Math.round(18 + level * 40)}%, transparent)` } : undefined}
      >
        {recording ? (
          <div className="flex h-[72px] items-center gap-2 px-2.5">
            <span className={cn("h-2 w-2 shrink-0 rounded-full bg-rec", recorder.state === "recording" && "animate-pulse")} />
            <span className="w-8 shrink-0 text-[11px] text-dim tabular">{clock(recorder.elapsed)}</span>
            <div className="flex h-9 min-w-0 flex-1 items-center justify-end gap-[2px] overflow-hidden" role="img" aria-label="Recording">
              {recorder.levels.map((l, i) => (
                <span key={i} className="w-[2px] shrink-0 rounded-full bg-agent transition-[height] duration-100 ease-out"
                  style={{ height: `${Math.max(7, l * 100)}%`, opacity: 0.25 + (0.75 * i) / BAR_COUNT }} />
              ))}
            </div>
            <IconButton size="sm" title="Throw it away (Esc)" onClick={recorder.cancel} className="h-7 w-7 rounded-full"><X size={13} /></IconButton>
            <IconButton size="sm" title="Check the text before sending (Enter)" disabled={recorder.state !== "recording"} onClick={() => void accept(false)}
              className="h-7 w-7 rounded-full bg-control text-fg"><Check size={13} /></IconButton>
            <IconButton size="sm" title="Send (⌘Enter)" disabled={recorder.state !== "recording" || running} onClick={() => void accept(true)}
              className="h-7 w-7 rounded-full bg-agent text-black hover:bg-agent"><ArrowUp size={13} /></IconButton>
          </div>
        ) : (
          <>
            <textarea
              ref={input}
              rows={1}
              value={value}
              disabled={disabled || transcribing}
              placeholder={disabled ? "Waiting for the engine…" : transcribing ? "" : "Ask the agent…"}
              onChange={(e) => onChange(e.target.value)}
              onKeyDown={(e) => {
                e.stopPropagation();
                // Enter sends; Shift+Enter starts a new line.
                if (e.key === "Enter" && !e.shiftKey && !e.nativeEvent.isComposing) {
                  e.preventDefault();
                  if (canSend && !running) onSend();
                }
              }}
              className="block max-h-[180px] min-h-[38px] w-full resize-none bg-transparent px-3 pt-2.5 pb-1 text-xs leading-5 text-fg outline-none placeholder:text-faint focus-visible:outline-none disabled:opacity-60"
            />
            {transcribing && (
              <div className="pointer-events-none absolute top-2.5 left-3 flex items-center gap-1.5 text-xs">
                <span className="text-shimmer">Transcribing…</span>
              </div>
            )}
            <div className="flex items-center gap-1 px-1.5 pb-1.5">
              <span className="ml-1.5 min-w-0 flex-1 truncate text-[10px] text-faint">
                {value.trim() ? "Enter to send · Shift+Enter for a new line" : `${VOICE_SHORTCUT} to talk`}
              </span>
              <IconButton size="sm" title={`Voice message (${VOICE_SHORTCUT})`} disabled={disabled || transcribing} onClick={startVoice}
                className="h-7 w-7 rounded-full">
                {transcribing ? <Loader2 size={13} className="animate-spin" /> : <Mic size={13} />}
              </IconButton>
              {running ? (
                <IconButton size="sm" title="Stop" onClick={onStop} className="h-7 w-7 rounded-full bg-control text-fg"><Square size={10} fill="currentColor" /></IconButton>
              ) : (
                <IconButton size="sm" title="Send (Enter)" disabled={!canSend} onClick={() => onSend()}
                  className={cn("h-7 w-7 rounded-full transition-colors", canSend ? "bg-agent text-black hover:bg-agent" : "bg-control")}>
                  <ArrowUp size={13} />
                </IconButton>
              )}
            </div>
          </>
        )}
      </div>
    </div>
  );
}

/** Voice needs Whisper's model on this Mac: what it is, and downloading it once. */
function VoiceModelOffer({ onClose }: { onClose: () => void }) {
  const model = useVoiceModel();
  const mb = Math.round((model?.modelBytes ?? 190e6) / 1e6);

  return (
    <div className="mb-2 animate-rise rounded-[10px] bg-raised p-2.5 text-xs ring-1 ring-line">
      <div className="flex items-start gap-2">
        <Mic size={13} className="mt-px shrink-0 text-agent" />
        <div className="min-w-0 flex-1 leading-relaxed text-dim">
          {model?.downloading ? (
            <>
              <div className="mb-1.5 text-fg">Downloading the speech model… {Math.round((model.progress ?? 0) * 100)}%</div>
              <div className="h-1 overflow-hidden rounded-full bg-control">
                <div className="h-full rounded-full bg-agent transition-[width] duration-200" style={{ width: `${(model.progress ?? 0) * 100}%` }} />
              </div>
            </>
          ) : (
            <>
              <span className="text-fg">Talk to the agent.</span> Your voice is turned into text on this Mac by Whisper, so nothing you say
              leaves it. That needs a speech model, downloaded once ({mb} MB).
              {model?.error && <div className="mt-1 text-rec">{model.error}</div>}
            </>
          )}
        </div>
        <IconButton size="sm" title="Not now" onClick={onClose} className="-mt-0.5 shrink-0"><X size={11} /></IconButton>
      </div>
      {!model?.downloading && (
        <div className="mt-2 flex justify-end">
          <Button size="sm" className="bg-agent text-black hover:bg-agent" onClick={() => void downloadVoiceModel().catch((e) => toast(String(e), "error"))}>
            <Download size={11} /> {model?.error ? "Try again" : `Download (${mb} MB)`}
          </Button>
        </div>
      )}
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
        Anthropic account. The key stays on this Mac. What you ask, and what the agent reads of the song to do it (names, notes, mix
        settings, sample file paths), is sent to Anthropic.
      </p>
      <form className="flex flex-col gap-2" onSubmit={(e) => {
        e.preventDefault();
        if (key.trim()) setApiKey(key);
      }}>
        <input type="password" autoComplete="off" value={key} onChange={(e) => setKey(e.target.value)} onKeyDown={(e) => e.stopPropagation()}
          placeholder="sk-ant-…" className="h-7 rounded-[6px] bg-bg px-2 text-xs text-fg outline-none ring-1 ring-line focus:ring-agent/60 focus-visible:outline-none" />
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
    void loadConversation(project);
  }, [project]);
}
