import { useEffect, useRef } from "react";
import { setUi, useUi } from "@/state/ui";
import { HEADER_W } from "./geometry";

/** ⌘/Ctrl + wheel zooms around the pointer. A trackpad sends many wheel events a frame; they're
    added up and applied once per frame, since each zoom step redraws every clip. */
export function useZoomWheel(scroller: React.RefObject<HTMLDivElement | null>, pxPerTick: number) {
  const current = useRef(pxPerTick);
  current.current = pxPerTick;

  useEffect(() => {
    const el = scroller.current;
    if (!el) return;
    let pending: { deltaY: number; clientX: number } | undefined;

    const apply = () => {
      const { deltaY, clientX } = pending!;
      pending = undefined;
      const x = clientX - el.getBoundingClientRect().left - HEADER_W + el.scrollLeft;
      const tick = x / current.current;
      const zoom = Math.min(400, Math.max(2, useUi.getState().playlistZoom * Math.exp(-deltaY * 0.004)));
      setUi({ playlistZoom: zoom });
      requestAnimationFrame(() => (el.scrollLeft = tick * (zoom / 960) - (x - el.scrollLeft)));
    };

    const onWheel = (e: WheelEvent) => {
      if (!e.metaKey && !e.ctrlKey) return;
      e.preventDefault();
      if (pending) pending = { deltaY: pending.deltaY + e.deltaY, clientX: e.clientX };
      else {
        pending = { deltaY: e.deltaY, clientX: e.clientX };
        requestAnimationFrame(apply);
      }
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, [scroller]);
}
