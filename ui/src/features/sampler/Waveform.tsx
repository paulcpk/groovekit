import { useEffect, useRef, useState } from "react";
import { startDrag } from "@/lib/drag";
import { drawPeaks, usePeaks } from "@/lib/peaks";
import { formatSeconds } from "./EnvelopeGraph";

const W = 920, H = 200;

/** A sample's waveform with the part that plays between two handles. Pressing the waveform plays
    the channel (with its envelopes and filter) for as long as it's held, with a playhead. */
export function Waveform({ path, start, end, reverse, color, onRegion, onPress, onRelease }: {
  path: string; start: number; end: number | undefined; reverse: boolean; color: string;
  /** Without it, the whole file plays and there are no handles. */
  onRegion?: (start: number, end: number | null, commit: boolean) => void;
  onPress: () => void; onRelease: () => void;
}) {
  const peaks = usePeaks(path);
  const canvas = useRef<HTMLCanvasElement>(null);
  const box = useRef<HTMLDivElement>(null);
  const playhead = useRef<HTMLDivElement>(null);
  const [live, setLive] = useState<{ start: number; end: number } | null>(null);
  const duration = peaks?.durationSeconds ?? 0;
  const region = live ?? { start, end: end ?? duration };

  useEffect(() => {
    if (canvas.current && peaks) drawPeaks(canvas.current, peaks, 0, peaks.durationSeconds, color);
  }, [peaks, color]);

  const toX = (s: number) => (duration ? (s / duration) * 100 : 0);

  const dragHandle = (e: React.PointerEvent, which: "start" | "end") => {
    e.stopPropagation();
    const width = box.current!.getBoundingClientRect().width;
    const from = region;
    let next = from;
    startDrag(e, (dx) => {
      const s = from[which] + (dx / width) * duration;
      next = which === "start"
        ? { ...from, start: round(Math.min(Math.max(0, s), from.end - 0.005)) }
        : { ...from, end: round(Math.max(Math.min(duration, s), from.start + 0.005)) };
      setLive(next);
      onRegion?.(next.start, next.end >= duration - 0.0005 ? null : next.end, false);
    }, (_dx, _dy, _ev, moved) => {
      setLive(null);
      if (moved) onRegion?.(next.start, next.end >= duration - 0.0005 ? null : next.end, true);
    }, "ew-resize");
  };

  // The playhead runs at the sample's own speed: what's heard at the root key.
  const press = (e: React.PointerEvent) => {
    if (e.button !== 0 || !duration) return;
    onPress();
    const t0 = performance.now();
    const { start: s, end: en } = region;
    let frame = requestAnimationFrame(function tick() {
      const t = (performance.now() - t0) / 1000;
      const at = reverse ? en - t : s + t;
      const el = playhead.current;
      if (el) {
        el.style.display = t < en - s ? "block" : "none";
        el.style.left = `${toX(at)}%`;
      }
      if (t < en - s) frame = requestAnimationFrame(tick);
    });
    startDrag(e, () => {}, () => {
      cancelAnimationFrame(frame);
      if (playhead.current) playhead.current.style.display = "none";
      onRelease();
    });
  };

  return (
    <div ref={box} className="relative h-[120px] cursor-pointer overflow-hidden rounded-md bg-bg ring-1 ring-line" onPointerDown={press}
      title="Press to play">
      <canvas ref={canvas} width={W} height={H} className="absolute inset-0 h-full w-full" />
      {!peaks && <div className="absolute inset-0 flex items-center justify-center text-xs text-faint">Loading the waveform…</div>}
      {peaks && (
        <>
          <div className="pointer-events-none absolute inset-y-0 left-0 bg-black/55" style={{ width: `${toX(region.start)}%` }} />
          <div className="pointer-events-none absolute inset-y-0 right-0 bg-black/55" style={{ width: `${100 - toX(region.end)}%` }} />
          {onRegion && (["start", "end"] as const).map((which) => (
            <div key={which} className="group absolute inset-y-0 w-3 -translate-x-1/2 cursor-ew-resize" style={{ left: `${toX(region[which])}%` }}
              onPointerDown={(e) => dragHandle(e, which)} title={which === "start" ? "Start" : "End"}>
              <div className="mx-auto h-full w-px bg-fg/70 group-hover:bg-accent" />
              <div className={`absolute ${which === "start" ? "left-1.5" : "right-1.5"} top-0 h-2.5 w-2.5 bg-fg/80 group-hover:bg-accent`}
                style={{ clipPath: which === "start" ? "polygon(0 0, 100% 0, 0 100%)" : "polygon(0 0, 100% 0, 100% 100%)" }} />
            </div>
          ))}
          <div ref={playhead} className="pointer-events-none absolute inset-y-0 hidden w-px bg-white shadow-[0_0_6px_white]" />
          <div className="pointer-events-none absolute right-2 bottom-1 text-2xs text-faint tabular">
            {region.start > 0 || region.end < duration ? `${formatSeconds(region.start)} – ${formatSeconds(region.end)} of ` : ""}{formatSeconds(duration)}
          </div>
          {reverse && <div className="pointer-events-none absolute top-1 left-2 text-2xs font-medium text-accent">Reversed</div>}
        </>
      )}
    </div>
  );
}

function round(s: number) {
  return Math.round(s * 10000) / 10000;
}
