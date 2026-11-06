import { Loader2, Mic, Search, Sparkles, Square } from "lucide-react";
import { useEffect, useLayoutEffect, useRef } from "react";

import { IconButton } from "@/components/controls";
import { VoiceModelOffer } from "@/features/agent/Composer";
import { BAR_COUNT, useRecorder } from "@/features/agent/voice";
import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import type { Item } from "./commands";
import {
  closeSpotlight, finishListening, listen, moveSpotlightSelection, runSpotlight, setSpotlightText, spotlightItems, useSpotlight,
} from "./spotlightState";

/** The command box (⌘K): over everything, in the upper middle of the screen. */
export function Spotlight() {
  const open = useSpotlight((s) => s.open);
  if (!open) return null;
  return <SpotlightBox />;
}

function SpotlightBox() {
  const { mode, text, selected, needsModel, known } = useSpotlight();
  const previewing = useEngine((s) => s.previewing);
  const input = useRef<HTMLInputElement>(null);
  const list = useRef<HTMLDivElement>(null);
  const items = spotlightItems({ text, known });
  const chosen = Math.min(selected, items.length - 1);

  useEffect(() => {
    if (mode === "typing") input.current?.focus();
  }, [mode]);

  useLayoutEffect(() => {
    list.current?.querySelector<HTMLElement>(`[data-index="${chosen}"]`)?.scrollIntoView({ block: "nearest" });
  }, [chosen]);

  return (
    <div className="fixed inset-0 z-50 flex justify-center bg-black/25 px-4 pt-[18vh]" onMouseDown={(e) => e.target === e.currentTarget && closeSpotlight()}>
      <div className="flex max-h-[min(560px,70vh)] w-full max-w-[620px] animate-rise flex-col self-start overflow-hidden rounded-2xl border border-line bg-panel/95 shadow-[0_24px_80px_rgb(0_0_0/0.6)] backdrop-blur-md">
        {mode === "typing" ? (
          <div className="flex h-12 shrink-0 items-center gap-2.5 px-4">
            <Search size={16} className="shrink-0 text-faint" />
            <input
              ref={input}
              value={text}
              onChange={(e) => setSpotlightText(e.target.value)}
              onKeyDown={(e) => {
                e.stopPropagation();
                if (e.key === "ArrowDown" || e.key === "ArrowUp") {
                  e.preventDefault();
                  moveSpotlightSelection(e.key === "ArrowDown" ? 1 : -1);
                } else if (e.key === "Enter" && !e.nativeEvent.isComposing) {
                  e.preventDefault();
                  runSpotlight();
                }
              }}
              placeholder="A command, or ask the agent: add a hat loop"
              className="min-w-0 flex-1 bg-transparent text-[15px] text-fg outline-none placeholder:text-faint"
            />
            <IconButton size="sm" title="Talk (or hold ⌘K)" onClick={() => void listen()} className="h-8 w-8 shrink-0 rounded-full"><Mic size={14} /></IconButton>
          </div>
        ) : (
          <Listening mode={mode} />
        )}

        {needsModel && <div className="px-3 pb-2"><VoiceModelOffer onClose={() => useSpotlight.setState({ needsModel: false })} /></div>}

        {mode === "typing" && items.length > 0 && (
          <div ref={list} className="min-h-0 overflow-y-auto border-t border-line p-1.5">
            {items.map((item, i) => (
              <Row key={item.id} item={item} index={i} selected={i === chosen} playing={!!item.sample && previewing === item.sample.path} />
            ))}
          </div>
        )}

        <div className="flex shrink-0 items-center gap-3 border-t border-line px-4 py-1.5 text-[10px] text-faint">
          {mode === "listening" ? <span>Let go of ⌘ to run it · Esc to stop listening</span> : (
            <>
              <span>↵ run</span><span>↑↓ choose</span><span>hold ⌘K to talk</span><span className="flex-1" /><span>esc</span>
            </>
          )}
        </div>
      </div>
    </div>
  );
}

function Row({ item, index, selected, playing }: { item: Item; index: number; selected: boolean; playing: boolean }) {
  const Icon = item.icon;
  const agent = item.kind === "agent";
  return (
    <button type="button" data-index={index}
      className={cn("flex h-9 w-full items-center gap-2.5 rounded-lg px-2.5 text-left text-[13px]", selected ? "bg-hover text-fg" : "text-dim hover:bg-hover/60")}
      onMouseMove={() => !selected && useSpotlight.setState({ selected: index })}
      onClick={() => runSpotlight(item)}>
      {agent ? <Sparkles size={14} className="shrink-0 text-agent" /> : playing ? <Square size={11} className="shrink-0 text-accent" fill="currentColor" /> : <Icon size={14} className="shrink-0" />}
      <span className={cn("min-w-0 truncate", agent && "text-fg")}>{agent ? `“${item.label}”` : item.label}</span>
      <span className="flex-1" />
      {item.detail && <span className={cn("shrink-0 truncate text-[11px]", agent ? "text-agent" : "text-faint")}>{item.detail}</span>}
    </button>
  );
}

/** While listening: the voice's level as bars; then the words being worked out. */
function Listening({ mode }: { mode: "listening" | "transcribing" }) {
  const levels = useRecorder((r) => r.levels);
  const starting = useRecorder((r) => r.state === "starting");
  const level = levels[levels.length - 1] ?? 0;
  return (
    <div className="flex h-12 shrink-0 items-center gap-3 px-4">
      {mode === "transcribing" ? (
        <>
          <Loader2 size={16} className="shrink-0 animate-spin text-agent" />
          <span className="text-shimmer text-[14px]">Working out what you said…</span>
        </>
      ) : (
        <>
          <span className="flex h-7 w-7 shrink-0 items-center justify-center rounded-full bg-agent/15 text-agent"
            style={{ boxShadow: `0 0 ${4 + level * 22}px ${level * 5}px color-mix(in srgb, var(--color-agent) ${Math.round(15 + level * 40)}%, transparent)` }}>
            <Mic size={14} />
          </span>
          <span className="shrink-0 text-[14px] text-fg">{starting ? "Starting the microphone…" : "Listening…"}</span>
          <div className="flex h-7 min-w-0 flex-1 items-center justify-end gap-[2px] overflow-hidden" role="img" aria-label="Listening">
            {levels.map((l, i) => (
              <span key={i} className="w-[2px] shrink-0 rounded-full bg-agent transition-[height] duration-100 ease-out"
                style={{ height: `${Math.max(8, l * 100)}%`, opacity: 0.25 + (0.75 * i) / BAR_COUNT }} />
            ))}
          </div>
          <IconButton size="sm" title="Done (or let go of ⌘)" onClick={() => void finishListening(true)} className="h-8 w-8 shrink-0 rounded-full bg-agent text-black hover:bg-agent">
            <Square size={10} fill="currentColor" />
          </IconButton>
        </>
      )}
    </div>
  );
}
