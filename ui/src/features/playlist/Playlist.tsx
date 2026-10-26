import type { AudioClip, Channel, Clip, PatternClip, Project } from "@groovekit/schema/project";
import { ChevronDown, Minus, Plus, ScrollText } from "lucide-react";
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { Button, IconButton } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { floorTo, snapTo, startDrag } from "@/lib/drag";
import { audioSource, baseName, colorFor, patternLength, snapTicks, songEnd, ticksPerBar, ticksPerBeat, uniqueId } from "@/lib/model";
import { getPeaks } from "@/lib/peaks";
import { execute, reportError, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { ClipView } from "./ClipView";
import { HEADER_W, RULER_H, droppedSample, isSampleDrag, type DraggedSample, type Geometry } from "./geometry";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { Playhead } from "./Playhead";
import { Ruler } from "./Ruler";
import { TrackHeader } from "./TrackHeader";

interface DragState {
  mode: "move" | "copy" | "resize" | "trim";
  ids: string[];
  dTicks: number;
  dTracks: number;
}

interface Rubber {
  x0: number; y0: number; x1: number; y1: number;
}

export function Playlist() {
  const doc = useEngine((s) => s.doc)!;
  const { playlistZoom, trackHeight, snap, selectedClips } = useUi();
  const scroller = useRef<HTMLDivElement>(null);
  const content = useRef<HTMLDivElement>(null);
  const [viewWidth, setViewWidth] = useState(1200);
  const [drag, setDrag] = useState<DragState | null>(null);
  const [rubber, setRubber] = useState<Rubber | null>(null);

  const g: Geometry = {
    pxPerTick: playlistZoom / 960,
    trackHeight,
    snap: snapTicks(snap, doc),
    bar: ticksPerBar(doc),
    beat: ticksPerBeat(doc),
  };

  // The timeline also grows with the playhead, in steps of 8 bars so it doesn't redraw while playing.
  const playedTo = useEngine((s) => Math.floor(s.transport.position / (8 * g.bar)) + 1) * 8 * g.bar;
  const endTicks = Math.max(songEnd(doc) + 16 * g.bar, playedTo + 8 * g.bar, Math.ceil(viewWidth / g.pxPerTick / g.bar + 4) * g.bar);
  const width = endTicks * g.pxPerTick;
  const tracks = doc.playlist.tracks;
  const trackIndex = useMemo(() => new Map(tracks.map((t, i) => [t.id, i])), [tracks]);

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
    const byTrack: { clip: Clip; start: number; length: number; ghost?: boolean }[][] = tracks.map(() => []);
    const clampTrack = (i: number) => Math.min(tracks.length - 1, Math.max(0, i));

    for (const clip of doc.playlist.clips) {
      const index = trackIndex.get(clip.track);
      if (index === undefined) continue;
      const dragged = drag?.ids.includes(clip.id) ? drag : null;

      if (!dragged) {
        byTrack[index].push({ clip, start: clip.start, length: clip.length });
      } else if (dragged.mode === "move" || dragged.mode === "copy") {
        if (dragged.mode === "copy") byTrack[index].push({ clip, start: clip.start, length: clip.length });
        byTrack[clampTrack(index + dragged.dTracks)].push({ clip, start: clip.start + dragged.dTicks, length: clip.length, ghost: dragged.mode === "copy" });
      } else if (dragged.mode === "resize") {
        byTrack[index].push({ clip, start: clip.start, length: clip.length + dragged.dTicks });
      } else {
        byTrack[index].push({ clip, start: clip.start + dragged.dTicks, length: clip.length - dragged.dTicks });
      }
    }
    return byTrack;
  }, [doc.playlist.clips, tracks, trackIndex, drag]);

  const beginClipDrag = (e: React.PointerEvent, clip: Clip, edge: "left" | "right" | "body") => {
    e.stopPropagation();
    const current = useUi.getState().selectedClips;
    const selection = current.includes(clip.id) ? current : e.shiftKey ? [...current, clip.id] : [clip.id];
    setUi({ selectedClips: selection });

    const mode: DragState["mode"] = edge === "right" ? "resize" : edge === "left" ? "trim" : e.altKey ? "copy" : "move";
    const ids = edge === "body" ? selection : [clip.id];
    const moving = doc.playlist.clips.filter((c) => ids.includes(c.id));
    const earliest = Math.min(...moving.map((c) => c.start));
    const minStep = Math.min(g.snap, g.beat);
    let state: DragState = { mode, ids, dTicks: 0, dTracks: 0 };

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

      state = { mode, ids, dTicks, dTracks: mode === "move" || mode === "copy" ? Math.round(dy / g.trackHeight) : 0 };
      setDrag(state);
    }, (_dx, _dy, _ev, moved) => {
      setDrag(null);
      if (moved) void commitDrag(state, doc);
    }, mode === "resize" || mode === "trim" ? "ew-resize" : mode === "copy" ? "copy" : "grabbing");
  };

  const beginEmptyDrag = (e: React.PointerEvent, laneIndex: number) => {
    // Only presses on the lane itself, not ones bubbling up from a menu or dialog it owns.
    if (e.button !== 0 || !e.currentTarget.contains(e.target as Node)) return;
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
        placePattern(doc, tracks[laneIndex].id, floorTo(Math.max(0, x0) / g.pxPerTick, g.snap));
        return;
      }

      const [xa, xb] = [Math.min(r.x0, r.x1), Math.max(r.x0, r.x1)];
      const [ya, yb] = [Math.min(r.y0, r.y1), Math.max(r.y0, r.y1)];
      const hits = doc.playlist.clips.filter((c) => {
        const i = trackIndex.get(c.track) ?? -1;
        const top = i * g.trackHeight;
        return c.start * g.pxPerTick < xb && (c.start + c.length) * g.pxPerTick > xa && top < yb && top + g.trackHeight > ya;
      }).map((c) => c.id);
      setUi({ selectedClips: ev.shiftKey ? [...new Set([...useUi.getState().selectedClips, ...hits])] : hits });
    }, "crosshair");
  };

  const gridStyle: React.CSSProperties = {
    width,
    height: g.trackHeight,
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

          {tracks.map((track, i) => (
            <div key={track.id} className="flex">
              <TrackHeader track={track} index={i} doc={doc} height={g.trackHeight} />
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
                        const clip = await dropSample(useEngine.getState().doc ?? doc, sample, track.id, start);
                        if (clip) start = clip.start + clip.length;
                      }
                    });
                    return;
                  }
                  const sample = droppedSample(e);
                  if (!sample) return;
                  e.preventDefault();
                  void dropSample(doc, sample, track.id, at);
                }}
              >
                {lanes[i].map(({ clip, start, length, ghost }) => (
                  <ClipView
                    key={clip.id + (ghost ? "-ghost" : "")}
                    clip={clip} doc={doc} ghost={ghost}
                    left={start * g.pxPerTick} width={length * g.pxPerTick} height={g.trackHeight - 2}
                    selected={selectedClips.includes(clip.id)}
                    pxPerTick={g.pxPerTick} snap={g.snap}
                    onPointerDown={beginClipDrag}
                  />
                ))}
              </div>
            </div>
          ))}

          <div className="flex">
            <div className="sticky left-0 z-10 flex shrink-0 items-center border-r border-line bg-panel px-1" style={{ width: HEADER_W, height: 32 }}>
              <Button size="sm" onClick={() => addTrack(doc)}><Plus size={12} /> Add track</Button>
            </div>
          </div>

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
  const { selectedPattern, playlistZoom, trackHeight, followPlayhead } = useUi();
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
      <Button size="sm" title="Track height" onClick={() => setUi({ trackHeight: trackHeight >= 72 ? 32 : trackHeight + 20 })}>
        {trackHeight <= 32 ? "S" : trackHeight <= 52 ? "M" : "L"}
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
  const tracks = doc.playlist.tracks;
  const index = new Map(tracks.map((t, i) => [t.id, i]));
  const clips = doc.playlist.clips.filter((c) => state.ids.includes(c.id));
  const n = clips.length;
  const what = n === 1 ? "clip" : `${n} clips`;
  const trackAfter = (c: Clip) => tracks[Math.min(tracks.length - 1, Math.max(0, (index.get(c.track) ?? 0) + state.dTracks))].id;

  if (state.mode === "move") {
    if (state.dTicks === 0 && state.dTracks === 0) return;
    await execute(clips.map((c) => ({
      type: "updateClip" as const, clip: c.id,
      changes: { start: c.start + state.dTicks, ...(state.dTracks ? { track: trackAfter(c) } : {}) },
    })), { label: `Move ${what}` });
  } else if (state.mode === "copy") {
    const taken = new Set(doc.playlist.clips.map((c) => c.id));
    const copies = clips.map((c) => {
      const id = uniqueId(c.id.replace(/-\d+$/, ""), taken);
      taken.add(id);
      return { ...c, id, start: c.start + state.dTicks, track: trackAfter(c) };
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

function placePattern(doc: Project, track: string, start: number) {
  const { selectedPattern } = useUi.getState();
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);
  setUi({ selectedClips: [] });

  if (!pattern) {
    toast("Make a pattern in the channel rack first");
    return;
  }

  const clip: PatternClip = { type: "pattern", id: uniqueId(pattern.id, doc.playlist.clips), track, start, length: patternLength(pattern, doc), pattern: pattern.id };
  void execute({ type: "addClip", clip }, { label: `Place ${pattern.name ?? pattern.id}` });
}

async function dropSample(doc: Project, sample: DraggedSample, track: string, start: number) {
  try {
    const duration = sample.durationSeconds ?? (await getPeaks(sample.path)).durationSeconds;
    const loopTempo = sample.loop && sample.tempo ? sample.tempo : undefined;
    const length = Math.max(1, Math.round((duration * (loopTempo ?? doc.tempo) * 960) / 60));
    // The sample's audio channel: one already playing it the same way, or a new one.
    const existing = doc.channels.find((c) => c.instrument.type === "audio" && c.instrument.sample === sample.path
      && !c.instrument.reverse && c.instrument.sourceTempo === loopTempo);
    const channel: Channel = existing ?? {
      id: uniqueId(baseName(sample.path).replace(/[^A-Za-z0-9_.-]+/g, "-").slice(0, 56) || "audio", doc.channels),
      name: baseName(sample.path),
      instrument: { type: "audio", sample: sample.path, ...(loopTempo ? { sourceTempo: loopTempo } : {}) },
    };
    const clip: AudioClip = { type: "audio", id: uniqueId(baseName(sample.path), doc.playlist.clips), track, start, length, channel: channel.id };
    await execute([...(existing ? [] : [{ type: "addChannel" as const, channel }]), { type: "addClip", clip }], { label: `Add ${baseName(sample.path)}` });
    setUi({ selectedClips: [clip.id] });
    return clip;
  } catch (e) {
    reportError(e);
    return undefined;
  }
}

function addTrack(doc: Project) {
  const id = uniqueId("track", doc.playlist.tracks);
  void execute({ type: "addTrack", track: { id, name: `Track ${doc.playlist.tracks.length + 1}` } }, { label: "Add track" });
}

export async function newPattern(doc: Project) {
  const id = uniqueId("pattern", doc.patterns);
  const n = doc.patterns.length + 1;
  await execute({ type: "addPattern", pattern: { id, name: `Pattern ${n}`, length: ticksPerBar(doc), notes: [] } }, { label: "New pattern" });
  setUi({ selectedPattern: id });
}
