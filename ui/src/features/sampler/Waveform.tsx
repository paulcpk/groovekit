import { useEffect, useRef, useState } from "react";
import { startDrag } from "@/lib/drag";
import { useWave, type Wave } from "@/lib/peaks";
import { formatSeconds } from "./EnvelopeGraph";

/** How a layer changes the sound before it plays, drawn over its wave. */
export interface WaveShaping {
  reverse: boolean;
  invert?: boolean;
  normalize?: boolean;
  fadeIn?: number;
  fadeOut?: number;
  loop?: "off" | "forward" | "pingPong";
}

const H = 140;

/** A sample's wave in detail, with the part that plays between two handles, drawn the way it
    plays: backwards when reversed, flipped, normalized and faded as the layer does. Pressing the
    wave plays the channel (with its envelopes and filter) for as long as it's held, with a
    playhead. */
export function Waveform({ path, start, end, shaping, color, onRegion, onPress, onRelease }: {
  path: string; start: number; end: number | undefined; shaping: WaveShaping; color: string;
  /** Without it, the whole file plays and there are no handles. */
  onRegion?: (start: number, end: number | null, commit: boolean) => void;
  onPress: () => void; onRelease: () => void;
}) {
  const wave = useWave(path);
  const canvas = useRef<HTMLCanvasElement>(null);
  const box = useRef<HTMLDivElement>(null);
  const playhead = useRef<HTMLDivElement>(null);
  const [live, setLive] = useState<{ start: number; end: number } | null>(null);
  const [size, setSize] = useState({ width: 0, ratio: 1 });
  const duration = wave?.durationSeconds ?? 0;
  const region = live ?? { start, end: end ?? duration };
  const { reverse } = shaping;

  // The canvas has as many pixels as the screen shows.
  useEffect(() => {
    const el = box.current;
    if (!el) return;
    const observer = new ResizeObserver(() => setSize({ width: el.clientWidth, ratio: window.devicePixelRatio || 1 }));
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    const c = canvas.current;
    if (!c || !wave || !size.width) return;
    c.width = Math.round(size.width * size.ratio);
    c.height = Math.round(H * size.ratio);
    drawWave(c, wave, region, shaping, color);
  }, [wave, size, color, region.start, region.end, shaping.reverse, shaping.invert, shaping.normalize, shaping.fadeIn, shaping.fadeOut, shaping.loop]); // eslint-disable-line react-hooks/exhaustive-deps

  // Reversed, the file is drawn backwards: a time in the file sits this far across.
  const toX = (s: number) => (duration ? ((reverse ? duration - s : s) / duration) * 100 : 0);
  const left = Math.min(toX(region.start), toX(region.end)), right = Math.max(toX(region.start), toX(region.end));

  const dragHandle = (e: React.PointerEvent, side: "left" | "right") => {
    e.stopPropagation();
    const width = box.current!.getBoundingClientRect().width;
    const from = region;
    // Reversed, the left handle is where the part ends in the file.
    const which = (side === "left") !== reverse ? "start" : "end";
    let next = from;
    startDrag(e, (dx) => {
      const s = from[which] + ((reverse ? -dx : dx) / width) * duration;
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

  // The playhead runs across the part at the sample's own speed: what's heard at the root key.
  const press = (e: React.PointerEvent) => {
    if (e.button !== 0 || !duration) return;
    onPress();
    const t0 = performance.now();
    const length = region.end - region.start;
    let frame = requestAnimationFrame(function tick() {
      const t = (performance.now() - t0) / 1000;
      const el = playhead.current;
      const loop = shaping.loop ?? "off";
      const looping = loop !== "off";
      const along = loop !== "off" ? loopPosition(t, length, loop) : t;
      if (el) {
        el.style.display = looping || t < length ? "block" : "none";
        el.style.left = `${left + (along / length) * (right - left)}%`;
      }
      if (looping || t < length) frame = requestAnimationFrame(tick);
    });
    startDrag(e, () => {}, () => {
      cancelAnimationFrame(frame);
      if (playhead.current) playhead.current.style.display = "none";
      onRelease();
    });
  };

  return (
    <div ref={box} className="relative cursor-pointer overflow-hidden rounded-md bg-bg ring-1 ring-line" style={{ height: H }} onPointerDown={press}
      title="Press to play">
      <canvas ref={canvas} className="absolute inset-0 h-full w-full" />
      {!wave && <div className="absolute inset-0 flex items-center justify-center text-xs text-faint">Loading the waveform…</div>}
      {wave && (
        <>
          <div className="pointer-events-none absolute inset-y-0 left-0 bg-black/55" style={{ width: `${left}%` }} />
          <div className="pointer-events-none absolute inset-y-0 right-0 bg-black/55" style={{ width: `${100 - right}%` }} />
          {onRegion && (["left", "right"] as const).map((side) => (
            <div key={side} className="group absolute inset-y-0 w-3 -translate-x-1/2 cursor-ew-resize" style={{ left: `${side === "left" ? left : right}%` }}
              onPointerDown={(e) => dragHandle(e, side)} title={side === "left" ? "Where it starts" : "Where it ends"}>
              <div className="mx-auto h-full w-px bg-fg/70 group-hover:bg-accent" />
              <div className={`absolute ${side === "left" ? "left-1.5" : "right-1.5"} top-0 h-2.5 w-2.5 bg-fg/80 group-hover:bg-accent`}
                style={{ clipPath: side === "left" ? "polygon(0 0, 100% 0, 0 100%)" : "polygon(0 0, 100% 0, 100% 100%)" }} />
            </div>
          ))}
          <div ref={playhead} className="pointer-events-none absolute inset-y-0 hidden w-px bg-white shadow-[0_0_6px_white]" />
          <div className="pointer-events-none absolute right-2 bottom-1 text-2xs text-faint tabular">
            {region.start > 0 || region.end < duration ? `${formatSeconds(region.start)} – ${formatSeconds(region.end)} of ` : ""}{formatSeconds(duration)}
          </div>
          <div className="pointer-events-none absolute top-1 left-2 flex gap-1.5 text-2xs font-medium text-accent">
            {reverse && <span>Reversed</span>}
            {shaping.loop === "forward" && <span>Loop</span>}
            {shaping.loop === "pingPong" && <span>Ping-pong</span>}
          </div>
        </>
      )}
    </div>
  );
}

/** Where a looping part is after t seconds of a part this long. */
function loopPosition(t: number, length: number, loop: "forward" | "pingPong") {
  if (loop === "forward") return t % length;
  const cycle = t % (2 * length);
  return cycle < length ? cycle : 2 * length - cycle;
}

/** The wave between the lowest and highest value of each pixel's slices, with a brighter
    outline, the centre line, and what the layer does to it: faded, flipped, normalized. */
function drawWave(canvas: HTMLCanvasElement, wave: Wave, region: { start: number; end: number }, shaping: WaveShaping, color: string) {
  const g = canvas.getContext("2d");
  if (!g) return;
  const { width, height } = canvas;
  const { durationSeconds: duration, resolution, min, max } = wave;
  g.clearRect(0, 0, width, height);
  const mid = height / 2, amp = mid * 0.92;

  // Normalizing brings the part that plays up to full height.
  let scale = 1;
  if (shaping.normalize) {
    let peak = 0;
    for (let i = Math.floor(region.start * resolution); i < Math.min(min.length, Math.ceil(region.end * resolution)); i++) peak = Math.max(peak, -min[i], max[i]);
    if (peak > 1e-4) scale = 1 / peak;
  }
  const flip = shaping.invert ? -1 : 1;

  // The fades follow the order the part plays in.
  const fade = (t: number) => {
    if (t < region.start || t > region.end) return 1;
    const along = shaping.reverse ? region.end - t : t - region.start, left = region.end - region.start - along;
    let f = 1;
    if (shaping.fadeIn) f *= Math.min(1, along / shaping.fadeIn);
    if (shaping.fadeOut && (shaping.loop ?? "off") === "off") f *= Math.min(1, left / shaping.fadeOut);
    return f;
  };

  const tops: number[] = [], bottoms: number[] = [];
  for (let x = 0; x < width; x++) {
    // Reversed, the file runs from right to left.
    const a = (x / width) * duration, b = ((x + 1) / width) * duration;
    const [t0, t1] = shaping.reverse ? [duration - b, duration - a] : [a, b];
    const from = Math.floor(t0 * resolution), to = Math.max(from + 1, Math.floor(t1 * resolution));
    let lo = 0, hi = 0;
    for (let i = Math.max(0, from); i < Math.min(min.length, to); i++) {
      lo = Math.min(lo, min[i]);
      hi = Math.max(hi, max[i]);
    }
    const k = scale * fade((t0 + t1) / 2);
    const [low, high] = flip > 0 ? [lo * k, hi * k] : [-hi * k, -lo * k];
    tops.push(mid - Math.min(1, high) * amp);
    bottoms.push(mid - Math.max(-1, low) * amp);
  }

  g.fillStyle = color;
  g.globalAlpha = 0.55;
  for (let x = 0; x < width; x++) g.fillRect(x, tops[x], 1, Math.max(1, bottoms[x] - tops[x]));
  g.globalAlpha = 1;

  // The outline, so thin waves still read as a line.
  g.strokeStyle = color;
  g.lineWidth = Math.max(1, window.devicePixelRatio || 1);
  for (const edge of [tops, bottoms]) {
    g.beginPath();
    edge.forEach((y, x) => (x ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.stroke();
  }

  g.fillStyle = "rgba(255,255,255,0.12)";
  g.fillRect(0, Math.round(mid), width, 1);

  // The fade curves, as lines over the part.
  if (shaping.fadeIn || shaping.fadeOut) {
    g.strokeStyle = "rgba(255,255,255,0.55)";
    g.setLineDash([4, 4]);
    g.beginPath();
    for (let x = 0; x < width; x++) {
      const t = ((shaping.reverse ? width - x : x) / width) * duration;
      if (t < region.start || t > region.end) continue;
      const y = mid - fade(t) * amp;
      g.lineTo(x, y);
    }
    g.stroke();
    g.setLineDash([]);
  }
}

function round(s: number) {
  return Math.round(s * 10000) / 10000;
}
