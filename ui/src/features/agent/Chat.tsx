import { AlertTriangle, ArrowDown, RotateCcw, Sparkles } from "lucide-react";
import { Fragment, useCallback, useLayoutEffect, useMemo, useRef, useState } from "react";
import Markdown from "react-markdown";
import remarkGfm from "remark-gfm";

import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { retry, send, stop, useAgent, useApiKey, type ChatItem } from "./agent";
import { MARKDOWN } from "./markdown";

import { Composer, VOICE_SHORTCUT } from "./Composer";

import { KeySetup } from "./KeySetup";
import { CopyButton, Message } from "./Message";
import { ToolGroup, ToolItem } from "./ToolCalls";

export const SUGGESTIONS = [
  "Make a 4-bar techno groove at 128 BPM with kick, clap and hats",
  "Add a bassline that follows the kick",
  "What would make this groove more hypnotic?",
  "Master the song to −14 LUFS",
];

export type Block = { kind: "tools"; tools: ToolItem[]; key: number } | { kind: "item"; item: ChatItem; key: number };

/** Consecutive tool calls show as one block, like a step of work. */
export function toBlocks(items: ChatItem[]): Block[] {
  const blocks: Block[] = [];
  items.forEach((item, i) => {
    const last = blocks.at(-1);
    if (item.kind === "tool" && last?.kind === "tools") last.tools.push(item);
    else if (item.kind === "tool") blocks.push({ kind: "tools", tools: [item], key: i });
    else blocks.push({ kind: "item", item, key: i });
  });
  return blocks;
}

export function Chat() {
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

export function Welcome({ connected, onPick }: { connected: boolean; onPick: (text: string) => void }) {
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

export function Kbd({ children }: { children: React.ReactNode }) {
  return <kbd className="rounded-[3px] bg-control px-1 py-px font-sans text-[10px] text-dim ring-1 ring-line">{children}</kbd>;
}

export function Item({ item, last, running }: { item: ChatItem; last: boolean; running: boolean }) {
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
