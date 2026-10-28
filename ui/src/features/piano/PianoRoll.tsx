import type { Note, Pattern, Project } from "@groovekit/schema/project";
import { ChevronDown, Minus, Plus } from "lucide-react";
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { Button, Empty, IconButton } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { newPattern } from "@/features/playlist/Playlist";
import { stepKey } from "@/features/rack/channels";
import { cn } from "@/lib/cn";
import { useLivePosition } from "@/lib/playhead";
import { floorTo, snapTo, startDrag } from "@/lib/drag";
import { colorFor, noteName, patternLength, STEP, ticksPerBar, ticksPerBeat } from "@/lib/model";
import { auditionNote } from "@/features/shell/actions";
import { engine, execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

const KEYS_W = 56;
const VELOCITY_H = 64;
const black = new Set([1, 3, 6, 8, 10]);

/** Notes copied with ⌘C, pasted into any pattern or channel with ⌘V. */
let noteClipboard: Note[] = [];

export function PianoRoll() {
  const doc = useEngine((s) => s.doc)!;
  const { selectedPattern, selectedChannel } = useUi();
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);
  const channel = doc.channels.find((c) => c.id === selectedChannel);

  if (!pattern || !channel) {
    return <Empty>{doc.channels.length === 0 ? "Add a channel in the channel rack first." : "Pick a pattern and a channel."}</Empty>;
  }

  return <Editor key={`${pattern.id}/${channel.id}`} doc={doc} pattern={pattern} channelId={channel.id} />;
}

/** Where pattern mode is playing this pattern. */
function PatternPlayhead({ pattern, px }: { pattern: string; px: number }) {
  const line = useRef<HTMLDivElement>(null);
  useLivePosition(({ ticks, playing }) => {
    const t = useEngine.getState().transport;
    const shown = playing && t.mode === "pattern" && t.pattern === pattern;
    if (!line.current) return;
    line.current.style.display = shown ? "" : "none";
    line.current.style.transform = `translateX(${ticks * px}px)`;
  }, [pattern, px]);
  return <div ref={line} className="pointer-events-none absolute top-0 bottom-0 left-0 z-10 w-px bg-fg/80 will-change-transform" style={{ display: "none" }} />;
}

/** The piano roll's grids, in ticks; 1 is off (free, to the tick). */
const GRIDS = [
  { label: "1/4", ticks: 960 }, { label: "1/8", ticks: 480 }, { label: "1/8T", ticks: 320 }, { label: "1/16", ticks: 240 },
  { label: "1/16T", ticks: 160 }, { label: "1/32", ticks: 120 }, { label: "1/64", ticks: 60 }, { label: "Off", ticks: 1 },
];

/** The shortest note drawn or resized without a grid: a 128th. */
const FREE_MIN = 30;

