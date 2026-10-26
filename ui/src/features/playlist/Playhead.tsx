import { useRef } from "react";
import { useLivePosition } from "@/lib/playhead";
import { useEngine } from "@/state/engine";
import { useUi } from "@/state/ui";
import { HEADER_W } from "./geometry";

/** The play position across the lanes, moved every frame; it also keeps itself in view while playing. */
export function Playhead({ pxPerTick, scroller }: { pxPerTick: number; scroller: React.RefObject<HTMLDivElement | null> }) {
  const line = useRef<HTMLDivElement>(null);
  const follow = useUi((s) => s.followPlayhead);
  // In pattern mode the position is within the pattern, not the song.
  const songMode = useEngine((s) => s.transport.mode !== "pattern");

  useLivePosition(({ ticks, playing }) => {
    const x = ticks * pxPerTick;
    if (line.current) line.current.style.transform = `translateX(${HEADER_W + x}px)`;

    const el = scroller.current;
    if (!el || !playing || !follow) return;
    const visible = el.clientWidth - HEADER_W;
    if (x < el.scrollLeft || x > el.scrollLeft + visible - 24) el.scrollLeft = Math.max(0, x - 40);
  }, [pxPerTick, follow, songMode]);

  if (!songMode) return null;

  return (
    <div ref={line} className="pointer-events-none absolute top-0 bottom-0 left-0 z-[15] w-px bg-fg/80 will-change-transform">
      <div className="absolute -top-px -left-[4px] h-0 w-0 border-x-[4.5px] border-t-[6px] border-x-transparent border-t-fg" />
    </div>
  );
}
