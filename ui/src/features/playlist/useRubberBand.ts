import type { Project } from "@groovekit/schema/project";
import { useState } from "react";
import { floorTo, startDrag } from "@/lib/drag";
import { useStable } from "@/lib/useStable";
import { setUi, useUi } from "@/state/ui";
import { placePattern } from "./edits";
import { HEADER_W, RULER_H, type Geometry } from "./geometry";

/** The box dragged out over the lanes, in pixels from the lanes' top left corner. */
export interface Rubber {
  x0: number; y0: number; x1: number; y1: number;
}

/** A press on an empty part of a lane places the selected pattern there; a drag selects the
    clips in the box it draws (Shift: adds them). `content` is the element the lanes are laid
    out in. */
export function useRubberBand(doc: Project, g: Geometry, content: React.RefObject<HTMLDivElement | null>, onStart: () => void) {
  const [rubber, setRubber] = useState<Rubber | null>(null);

  const beginEmptyDrag = useStable((e: React.PointerEvent, laneIndex: number) => {
    // Only presses on the lane itself, not ones bubbling up from a menu or dialog it owns.
    if (e.button !== 0 || !e.currentTarget.contains(e.target as Node)) return;
    onStart();
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
  });

  return { rubber, beginEmptyDrag };
}
