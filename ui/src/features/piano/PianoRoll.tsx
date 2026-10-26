import type { Note, Pattern, Project } from "@groovekit/schema/project";
import { ChevronDown, Minus, Plus } from "lucide-react";
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { Button, Empty, IconButton } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { newPattern } from "@/features/playlist/Playlist";
import { stepKey } from "@/features/rack/channels";
import { cn } from "@/lib/cn";
import { floorTo, snapTo, startDrag } from "@/lib/drag";
import { colorFor, noteName, patternLength, snapTicks, STEP, ticksPerBar, ticksPerBeat } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

const KEYS_W = 56;
const VELOCITY_H = 64;
const black = new Set([1, 3, 6, 8, 10]);

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

function Editor({ doc, pattern, channelId }: { doc: Project; pattern: Pattern; channelId: string }) {
  const { pianoZoom, pianoKeyHeight: keyH, snap } = useUi();
  const channel = doc.channels.find((c) => c.id === channelId)!;
  const color = colorFor(channel.id, channel.color);
  const px = pianoZoom / 960;
  const step = Math.min(snapTicks(snap, doc), ticksPerBeat(doc));
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

  const save = (next: Note[], label: string) => {
    setDraft(next);
    void execute({ type: "setNotes", pattern: pattern.id, channel: channelId, notes: next.map((n) => ({ ...n, channel: channelId })) }, { label })
      .finally(() => setDraft(null));
  };

  // Delete removes the selected notes while the piano roll is open.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.key !== "Delete" && e.key !== "Backspace") || selected.size === 0) return;
      if ((e.target as HTMLElement)?.tagName === "INPUT") return;
      e.preventDefault();
      e.stopImmediatePropagation();
      save(stored.filter((_, i) => !selected.has(i)), selected.size === 1 ? "Delete note" : `Delete ${selected.size} notes`);
      setSelected(new Set());
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  });

  const pointToNote = (e: { clientX: number; clientY: number }, el: HTMLElement) => {
    const r = el.getBoundingClientRect();
    return { tick: (e.clientX - r.left) / px, key: 127 - Math.floor((e.clientY - r.top) / keyH) };
  };

  const onGridDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    const el = e.currentTarget;
    const { tick, key } = pointToNote(e, el);
    const start = floorTo(tick, step);
    const created: Note = { channel: channelId, start, length: lastLength.current, key, velocity: 100 };
    const index = stored.length;
    setSelected(new Set([index]));
    setDraft([...stored, created]);

    startDrag(e, (_dx, _dy, ev) => {
      const t = pointToNote(ev, el).tick;
      const length = Math.max(step, snapTo(t - start, step));
      setDraft([...stored, { ...created, length }]);
    }, (_dx, _dy, ev, moved) => {
      // A click draws a note as long as the last one; a drag sets the length.
      const length = moved ? Math.max(step, snapTo(pointToNote(ev, el).tick - start, step)) : lastLength.current;
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

    startDrag(e, (_dx, _dy, ev) => {
      const at = pointToNote(ev, grid);
      if (edge === "right") {
        const n = base[index];
        const length = Math.max(step, snapTo(at.tick - n.start, step));
        const dl = length - n.length;
        next = base.map((m, i) => (sel.has(i) ? { ...m, length: Math.max(step, m.length + dl) } : m));
      } else {
        const n = base[index];
        const dt = snapTo(n.start + (at.tick - origin.tick), step) - n.start;
        const dk = at.key - origin.key;
        const minStart = Math.min(...[...sel].map((i) => base[i].start));
        const shift = Math.max(-minStart, dt);
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
      `repeating-linear-gradient(to bottom, ${Array.from({ length: 12 }, (_, i) => {
        const key = 11 - i;
        const c = black.has(key) ? "#16161a" : "#1b1b1f";
        return `${c} ${i * keyH}px, ${c} ${(i + 1) * keyH - 1}px, #141417 ${(i + 1) * keyH - 1}px, #141417 ${(i + 1) * keyH}px`;
      }).join(", ")})`,
    ].join(", "),
    backgroundSize: `${bar * px}px 100%, ${ticksPerBeat(doc) * px}px 100%, 100% ${12 * keyH}px`,
    backgroundPosition: `0 0, 0 0, 0 ${(128 % 12) * keyH - 12 * keyH}px`,
  };

  return (
    <div className="flex h-full flex-col">
      <PianoToolbar doc={doc} pattern={pattern} channelId={channelId} />
      <div className="flex min-h-0 flex-1">
        <div className="flex shrink-0 flex-col border-r border-line bg-panel" style={{ width: KEYS_W }}>
          <div className="relative min-h-0 flex-1 overflow-hidden" ref={keysRef}>
            <Keyboard keyH={keyH} scroller={scroller} />
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

function Keyboard({ keyH, scroller }: { keyH: number; scroller: React.RefObject<HTMLDivElement | null> }) {
  const keys = Array.from({ length: 128 }, (_, i) => 127 - i);
  return (
    <div style={{ height: 128 * keyH }} onWheel={(e) => scroller.current && (scroller.current.scrollTop += e.deltaY)}>
      {keys.map((k) => (
        <div key={k} className={cn("flex items-center justify-end border-b border-black/40 pr-1 text-[9px]", black.has(k % 12) ? "bg-[#202024] text-faint" : "bg-[#cfcfd4] text-[#3a3a40]")} style={{ height: keyH }}>
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

function PianoToolbar({ doc, pattern, channelId }: { doc: Project; pattern: Pattern; channelId: string }) {
  const { pianoZoom, pianoKeyHeight } = useUi();
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
      <div className="text-2xs text-faint">Click to draw · drag to move · edge to resize · Alt-drag copies · double-click deletes</div>
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
