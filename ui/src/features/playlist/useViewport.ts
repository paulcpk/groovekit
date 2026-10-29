import { useLayoutEffect, useState } from "react";
import { HEADER_W } from "./geometry";

/** The part of the lanes that's scrolled into view, in pixels, rounded out to whole chunks:
    only the rows and clips in and around it are drawn. Rounding means scrolling a little
    changes nothing, and so draws nothing. */
export interface Viewport {
  /** The scroller's size, less the row headers' width, rounded up. */
  width: number;
  height: number;
  /** How far it's scrolled, rounded down. */
  left: number;
  top: number;
}

export const CHUNK = 400;

export function useViewport(scroller: React.RefObject<HTMLDivElement | null>): Viewport {
  const [view, setView] = useState<Viewport>({ width: 1200, height: 800, left: 0, top: 0 });

  useLayoutEffect(() => {
    const el = scroller.current;
    if (!el) return;
    let frame = 0;

    const measure = () => {
      frame = 0;
      const next: Viewport = {
        width: Math.ceil(Math.max(1, el.clientWidth - HEADER_W) / CHUNK) * CHUNK,
        height: Math.ceil(Math.max(1, el.clientHeight) / CHUNK) * CHUNK,
        left: Math.floor(el.scrollLeft / CHUNK) * CHUNK,
        top: Math.floor(el.scrollTop / CHUNK) * CHUNK,
      };
      setView((v) => (v.width === next.width && v.height === next.height && v.left === next.left && v.top === next.top ? v : next));
    };
    const schedule = () => {
      if (!frame) frame = requestAnimationFrame(measure);
    };

    measure();
    const observer = new ResizeObserver(schedule);
    observer.observe(el);
    el.addEventListener("scroll", schedule, { passive: true });
    return () => {
      cancelAnimationFrame(frame);
      observer.disconnect();
      el.removeEventListener("scroll", schedule);
    };
  }, [scroller]);

  return view;
}
