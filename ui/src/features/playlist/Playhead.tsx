import { useEffect } from "react";
import { useEngine } from "@/state/engine";
import { useUi } from "@/state/ui";
import { HEADER_W } from "./geometry";

/** The play position across the lanes; it also keeps itself in view while playing. */
export function Playhead({ pxPerTick, scroller }: { pxPerTick: number; scroller: React.RefObject<HTMLDivElement | null> }) {
  const position = useEngine((s) => s.transport.position);
  const playing = useEngine((s) => s.transport.playing);
  const follow = useUi((s) => s.followPlayhead);
  // In pattern mode the position is within the pattern, not the song.
  const songMode = useEngine((s) => s.transport.mode !== "pattern");
  const x = position * pxPerTick;

  useEffect(() => {
    const el = scroller.current;
    if (!el || !playing || !follow || !songMode) return;
    const visible = el.clientWidth - HEADER_W;
    if (x < el.scrollLeft || x > el.scrollLeft + visible - 24) el.scrollLeft = Math.max(0, x - 40);
  }, [x, playing, follow, songMode, scroller]);

  if (!songMode) return null;

  return (
    <div className="pointer-events-none absolute top-0 bottom-0 z-[15] w-px bg-fg/80" style={{ left: HEADER_W + x }}>
      <div className="absolute -top-px -left-[4px] h-0 w-0 border-x-[4.5px] border-t-[6px] border-x-transparent border-t-fg" />
    </div>
  );
}
