import type { Clip } from "@groovekit/schema/project";
import { useMemo, useRef, useState } from "react";
import { useShallow } from "zustand/react/shallow";
import { ContextMenu, ContextMenuTrigger } from "@/components/menu";
import { floorTo } from "@/lib/drag";
import { rowCount, snapTicks, songEnd, ticksPerBar, ticksPerBeat } from "@/lib/model";
import { useStable } from "@/lib/useStable";
import { useEngine } from "@/state/engine";
import { useUi } from "@/state/ui";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { ClipMenu, ClipView, clipColor, clipName } from "./ClipView";
import { dropSample } from "./edits";
import { HEADER_W, RULER_H, droppedSample, isSampleDrag, type Geometry } from "./geometry";
import { GridLines } from "./GridLines";
import { Playhead } from "./Playhead";
import { PlaylistToolbar } from "./PlaylistToolbar";
import { RowHeader } from "./RowHeader";
import { Ruler } from "./Ruler";
import { useClipDrag } from "./useClipDrag";
import { usePlaylistKeys } from "./usePlaylistKeys";
import { useRowDrag } from "./useRowDrag";
import { useRubberBand } from "./useRubberBand";
import { CHUNK, useViewport } from "./useViewport";
import { useZoomWheel } from "./useZoomWheel";

/** A row the song hasn't set up yet. */
const EMPTY_ROW = {};

/** The arrangement: numbered rows of pattern, audio and automation clips over a bar ruler. Only
    the rows and clips scrolled into view (and a margin around them) are in the DOM. */