function Editor({ doc, pattern, channelId }: { doc: Project; pattern: Pattern; channelId: string }) {
  const { pianoZoom, pianoKeyHeight: keyH, pianoSnap } = useUi();
  const channel = doc.channels.find((c) => c.id === channelId)!;
  const color = colorFor(channel.id, channel.color);
  const px = pianoZoom / 960;
  // Notes snap to the grid; holding ⌘ while dragging places them freely.
  const step = pianoSnap;
  const stepFor = (ev: { metaKey: boolean }) => (ev.metaKey ? 1 : step);
  const minLength = (s: number) => (s <= 1 ? FREE_MIN : s);
  const bar = ticksPerBar(doc);
  const loop = patternLength(pattern, doc);
  const width = Math.max(loop, 4 * bar) * px + 200;
  const height = 128 * keyH;

  const stored = useMemo(() => pattern.notes.filter((n) => n.channel === channelId), [pattern.notes, channelId]);
  const others = useMemo(() => pattern.notes.filter((n) => n.channel !== channelId), [pattern.notes, channelId]);
  const [draft, setDraft] = useState<Note[] | null>(null);
  const [selected, setSelected] = useState<Set<number>>(new Set());
  const notes = draft ?? stored;
  const lastLength = useRef(STEP);
  const scroller = useRef<HTMLDivElement>(null);
  const keysRef = useRef<HTMLDivElement>(null);

  // Start scrolled to the notes, or around C4.
  useLayoutEffect(() => {
    const el = scroller.current;
    if (!el) return;
    const keys = stored.map((n) => n.key ?? 60);
    const centre = keys.length ? (Math.max(...keys) + Math.min(...keys)) / 2 : stepKey(channel);
    el.scrollTop = (127 - centre) * keyH - el.clientHeight / 2;
  }, []); // only when the editor opens

  // The keyboard follows the grid's vertical scroll, also after the grid re-renders.
  useLayoutEffect(() => {
    if (keysRef.current && scroller.current) keysRef.current.scrollTop = scroller.current.scrollTop;
  });

  const save = (next: Note[], label: string) => {
    setDraft(next);
    void execute({ type: "setNotes", pattern: pattern.id, channel: channelId, notes: next.map((n) => ({ ...n, channel: channelId })) }, { label })
      .finally(() => setDraft(null));
  };

  // Editing keys act on the piano roll while it's what was clicked last.
  const root = useRef<HTMLDivElement>(null);
  const active = useRef(false);

  useEffect(() => {
    const onDown = (e: PointerEvent) => {
      active.current = !!root.current?.contains(e.target as Node);
    };
    window.addEventListener("pointerdown", onDown, true);
    return () => window.removeEventListener("pointerdown", onDown, true);
  }, []);

  const quantize = () => {
    const targets = selected.size ? selected : new Set(stored.map((_, i) => i));
    const to = step > 1 ? step : 240;
    save(stored.map((n, i) => (targets.has(i) ? { ...n, start: Math.max(0, snapTo(n.start, to)) } : n)), "Quantize notes");
  };

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      if (!active.current || t?.tagName === "INPUT" || t?.tagName === "TEXTAREA") return;
      const mod = e.metaKey || e.ctrlKey;
      const k = e.key.toLowerCase();
      const all = new Set(stored.map((_, i) => i));
      const chosen = [...selected].map((i) => stored[i]).filter(Boolean);
      const handled = () => {
        e.preventDefault();
        e.stopImmediatePropagation();
      };

      if ((e.key === "Delete" || e.key === "Backspace") && selected.size) {
        handled();
        save(stored.filter((_, i) => !selected.has(i)), selected.size === 1 ? "Delete note" : `Delete ${selected.size} notes`);
        setSelected(new Set());
      } else if (mod && k === "a") {
        handled();
        setSelected(all);
      } else if (mod && k === "c" && chosen.length) {
        handled();
        noteClipboard = chosen.map((n) => ({ ...n }));
      } else if (mod && (k === "v" || k === "d")) {
        // Paste puts copied notes back at their times; duplicate repeats the selection right after itself.
        const source = k === "v" ? noteClipboard : chosen;
        if (!source.length) return;
        handled();
        const span = k === "d" ? Math.ceil((Math.max(...source.map((n) => n.start + n.length)) - Math.min(...source.map((n) => n.start))) / step) * step : 0;
        const added = source.map((n) => ({ ...n, channel: channelId, start: n.start + span }));
        const next = [...stored, ...added];
        const end = Math.max(...next.map((n) => n.start + n.length));
        // A pattern with a set length grows (in whole bars) to keep the new notes in its loop.
        if (pattern.length && end > pattern.length) {
          setDraft(next);
          void execute([
            { type: "updatePattern", pattern: pattern.id, changes: { length: Math.ceil(end / bar) * bar } },
            { type: "setNotes", pattern: pattern.id, channel: channelId, notes: next.map((n) => ({ ...n, channel: channelId })) },
          ], { label: k === "v" ? "Paste notes" : "Duplicate notes" }).finally(() => setDraft(null));
        } else {
          save(next, k === "v" ? "Paste notes" : "Duplicate notes");
        }
        setSelected(new Set(added.map((_, i) => stored.length + i)));
      } else if ((e.key === "ArrowUp" || e.key === "ArrowDown") && selected.size) {
        handled();
        const dk = (e.key === "ArrowUp" ? 1 : -1) * (e.shiftKey ? 12 : 1);
        save(stored.map((n, i) => (selected.has(i) ? { ...n, key: Math.min(127, Math.max(0, (n.key ?? 60) + dk)) } : n)), "Transpose notes");
        const first = chosen[0];
        if (first) auditionNote(channelId, Math.min(127, Math.max(0, (first.key ?? 60) + dk)));
      } else if ((e.key === "ArrowLeft" || e.key === "ArrowRight") && selected.size) {
        handled();
        const dt = (e.key === "ArrowRight" ? 1 : -1) * step;
        if (Math.min(...chosen.map((n) => n.start)) + dt < 0) return;
        save(stored.map((n, i) => (selected.has(i) ? { ...n, start: n.start + dt } : n)), "Move notes");
      } else if (k === "q" && !mod) {
        handled();
        quantize();
      }
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  });

  const [rubber, setRubber] = useState<{ x0: number; y0: number; x1: number; y1: number } | null>(null);

  const pointToNote = (e: { clientX: number; clientY: number }, el: HTMLElement) => {
    const r = el.getBoundingClientRect();
    return { tick: (e.clientX - r.left) / px, key: 127 - Math.floor((e.clientY - r.top) / keyH) };
  };

  const onGridDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    const el = e.currentTarget;

    // Shift- or ⌘-drag selects the notes in a box.
    if (e.shiftKey || e.metaKey || e.ctrlKey) {
      const r = el.getBoundingClientRect();
      const x0 = e.clientX - r.left, y0 = e.clientY - r.top;
      const before = e.shiftKey ? selected : new Set<number>();
      startDrag(e, (_dx, _dy, ev) => {
        const box = { x0, y0, x1: ev.clientX - r.left, y1: ev.clientY - r.top };
        setRubber(box);
        const [xa, xb] = [Math.min(box.x0, box.x1), Math.max(box.x0, box.x1)];
        const [ya, yb] = [Math.min(box.y0, box.y1), Math.max(box.y0, box.y1)];
        const hit = stored.flatMap((n, i) => {
          const top = (127 - (n.key ?? 60)) * keyH;
          return n.start * px < xb && (n.start + n.length) * px > xa && top < yb && top + keyH > ya ? [i] : [];
        });
        setSelected(new Set([...before, ...hit]));
      }, () => setRubber(null), "crosshair");
      return;
    }

    const { tick, key } = pointToNote(e, el);
    const start = floorTo(tick, step);
    auditionNote(channelId, key);
    const created: Note = { channel: channelId, start, length: lastLength.current, key, velocity: 100 };
    const index = stored.length;
    setSelected(new Set([index]));
    setDraft([...stored, created]);

    const lengthAt = (ev: PointerEvent) => {
      const s = stepFor(ev);
      return Math.max(minLength(s), snapTo(pointToNote(ev, el).tick - start, s));
    };

    startDrag(e, (_dx, _dy, ev) => {
      setDraft([...stored, { ...created, length: lengthAt(ev) }]);
    }, (_dx, _dy, ev, moved) => {
      // A click draws a note as long as the last one; a drag sets the length.
      const length = moved ? lengthAt(ev) : lastLength.current;
      lastLength.current = length;
      save([...stored, { ...created, length }], `Add note ${noteName(key)}`);
    }, "ew-resize");
  };

  const onNoteDown = (e: React.PointerEvent, index: number, edge: "right" | "body") => {
    e.stopPropagation();
    if (e.button !== 0) return;

    const sel = selected.has(index) ? selected : e.shiftKey ? new Set([...selected, index]) : new Set([index]);
    setSelected(sel);
    const grid = (e.currentTarget as HTMLElement).parentElement!;
    const origin = pointToNote(e, grid);
    const copy = e.altKey;
    const base = stored;
    let next = base;
    let lastDk = 0;

    if (edge === "body") auditionNote(channelId, base[index].key ?? 60, base[index].velocity ?? 100);

    startDrag(e, (_dx, _dy, ev) => {
      const at = pointToNote(ev, grid);
      const s = stepFor(ev);
      if (edge === "right") {
        const n = base[index];
        const length = Math.max(minLength(s), snapTo(at.tick - n.start, s));
        const dl = length - n.length;
        next = base.map((m, i) => (sel.has(i) ? { ...m, length: Math.max(minLength(s), m.length + dl) } : m));
      } else {
        const n = base[index];
        const dt = snapTo(n.start + (at.tick - origin.tick), s) - n.start;
        const dk = at.key - origin.key;
        const minStart = Math.min(...[...sel].map((i) => base[i].start));
        const shift = Math.max(-minStart, dt);
        if (dk !== lastDk) {
          lastDk = dk;
          auditionNote(channelId, Math.min(127, Math.max(0, (n.key ?? 60) + dk)));
        }
        const moved = base.flatMap((m, i) => (sel.has(i) ? [{ ...m, start: m.start + shift, key: Math.min(127, Math.max(0, (m.key ?? 60) + dk)) }] : []));
        next = copy ? [...base, ...moved] : base.map((m, i) => (sel.has(i) ? moved[[...sel].sort((a, b) => a - b).indexOf(i)] : m));
      }
      setDraft(next);
    }, (_dx, _dy, _ev, moved) => {
      if (!moved) {
        setDraft(null);
        return;
      }
      if (edge === "right") lastLength.current = next[index].length;
      save(next, edge === "right" ? "Resize notes" : copy ? "Copy notes" : "Move notes");
      if (copy) setSelected(new Set(next.map((_, i) => i).slice(base.length)));
    }, edge === "right" ? "ew-resize" : copy ? "copy" : "grabbing");
  };

  const removeNote = (index: number) => {
    save(stored.filter((_, i) => i !== index), "Delete note");
    setSelected(new Set());
  };

  const gridBackground: React.CSSProperties = {
    width, height,
    backgroundImage: [
      "linear-gradient(to right, var(--color-grid-strong) 1px, transparent 1px)",
      "linear-gradient(to right, #222226 1px, transparent 1px)",
      step > 1 && step < ticksPerBeat(doc) && step * px >= 5 ? "linear-gradient(to right, #1e1e22 1px, transparent 1px)" : "linear-gradient(transparent, transparent)",
      `repeating-linear-gradient(to bottom, ${Array.from({ length: 12 }, (_, i) => {
        const key = 11 - i;
        const c = black.has(key) ? "#16161a" : "#1b1b1f";
        return `${c} ${i * keyH}px, ${c} ${(i + 1) * keyH - 1}px, #141417 ${(i + 1) * keyH - 1}px, #141417 ${(i + 1) * keyH}px`;
      }).join(", ")})`,
    ].join(", "),
    backgroundSize: `${bar * px}px 100%, ${ticksPerBeat(doc) * px}px 100%, ${Math.max(1, step) * px}px 100%, 100% ${12 * keyH}px`,
    backgroundPosition: `0 0, 0 0, 0 0, 0 ${(128 % 12) * keyH - 12 * keyH}px`,
  };

  return (
    <div ref={root} className="flex h-full flex-col">
      <PianoToolbar doc={doc} pattern={pattern} channelId={channelId} onQuantize={quantize} selection={selected.size} />
      <div className="flex min-h-0 flex-1">
        <div className="flex shrink-0 flex-col border-r border-line bg-panel" style={{ width: KEYS_W }}>
          <div className="relative min-h-0 flex-1 overflow-hidden" ref={keysRef}>
            <Keyboard keyH={keyH} scroller={scroller} channel={channelId} />
          </div>
          <div className="flex shrink-0 items-start justify-end border-t border-line p-1 text-[9px] text-faint" style={{ height: VELOCITY_H }}>Velocity</div>
        </div>
        <div className="flex min-w-0 flex-1 flex-col">
          <div
            ref={scroller}
            className="relative min-h-0 flex-1 overflow-auto"
            onScroll={(e) => {
              if (keysRef.current) keysRef.current.scrollTop = e.currentTarget.scrollTop;
              const vel = e.currentTarget.parentElement?.querySelector<HTMLDivElement>("[data-velocity]");
              if (vel) vel.scrollLeft = e.currentTarget.scrollLeft;
            }}
          >
            <div className="relative" style={gridBackground} onPointerDown={onGridDown}>
              <div className="pointer-events-none absolute top-0 bottom-0 bg-black/35" style={{ left: loop * px, right: 0 }} />
              <PatternPlayhead pattern={pattern.id} px={px} />
              {rubber && (
                <div className="pointer-events-none absolute z-20 border border-accent/80 bg-accent/10"
                  style={{ left: Math.min(rubber.x0, rubber.x1), top: Math.min(rubber.y0, rubber.y1), width: Math.abs(rubber.x1 - rubber.x0), height: Math.abs(rubber.y1 - rubber.y0) }} />
              )}
              {others.map((n, i) => (
                <div key={`o${i}`} className="pointer-events-none absolute rounded-[2px] bg-fg/10"
                  style={{ left: n.start * px, top: (127 - (n.key ?? 60)) * keyH, width: n.length * px, height: keyH - 1 }} />
              ))}
              {notes.map((n, i) => (
                <div
                  key={i}
                  className={cn("absolute rounded-[2px] text-[9px] leading-none text-black/70", selected.has(i) ? "ring-1 ring-fg" : "ring-1 ring-black/50")}
                  style={{ left: n.start * px, top: (127 - (n.key ?? 60)) * keyH, width: Math.max(3, n.length * px - 1), height: keyH - 1, background: color, opacity: 0.55 + ((n.velocity ?? 100) / 127) * 0.45 }}
                  onPointerDown={(e) => {
                    const x = e.clientX - e.currentTarget.getBoundingClientRect().left;
                    onNoteDown(e, i, x > n.length * px - 6 ? "right" : "body");
                  }}
                  onDoubleClick={(e) => {
                    e.stopPropagation();
                    removeNote(i);
                  }}
                  onContextMenu={(e) => {
                    e.preventDefault();
                    removeNote(i);
                  }}
                >
                  {keyH >= 11 && n.length * px > 24 && <span className="pl-0.5">{noteName(n.key ?? 60)}</span>}
                  <div className="absolute top-0 right-0 bottom-0 w-1.5 cursor-ew-resize" />
                </div>
              ))}
            </div>
          </div>
          <VelocityLane notes={notes} px={px} width={width} color={color} selected={selected}
            onChange={(next) => save(next, "Velocity")} onPreview={setDraft} />
        </div>
      </div>
    </div>
  );
}

/** The keys play the channel while held. */
function Keyboard({ keyH, scroller, channel }: { keyH: number; scroller: React.RefObject<HTMLDivElement | null>; channel: string }) {
  const keys = Array.from({ length: 128 }, (_, i) => 127 - i);
  const play = (e: React.PointerEvent, key: number) => {
    if (e.button !== 0) return;
    void engine.call("live.noteOn", { channel, key, velocity: 100 }).catch(() => {});
    startDrag(e, () => {}, () => void engine.call("live.noteOff", { key }).catch(() => {}));
  };
  return (
    <div style={{ height: 128 * keyH }} onWheel={(e) => scroller.current && (scroller.current.scrollTop += e.deltaY)}>
      {keys.map((k) => (
        <div key={k} onPointerDown={(e) => play(e, k)}
          className={cn("flex cursor-pointer items-center justify-end border-b border-black/40 pr-1 text-[9px] active:brightness-75", black.has(k % 12) ? "bg-[#202024] text-faint" : "bg-[#cfcfd4] text-[#3a3a40]")} style={{ height: keyH }}>
          {k % 12 === 0 && keyH >= 9 ? noteName(k) : ""}
        </div>
      ))}
    </div>
  );
}

function VelocityLane({ notes, px, width, color, selected, onChange, onPreview }: {
  notes: Note[]; px: number; width: number; color: string; selected: Set<number>;
  onChange: (notes: Note[]) => void; onPreview: (notes: Note[] | null) => void;
}) {
  return (
    <div data-velocity className="shrink-0 overflow-hidden border-t border-line bg-panel" style={{ height: VELOCITY_H }}>
      <div className="relative" style={{ width, height: VELOCITY_H }}
        onPointerDown={(e) => {
          const el = e.currentTarget;
          const apply = (ev: { clientX: number; clientY: number }) => {
            const r = el.getBoundingClientRect();
            const t = (ev.clientX - r.left) / px;
            const v = Math.min(127, Math.max(1, Math.round((1 - (ev.clientY - r.top - 4) / (VELOCITY_H - 8)) * 127)));
            // The notes under the pointer; with notes selected, only those.
            return notes.map((n, i) => {
              const under = t >= n.start - 4 / px && t <= n.start + Math.max(n.length, 8 / px);
              return under && (selected.size === 0 || selected.has(i)) ? { ...n, velocity: v } : n;
            });
          };
          let next = apply(e);
          onPreview(next);
          startDrag(e, (_dx, _dy, ev) => {
            next = apply(ev);
            onPreview(next);
          }, () => onChange(next));
        }}>
        {notes.map((n, i) => {
          const h = ((n.velocity ?? 100) / 127) * (VELOCITY_H - 8);
          return (
            <div key={i} className="absolute bottom-1" style={{ left: n.start * px, width: 3, height: h, background: selected.has(i) ? "#fff" : color }}>
              <div className="absolute -top-1 -left-[2px] h-[7px] w-[7px] rounded-full" style={{ background: selected.has(i) ? "#fff" : color }} />
            </div>
          );
        })}
      </div>
    </div>
  );
}

function PianoToolbar({ doc, pattern, channelId, onQuantize, selection }: { doc: Project; pattern: Pattern; channelId: string; onQuantize: () => void; selection: number }) {
  const { pianoZoom, pianoKeyHeight, pianoSnap } = useUi();
  const grid = GRIDS.find((g) => g.ticks === pianoSnap) ?? GRIDS[3];
  const channel = doc.channels.find((c) => c.id === channelId)!;

  return (
    <div className="flex h-8 shrink-0 items-center gap-2 border-b border-line px-2">
      <Dropdown>
        <DropdownTrigger asChild>
          <Button size="sm"><span className="text-faint">Pattern</span> <span className="text-fg">{pattern.name ?? pattern.id}</span> <ChevronDown size={11} /></Button>
        </DropdownTrigger>
        <DropdownContent>
          {doc.patterns.map((p) => (
            <DropdownItem key={p.id} checked={p.id === pattern.id} onSelect={() => setUi({ selectedPattern: p.id })}>{p.name ?? p.id}</DropdownItem>
          ))}
          <DropdownSeparator />
          <DropdownItem onSelect={() => void newPattern(doc)}>New pattern</DropdownItem>
        </DropdownContent>
      </Dropdown>
      <Dropdown>
        <DropdownTrigger asChild>
          <Button size="sm">
            <span className="h-2.5 w-2.5 rounded-[2px]" style={{ background: colorFor(channel.id, channel.color) }} />
            <span className="text-fg">{channel.name ?? channel.id}</span> <ChevronDown size={11} />
          </Button>
        </DropdownTrigger>
        <DropdownContent>
          {doc.channels.map((c) => (
            <DropdownItem key={c.id} checked={c.id === channelId} onSelect={() => setUi({ selectedChannel: c.id })}>{c.name ?? c.id}</DropdownItem>
          ))}
        </DropdownContent>
      </Dropdown>
      <Dropdown>
        <DropdownTrigger asChild>
          <Button size="sm" title="The grid notes are drawn, moved and resized on; hold ⌘ while dragging to place them freely">
            <span className="text-faint">Grid</span> <span className="text-fg">{grid.label}</span> <ChevronDown size={11} />
          </Button>
        </DropdownTrigger>
        <DropdownContent>
          {GRIDS.map((g) => (
            <DropdownItem key={g.label} checked={g.ticks === grid.ticks} onSelect={() => setUi({ pianoSnap: g.ticks })}>{g.label}</DropdownItem>
          ))}
        </DropdownContent>
      </Dropdown>
      <Button size="sm" title="Snap note starts to the grid (Q)" onClick={onQuantize}>Quantize{selection ? ` ${selection}` : ""}</Button>
      <div className="truncate text-2xs text-faint">Click to draw · Shift-drag selects · Alt-drag copies · ⌘ while dragging ignores the grid · ↑↓ transpose · ⌘C ⌘V ⌘D · double-click deletes</div>
      <div className="flex-1" />
      <div className="flex items-center">
        <IconButton size="sm" title="Shorter keys" onClick={() => setUi({ pianoKeyHeight: Math.max(6, pianoKeyHeight - 2) })}><Minus size={12} /></IconButton>
        <span className="text-2xs text-faint">Keys</span>
        <IconButton size="sm" title="Taller keys" onClick={() => setUi({ pianoKeyHeight: Math.min(24, pianoKeyHeight + 2) })}><Plus size={12} /></IconButton>
      </div>
      <div className="flex items-center">
        <IconButton size="sm" title="Zoom out" onClick={() => setUi({ pianoZoom: Math.max(20, pianoZoom / 1.3) })}><Minus size={12} /></IconButton>
        <span className="text-2xs text-faint">Zoom</span>
        <IconButton size="sm" title="Zoom in" onClick={() => setUi({ pianoZoom: Math.min(600, pianoZoom * 1.3) })}><Plus size={12} /></IconButton>
      </div>
    </div>
  );
}
