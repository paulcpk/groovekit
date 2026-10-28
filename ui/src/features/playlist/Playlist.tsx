import type { AudioClip, Channel, Clip, PatternClip, Project } from "@groovekit/schema/project";
import { ChevronDown, Minus, Plus, ScrollText } from "lucide-react";
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { Button, IconButton } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { floorTo, snapTo, startDrag } from "@/lib/drag";
import { fitSpeed } from "@/lib/fit";
import { audioSource, baseName, colorFor, rowCount, patternLength, snapTicks, songEnd, ticksPerBar, ticksPerBeat, uniqueId } from "@/lib/model";
import { getPeaks } from "@/lib/peaks";
import { execute, reportError, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { ClipView } from "./ClipView";
import { HEADER_W, RULER_H, droppedSample, isSampleDrag, type DraggedSample, type Geometry } from "./geometry";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { Playhead } from "./Playhead";
import { Ruler } from "./Ruler";
import { RowHeader } from "./RowHeader";

interface DragState {
  mode: "move" | "copy" | "resize" | "trim";
  ids: string[];
  dTicks: number;
  dRows: number;
}

interface Rubber {
  x0: number; y0: number; x1: number; y1: number;
}

/** Rows selected by their headers (a run, from the first to the last), and a drag moving them. */
interface RowRange {
  from: number;
  to: number;
}

export function Playlist() {
  const doc = useEngine((s) => s.doc)!;
  const { playlistZoom, rowHeight, snap, selectedClips } = useUi();
  const scroller = useRef<HTMLDivElement>(null);
  const content = useRef<HTMLDivElement>(null);
  const [viewWidth, setViewWidth] = useState(1200);
  const [drag, setDrag] = useState<DragState | null>(null);
  const [rubber, setRubber] = useState<Rubber | null>(null);
  const [rows, setRows] = useState<RowRange | null>(null);
  const [rowDrag, setRowDrag] = useState<number | null>(null);
  const rowAnchor = useRef(0);
  const clipAnchor = useRef<string | null>(null);

  const g: Geometry = {
    pxPerTick: playlistZoom / 960,
    rowHeight,
    snap: snapTicks(snap, doc),
    bar: ticksPerBar(doc),
    beat: ticksPerBeat(doc),
  };

  // The timeline also grows with the playhead, in steps of 8 bars so it doesn't redraw while playing.
  const playedTo = useEngine((s) => Math.floor(s.transport.position / (8 * g.bar)) + 1) * 8 * g.bar;
  const endTicks = Math.max(songEnd(doc) + 16 * g.bar, playedTo + 8 * g.bar, Math.ceil(viewWidth / g.pxPerTick / g.bar + 4) * g.bar);
  const width = endTicks * g.pxPerTick;
  // Rows are numbered: every one in use, and spare ones below to drop onto.
  const count = rowCount(doc) + (drag?.dRows && drag.dRows > 0 ? drag.dRows : 0);

  useLayoutEffect(() => {
    const el = scroller.current;
    if (!el) return;
    const observer = new ResizeObserver(() => setViewWidth(el.clientWidth - HEADER_W));
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  useZoomWheel(scroller, g.pxPerTick);

  // What each lane shows, with the drag in progress applied.
  const lanes = useMemo(() => {
    const byRow: { clip: Clip; start: number; length: number; ghost?: boolean }[][] = Array.from({ length: count }, () => []);
    const clampRow = (i: number) => Math.min(count - 1, Math.max(0, i));

    for (const clip of doc.playlist.clips) {
      const index = clampRow(clip.row);
      const dragged = drag?.ids.includes(clip.id) ? drag : null;

      if (!dragged) {
        byRow[index].push({ clip, start: clip.start, length: clip.length });
      } else if (dragged.mode === "move" || dragged.mode === "copy") {
        if (dragged.mode === "copy") byRow[index].push({ clip, start: clip.start, length: clip.length });
        byRow[clampRow(index + dragged.dRows)].push({ clip, start: clip.start + dragged.dTicks, length: clip.length, ghost: dragged.mode === "copy" });
      } else if (dragged.mode === "resize") {
        byRow[index].push({ clip, start: clip.start, length: clip.length + dragged.dTicks });
      } else {
        byRow[index].push({ clip, start: clip.start + dragged.dTicks, length: clip.length - dragged.dTicks });
      }
    }
    return byRow;
  }, [doc.playlist.clips, count, drag]);

  // Pressing a row's header selects it (Shift: the rows from the last one pressed); dragging moves
  // the selected rows, with their clips, names, mute and solo, so nothing sounds different.
  const beginRowDrag = (e: React.PointerEvent, index: number) => {
    let range: RowRange;
    if (e.shiftKey && rows) range = { from: Math.min(rowAnchor.current, index), to: Math.max(rowAnchor.current, index) };
    else if (rows && index >= rows.from && index <= rows.to) range = rows;
    else {
      range = { from: index, to: index };
      rowAnchor.current = index;
    }
    setRows(range);
    setUi({ selectedClips: [] });
    let dRows = 0;

    startDrag(e, (_dx, dy) => {
      dRows = Math.max(-range.from, Math.round(dy / g.rowHeight));
      setRowDrag(dRows);
    }, (_dx, _dy, _ev, moved) => {
      setRowDrag(null);
      if (!moved || dRows === 0) return;
      const count = range.to - range.from + 1;
      void execute({ type: "moveRows", row: range.from, count, to: range.from + dRows },
        { label: count === 1 ? "Move row" : `Move ${count} rows` });
      setRows({ from: range.from + dRows, to: range.to + dRows });
      rowAnchor.current += dRows;
    }, "grabbing");
  };

  const beginClipDrag = (e: React.PointerEvent, clip: Clip, edge: "left" | "right" | "body") => {
    e.stopPropagation();
    setRows(null);
    const current = useUi.getState().selectedClips;
    // Shift: everything in the row from the clip pressed last to this one; across rows, just this one.
    const anchor = e.shiftKey ? doc.playlist.clips.find((c) => c.id === clipAnchor.current) : undefined;
    let selection: string[];
    if (anchor && anchor.row === clip.row && anchor.id !== clip.id) {
      const [a, b] = [Math.min(anchor.start, clip.start), Math.max(anchor.start, clip.start)];
      const between = doc.playlist.clips.filter((c) => c.row === clip.row && c.start >= a && c.start <= b).map((c) => c.id);
      selection = [...new Set([...current, ...between])];
    } else {
      selection = current.includes(clip.id) ? current : e.shiftKey ? [...current, clip.id] : [clip.id];
    }
    if (!e.shiftKey || !anchor) clipAnchor.current = clip.id;
    setUi({ selectedClips: selection });

    const mode: DragState["mode"] = edge === "right" ? "resize" : edge === "left" ? "trim" : e.altKey ? "copy" : "move";
    const ids = edge === "body" ? selection : [clip.id];
    const moving = doc.playlist.clips.filter((c) => ids.includes(c.id));
    const earliest = Math.min(...moving.map((c) => c.start));
    const topmost = Math.min(...moving.map((c) => c.row));
    const minStep = Math.min(g.snap, g.beat);
    let state: DragState = { mode, ids, dTicks: 0, dRows: 0 };

    startDrag(e, (dx, dy) => {
      const t = dx / g.pxPerTick;
      let dTicks = 0;

      if (mode === "move" || mode === "copy") {
        dTicks = Math.max(-earliest, snapTo(clip.start + t, g.snap) - clip.start);
      } else if (mode === "resize") {
        dTicks = Math.max(minStep - clip.length, snapTo(clip.start + clip.length + t, g.snap) - clip.start - clip.length);
      } else {
        dTicks = Math.min(clip.length - minStep, Math.max(-clip.start, snapTo(clip.start + t, g.snap) - clip.start));
        if (clip.type === "audio") dTicks = Math.max(dTicks, -secondsToTicks((clip.offset ?? 0), clip, doc));
      }

      state = { mode, ids, dTicks, dRows: mode === "move" || mode === "copy" ? Math.max(-topmost, Math.round(dy / g.rowHeight)) : 0 };
      setDrag(state);
    }, (_dx, _dy, _ev, moved) => {
      setDrag(null);
      if (moved) void commitDrag(state, doc);
    }, mode === "resize" || mode === "trim" ? "ew-resize" : mode === "copy" ? "copy" : "grabbing");
  };

  const beginEmptyDrag = (e: React.PointerEvent, laneIndex: number) => {
    // Only presses on the lane itself, not ones bubbling up from a menu or dialog it owns.
    if (e.button !== 0 || !e.currentTarget.contains(e.target as Node)) return;
    setRows(null);
    const box = content.current!.getBoundingClientRect();
    const x0 = e.clientX - box.left - HEADER_W;
    const y0 = e.clientY - box.top - RULER_H;
    let r: Rubber = { x0, y0, x1: x0, y1: y0 };

    startDrag(e, (_dx, _dy, ev) => {
      r = { x0, y0, x1: ev.clientX - box.left - HEADER_W, y1: ev.clientY - box.top - RULER_H };
      setRubber(r);
    }, (_dx, _dy, ev, moved) => {
      setRubber(null);

      if (!moved) {
        placePattern(doc, laneIndex, floorTo(Math.max(0, x0) / g.pxPerTick, g.snap));
        return;
      }

      const [xa, xb] = [Math.min(r.x0, r.x1), Math.max(r.x0, r.x1)];
      const [ya, yb] = [Math.min(r.y0, r.y1), Math.max(r.y0, r.y1)];
      const hits = doc.playlist.clips.filter((c) => {
        const top = c.row * g.rowHeight;
        return c.start * g.pxPerTick < xb && (c.start + c.length) * g.pxPerTick > xa && top < yb && top + g.rowHeight > ya;
      }).map((c) => c.id);
      setUi({ selectedClips: ev.shiftKey ? [...new Set([...useUi.getState().selectedClips, ...hits])] : hits });
    }, "crosshair");
  };

  const gridStyle: React.CSSProperties = {
    width,
    height: g.rowHeight,
    backgroundImage: [
      "linear-gradient(to right, var(--color-grid-strong) 1px, transparent 1px)",
      g.beat * g.pxPerTick >= 6 ? "linear-gradient(to right, var(--color-grid) 1px, transparent 1px)" : "",
    ].filter(Boolean).join(", "),
    backgroundSize: `${g.bar * g.pxPerTick}px 100%, ${g.beat * g.pxPerTick}px 100%`,
  };

  return (
    <div className="flex h-full flex-col bg-bg">
      <PlaylistToolbar doc={doc} />
      <div ref={scroller} className="relative min-h-0 flex-1 overflow-auto">
        <div ref={content} className="relative" style={{ width: HEADER_W + width, minHeight: "100%" }}>
          <div className="sticky top-0 z-20 flex" style={{ height: RULER_H }}>
            <div className="sticky left-0 z-10 flex shrink-0 items-end border-r border-b border-line bg-panel px-2 pb-1 text-2xs text-faint" style={{ width: HEADER_W }}>
              Playlist
            </div>
            <Ruler g={g} endTicks={endTicks} />
          </div>

          {lanes.map((laneClips, i) => (
            <div key={i} className="flex">
              <RowHeader row={doc.playlist.rows[i] ?? {}} index={i} doc={doc} height={g.rowHeight}
                selected={!!rows && i >= rows.from && i <= rows.to} onPress={beginRowDrag} />
              <div
                className="relative border-b border-grid-strong"
                style={gridStyle}
                onPointerDown={(e) => beginEmptyDrag(e, i)}
                onDragOver={(e) => {
                  if (isSampleDrag(e) || isFileDrag(e)) e.preventDefault();
                }}
                onDrop={(e) => {
                  const x = e.clientX - e.currentTarget.getBoundingClientRect().left;
                  const at = floorTo(x / g.pxPerTick, g.snap);
                  if (isFileDrag(e)) {
                    e.preventDefault();
                    // Several files play one after another from where they're dropped.
                    void importDroppedFiles(e).then(async (samples) => {
                      let start = at;
                      for (const sample of samples) {
                        const clip = await dropSample(useEngine.getState().doc ?? doc, sample, i, start);
                        if (clip) start = clip.start + clip.length;
                      }
                    });
                    return;
                  }
                  const sample = droppedSample(e);
                  if (!sample) return;
                  e.preventDefault();
                  void dropSample(doc, sample, i, at);
                }}
              >
                {laneClips.map(({ clip, start, length, ghost }) => (
                  <ClipView
                    key={clip.id + (ghost ? "-ghost" : "")}
                    clip={clip} doc={doc} ghost={ghost}
                    left={start * g.pxPerTick} width={length * g.pxPerTick} height={g.rowHeight - 2}
                    selected={selectedClips.includes(clip.id)}
                    pxPerTick={g.pxPerTick} snap={g.snap}
                    onPointerDown={beginClipDrag}
                  />
                ))}
              </div>
            </div>
          ))}

          {/* Room below the last row, so it never sits on the window's edge. */}
          <div className="flex" style={{ height: 3 * g.rowHeight }}>
            <div className="sticky left-0 z-10 shrink-0 border-r border-line bg-panel" style={{ width: HEADER_W }} />
          </div>

          {rows && rowDrag !== null && (
            <div
              className="pointer-events-none absolute z-30 rounded-[3px] border-2 border-accent bg-accent/10"
              style={{ left: 0, right: 0, top: RULER_H + (rows.from + rowDrag) * g.rowHeight, height: (rows.to - rows.from + 1) * g.rowHeight }}
            />
          )}

          {rubber && (
            <div
              className="pointer-events-none absolute z-30 border border-fg/60 bg-fg/10"
              style={{
                left: HEADER_W + Math.min(rubber.x0, rubber.x1), top: RULER_H + Math.min(rubber.y0, rubber.y1),
                width: Math.abs(rubber.x1 - rubber.x0), height: Math.abs(rubber.y1 - rubber.y0),
              }}
            />
          )}
          <Playhead pxPerTick={g.pxPerTick} scroller={scroller} />
        </div>
      </div>
    </div>
  );
}

//==============================================================================
function PlaylistToolbar({ doc }: { doc: Project }) {
  const { selectedPattern, playlistZoom, rowHeight, followPlayhead } = useUi();
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);

  return (
    <div className="flex h-8 shrink-0 items-center gap-2 border-b border-line bg-panel px-2">
      <Dropdown>
        <DropdownTrigger asChild>
          <Button size="sm" title="The pattern a click in the playlist places">
            <span className="h-2.5 w-2.5 rounded-[2px]" style={{ background: pattern ? colorFor(pattern.id, pattern.color) : "#444" }} />
            <span className="max-w-[160px] truncate text-fg">{pattern ? pattern.name ?? pattern.id : "No pattern"}</span>
            <ChevronDown size={11} />
          </Button>
        </DropdownTrigger>
        <DropdownContent>
          {doc.patterns.map((p) => (
            <DropdownItem key={p.id} checked={p.id === selectedPattern} onSelect={() => setUi({ selectedPattern: p.id })}>
              {p.name ?? p.id}
            </DropdownItem>
          ))}
          {doc.patterns.length > 0 && <DropdownSeparator />}
          <DropdownItem onSelect={() => void newPattern(doc)}>New pattern</DropdownItem>
        </DropdownContent>
      </Dropdown>
      <div className="text-2xs text-faint">Click a lane to place it · drag to select · Alt-drag to copy</div>
      <div className="flex-1" />
      <IconButton size="sm" title="Follow the playhead" active={followPlayhead} onClick={() => setUi({ followPlayhead: !followPlayhead })}>
        <ScrollText size={13} />
      </IconButton>
      <Button size="sm" title="Row height" onClick={() => setUi({ rowHeight: rowHeight >= 72 ? 32 : rowHeight + 20 })}>
        {rowHeight <= 32 ? "S" : rowHeight <= 52 ? "M" : "L"}
      </Button>
      <div className="flex items-center">
        <IconButton size="sm" title="Zoom out (⌘ scroll)" onClick={() => setUi({ playlistZoom: Math.max(2, playlistZoom / 1.4) })}><Minus size={12} /></IconButton>
        <IconButton size="sm" title="Zoom in (⌘ scroll)" onClick={() => setUi({ playlistZoom: Math.min(400, playlistZoom * 1.4) })}><Plus size={12} /></IconButton>
      </div>
    </div>
  );
}

/** ⌘/Ctrl + wheel zooms around the pointer. */
function useZoomWheel(scroller: React.RefObject<HTMLDivElement | null>, pxPerTick: number) {
  const current = useRef(pxPerTick);
  current.current = pxPerTick;

  useEffect(() => {
    const el = scroller.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      if (!e.metaKey && !e.ctrlKey) return;
      e.preventDefault();
      const x = e.clientX - el.getBoundingClientRect().left - HEADER_W + el.scrollLeft;
      const tick = x / current.current;
      const zoom = Math.min(400, Math.max(2, useUi.getState().playlistZoom * Math.exp(-e.deltaY * 0.004)));
      setUi({ playlistZoom: zoom });
      requestAnimationFrame(() => (el.scrollLeft = tick * (zoom / 960) - (x - el.scrollLeft)));
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, [scroller]);
}

//==============================================================================
function secondsToTicks(seconds: number, clip: AudioClip, doc: Project) {
  return (seconds * (audioSource(doc, clip).sourceTempo ?? doc.tempo) * 960) / 60;
}

async function commitDrag(state: DragState, doc: Project) {
  const clips = doc.playlist.clips.filter((c) => state.ids.includes(c.id));
  const n = clips.length;
  const what = n === 1 ? "clip" : `${n} clips`;
  const rowAfter = (c: Clip) => Math.min(999, Math.max(0, c.row + state.dRows));

  if (state.mode === "move") {
    if (state.dTicks === 0 && state.dRows === 0) return;
    await execute(clips.map((c) => ({
      type: "updateClip" as const, clip: c.id,
      changes: { start: c.start + state.dTicks, ...(state.dRows ? { row: rowAfter(c) } : {}) },
    })), { label: `Move ${what}` });
  } else if (state.mode === "copy") {
    const taken = new Set(doc.playlist.clips.map((c) => c.id));
    const copies = clips.map((c) => {
      const id = uniqueId(c.id.replace(/-\d+$/, ""), taken);
      taken.add(id);
      return { ...c, id, start: c.start + state.dTicks, row: rowAfter(c) };
    });
    await execute(copies.map((clip) => ({ type: "addClip" as const, clip })), { label: `Copy ${what}` });
    setUi({ selectedClips: copies.map((c) => c.id) });
  } else if (state.mode === "resize") {
    await execute(clips.map((c) => ({ type: "updateClip" as const, clip: c.id, changes: { length: c.length + state.dTicks } })), { label: `Resize ${what}` });
  } else {
    await execute(clips.map((c) => {
      const changes: Record<string, unknown> = { start: c.start + state.dTicks, length: c.length - state.dTicks };
      if (c.type === "audio") {
        changes.offset = Math.max(0, (c.offset ?? 0) + (state.dTicks * 60) / ((audioSource(doc, c).sourceTempo ?? doc.tempo) * 960));
      } else if (c.type === "pattern") {
        const pattern = doc.patterns.find((p) => p.id === c.pattern);
        const loop = pattern ? patternLength(pattern, doc) : 1;
        changes.offset = ((((c.offset ?? 0) + state.dTicks) % loop) + loop) % loop;
      }
      return { type: "updateClip" as const, clip: c.id, changes };
    }), { label: `Trim ${what}` });
  }
}

function placePattern(doc: Project, row: number, start: number) {
  const { selectedPattern } = useUi.getState();
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);
  setUi({ selectedClips: [] });

  if (!pattern) {
    toast("Make a pattern in the channel rack first");
    return;
  }

  const clip: PatternClip = { type: "pattern", id: uniqueId(pattern.id, doc.playlist.clips), row, start, length: patternLength(pattern, doc), pattern: pattern.id };
  void execute({ type: "addClip", clip }, { label: `Place ${pattern.name ?? pattern.id}` });
}

/** A sample dropped onto the playlist becomes a clip of a sampler channel listed with the
    arrangement's: one that already plays the whole file as it is, or a new one. */
async function dropSample(doc: Project, sample: DraggedSample, row: number, start: number) {
  try {
    const duration = sample.durationSeconds ?? (await getPeaks(sample.path)).durationSeconds;
    // Loops fit the song as set (by their own tempo, so they fill as many bars as they're long).
    const fit = useUi.getState().loopDrop;
    const loopTempo = sample.loop && sample.tempo && fit !== "off" ? sample.tempo : undefined;
    const resample = !!loopTempo && fit === "resample";
    const length = Math.max(1, Math.round((duration * (doc.tempo / fitSpeed(doc, loopTempo)) * 960) / 60));
    const existing = doc.channels.find((c) => {
      const layer = c.instrument.type === "sampler" && c.group === "arrangement" ? c.instrument.layers[0] : undefined;
      return layer && layer.sample === sample.path && !layer.reverse && !layer.start && layer.end === undefined
        && !layer.transpose && !layer.fineTune && layer.sourceTempo === loopTempo && !!layer.resample === resample;
    });
    const channel: Channel = existing ?? {
      id: uniqueId(baseName(sample.path).replace(/[^A-Za-z0-9_.-]+/g, "-").slice(0, 56) || "audio", doc.channels),
      name: baseName(sample.path),
      group: "arrangement",
      instrument: { type: "sampler", layers: [{ id: "sample", sample: sample.path, ...(loopTempo ? { sourceTempo: loopTempo } : {}), ...(resample ? { resample } : {}) }] },
    };
    const clip: AudioClip = { type: "audio", id: uniqueId(baseName(sample.path), doc.playlist.clips), row, start, length, channel: channel.id };
    await execute([...(existing ? [] : [{ type: "addChannel" as const, channel }]), { type: "addClip", clip }], { label: `Add ${baseName(sample.path)}` });
    setUi({ selectedClips: [clip.id] });
    return clip;
  } catch (e) {
    reportError(e);
    return undefined;
  }
}

export async function newPattern(doc: Project) {
  const id = uniqueId("pattern", doc.patterns);
  const n = doc.patterns.length + 1;
  await execute({ type: "addPattern", pattern: { id, name: `Pattern ${n}`, length: ticksPerBar(doc), notes: [] } }, { label: "New pattern" });
  setUi({ selectedPattern: id });
}
