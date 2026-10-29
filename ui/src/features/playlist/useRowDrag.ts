import { useRef, useState } from "react";
import { startDrag } from "@/lib/drag";
import { useStable } from "@/lib/useStable";
import { execute } from "@/state/engine";
import { setUi } from "@/state/ui";
import type { Geometry } from "./geometry";

/** Rows selected by their headers (a run, from the first to the last). */
export interface RowRange {
  from: number;
  to: number;
}

/** Pressing a row's header selects it (Shift: the rows from the last one pressed); dragging moves
    the selected rows, with their clips, names, mute and solo, so nothing sounds different.
    `rowDrag` is how many rows down the selection is being dragged, while it is. */
export function useRowDrag(g: Geometry) {
  const [rows, setRows] = useState<RowRange | null>(null);
  const [rowDrag, setRowDrag] = useState<number | null>(null);
  const anchor = useRef(0);

  const beginRowDrag = useStable((e: React.PointerEvent, index: number) => {
    let range: RowRange;
    if (e.shiftKey && rows) range = { from: Math.min(anchor.current, index), to: Math.max(anchor.current, index) };
    else if (rows && index >= rows.from && index <= rows.to) range = rows;
    else {
      range = { from: index, to: index };
      anchor.current = index;
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
      anchor.current += dRows;
    }, "grabbing");
  });

  const clearRows = useStable(() => setRows(null));

  return { rows, rowDrag, beginRowDrag, clearRows };
}