export function Playlist() {
  const doc = useEngine((s) => s.doc)!;
  const { playlistZoom, rowHeight, snap, selectedClips } = useUi(useShallow((s) =>
    ({ playlistZoom: s.playlistZoom, rowHeight: s.rowHeight, snap: s.snap, selectedClips: s.selectedClips })));
  const root = useRef<HTMLDivElement>(null);
  const scroller = useRef<HTMLDivElement>(null);
  const content = useRef<HTMLDivElement>(null);
  usePlaylistKeys(root);
  // The clip last right-clicked, for the one context menu all the clips share.
  const [menu, setMenu] = useState<{ clipId: string; at: number } | null>(null);

  // The same object while nothing in it changes, so what takes it isn't drawn again.
  const bar = ticksPerBar(doc), beat = ticksPerBeat(doc), snapStep = snapTicks(snap, doc);
  const g: Geometry = useMemo(() => ({ pxPerTick: playlistZoom / 960, rowHeight, snap: snapStep, bar, beat }), [playlistZoom, rowHeight, snapStep, bar, beat]);

  const view = useViewport(scroller);
  useZoomWheel(scroller, g.pxPerTick);
  const { rows, rowDrag, beginRowDrag, clearRows } = useRowDrag(g);
  const { drag, beginClipDrag } = useClipDrag(doc, g, clearRows);
  const { rubber, beginEmptyDrag } = useRubberBand(doc, g, content, clearRows);
  const openClipMenu = useStable((clip: Clip, at: number) => setMenu({ clipId: clip.id, at }));

  // The timeline also grows with the playhead, in steps of 8 bars so it doesn't redraw while playing.
  const playedTo = useEngine((s) => Math.floor(s.transport.position / (8 * g.bar)) + 1) * 8 * g.bar;
  const endTicks = Math.max(songEnd(doc) + 16 * g.bar, playedTo + 8 * g.bar, Math.ceil(view.width / g.pxPerTick / g.bar + 4) * g.bar);
  const width = endTicks * g.pxPerTick;
  // Rows are numbered: every one in use, and spare ones below to drop onto.
  const count = rowCount(doc) + (drag?.dRows && drag.dRows > 0 ? drag.dRows : 0);

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

  const clipsInRow = useMemo(() => {
    const counts: number[] = [];
    for (const clip of doc.playlist.clips) counts[clip.row] = (counts[clip.row] ?? 0) + 1;
    return counts;
  }, [doc.playlist.clips]);

  // The rows and the stretch of time in view, with a chunk around them; a clip being dragged is
  // always drawn, wherever it's taken.
  const firstRow = Math.max(0, Math.floor((view.top - RULER_H) / g.rowHeight));
  const lastRow = Math.min(count - 1, Math.ceil((view.top + view.height + CHUNK) / g.rowHeight));
  const fromTick = view.left / g.pxPerTick;
  const toTick = (view.left + view.width + CHUNK) / g.pxPerTick;
  const inView = (start: number, length: number, clip: Clip) => (start + length >= fromTick && start <= toTick) || !!drag?.ids.includes(clip.id);

  const gridStyle: React.CSSProperties = { width, height: g.rowHeight };

  return (
    <div ref={root} className="flex h-full flex-col bg-bg" data-tour="playlist">
      <PlaylistToolbar doc={doc} />
      <div ref={scroller} className="relative min-h-0 flex-1 overflow-auto">
        {/* One context menu serves every clip: a right-click on a clip names it (openClipMenu)
            and the menu opens; anywhere else, nothing opens. */}
        <ContextMenu>
        <ContextMenuTrigger asChild>
        <div ref={content} className="relative" style={{ width: HEADER_W + width, minHeight: "100%" }}
          onContextMenu={(e) => {
            if (!(e.target as Element).closest("[data-clip]")) e.preventDefault();
          }}>
          <div className="sticky top-0 z-20 flex" style={{ height: RULER_H }}>
            <div className="sticky left-0 z-10 flex shrink-0 items-end border-r border-b border-line bg-panel px-2 pb-1 text-2xs text-faint" style={{ width: HEADER_W }}>
              Playlist
            </div>
            <Ruler g={g} endTicks={endTicks} />
          </div>

          <GridLines g={g} width={width} height={count * g.rowHeight} left={HEADER_W} top={RULER_H} />
          {firstRow > 0 && <Spacer rows={firstRow} rowHeight={g.rowHeight} />}
          {lanes.slice(firstRow, lastRow + 1).map((laneClips, k) => {
            const i = firstRow + k;
            return (
              <div key={i} className="flex">
                <RowHeader row={doc.playlist.rows[i] ?? EMPTY_ROW} index={i} clips={clipsInRow[i] ?? 0} height={g.rowHeight}
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
                  {laneClips.map(({ clip, start, length, ghost }) => inView(start, length, clip) && (
                    <ClipView
                      key={clip.id + (ghost ? "-ghost" : "")}
                      clip={clip} color={clipColor(clip, doc)} name={clipName(clip, doc)} ghost={ghost}
                      left={start * g.pxPerTick} width={length * g.pxPerTick} height={g.rowHeight - 2}
                      selected={selectedClips.includes(clip.id)}
                      pxPerTick={g.pxPerTick} snap={g.snap}
                      onPointerDown={beginClipDrag} onMenu={openClipMenu}
                    />
                  ))}
                </div>
              </div>
            );
          })}
          {lastRow < count - 1 && <Spacer rows={count - 1 - lastRow} rowHeight={g.rowHeight} />}

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
        </ContextMenuTrigger>
        {menu && <ClipMenu clipId={menu.clipId} at={menu.at} />}
        </ContextMenu>
      </div>
    </div>
  );
}

/** Rows scrolled out of view: their height and row lines, nothing else. */
function Spacer({ rows, rowHeight }: { rows: number; rowHeight: number }) {
  const lines = (color: string) => `repeating-linear-gradient(to bottom, transparent 0, transparent ${rowHeight - 1}px, ${color} ${rowHeight - 1}px, ${color} ${rowHeight}px)`;
  return (
    <div className="flex" style={{ height: rows * rowHeight }}>
      <div className="sticky left-0 z-10 shrink-0 border-r border-line bg-raised" style={{ width: HEADER_W, backgroundImage: lines("var(--color-line)") }} />
      <div className="flex-1" style={{ backgroundImage: lines("var(--color-grid-strong)") }} />
    </div>
  );
}
