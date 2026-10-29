import type { Clip, Project } from "@groovekit/schema/project";
import { useRef, useState } from "react";
import { snapTo, startDrag } from "@/lib/drag";
import { useStable } from "@/lib/useStable";
import { setUi, useUi } from "@/state/ui";
import { commitDrag, secondsToTicks, type DragState } from "./edits";
import type { Geometry } from "./geometry";

/** Pressing a clip selects it (Shift: adds it, or the run of clips in its row from the one
    pressed last) and starts a drag: of its body to move (Alt: copy) the selection, of its right
    edge to resize it, of its left edge to trim it. `drag` is the drag in progress; the song
    changes on release. */
export function useClipDrag(doc: Project, g: Geometry, onStart: () => void) {
  const [drag, setDrag] = useState<DragState | null>(null);
  const anchor = useRef<string | null>(null);

  const beginClipDrag = useStable((e: React.PointerEvent, clip: Clip, edge: "left" | "right" | "body") => {
    e.stopPropagation();
    onStart();
    const current = useUi.getState().selectedClips;
    // Shift: everything in the row from the clip pressed last to this one; across rows, just this one.
    const last = e.shiftKey ? doc.playlist.clips.find((c) => c.id === anchor.current) : undefined;
    let selection: string[];
    if (last && last.row === clip.row && last.id !== clip.id) {
      const [a, b] = [Math.min(last.start, clip.start), Math.max(last.start, clip.start)];
      const between = doc.playlist.clips.filter((c) => c.row === clip.row && c.start >= a && c.start <= b).map((c) => c.id);
      selection = [...new Set([...current, ...between])];
    } else {
      selection = current.includes(clip.id) ? current : e.shiftKey ? [...current, clip.id] : [clip.id];
    }
    if (!e.shiftKey || !last) anchor.current = clip.id;
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
  });

  return { drag, beginClipDrag };
}
