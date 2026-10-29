import { useEffect, useRef } from "react";
import { setUi, useUi } from "@/state/ui";
import { HEADER_W, RULER_H } from "./geometry";

/** ⌘/Ctrl + wheel zooms the time around the pointer. Shift + wheel too: scrolled up and down,
    it zooms the rows' height around the pointer; sideways, the time. (With Shift held, macOS
    reports a vertical wheel as a horizontal one, so whichever axis moves counts as the gesture's
    axis: a mouse wheel then zooms time, a trackpad swipe up or down the rows.) A trackpad sends
    many wheel events a frame; they're added up and applied once per frame, since each zoom
    step redraws every clip. */
export function useZoomWheel(scroller: React.RefObject<HTMLDivElement | null>, pxPerTick: number) {
  const current = useRef(pxPerTick);
  current.current = pxPerTick;

  useEffect(() => {
    const el = scroller.current;
    if (!el) return;
    let pending: { time: number; rows: number; clientX: number; clientY: number } | undefined;

    const apply = () => {
      const { time, rows, clientX, clientY } = pending!;
      pending = undefined;
      const box = el.getBoundingClientRect();

      if (time) {
        const x = clientX - box.left - HEADER_W + el.scrollLeft;
        const tick = x / current.current;
        const zoom = Math.min(400, Math.max(2, useUi.getState().playlistZoom * Math.exp(-time * 0.004)));
        setUi({ playlistZoom: zoom });
        requestAnimationFrame(() => (el.scrollLeft = tick * (zoom / 960) - (x - el.scrollLeft)));
      }

      if (rows) {
        const before = useUi.getState().rowHeight;
        const y = clientY - box.top - RULER_H + el.scrollTop;
        const rowHeight = Math.round(Math.min(120, Math.max(20, before * Math.exp(-rows * 0.004))));
        if (rowHeight === before) return;
        setUi({ rowHeight });
        requestAnimationFrame(() => (el.scrollTop = (y * rowHeight) / before - (y - el.scrollTop)));
      }
    };

    const onWheel = (e: WheelEvent) => {
      const zoom = e.metaKey || e.ctrlKey;
      if (!zoom && !e.shiftKey) return;
      e.preventDefault();
      // ⌘: time, whichever way the wheel turns. Shift: by the axis that moved.
      const time = zoom ? e.deltaY || e.deltaX : e.deltaX;
      const rows = zoom ? 0 : e.deltaY;
      if (pending) pending = { time: pending.time + time, rows: pending.rows + rows, clientX: e.clientX, clientY: e.clientY };
      else {
        pending = { time, rows, clientX: e.clientX, clientY: e.clientY };
        requestAnimationFrame(apply);
      }
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, [scroller]);
}
