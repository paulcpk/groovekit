import type { AudioClip, AutomationClip, AutomationPoint, PatternClip, Project } from "@groovekit/schema/project";
import { memo, useEffect, useMemo, useRef, useState } from "react";
import { automatedEffect, rangePosition, rangeValue, targetRangeOf } from "@/lib/automation";
import { useCatalogue } from "@/lib/catalogue";
import { snapTo, startDrag } from "@/lib/drag";
import { fitSpeed } from "@/lib/fit";
import { patternLoop, ticksPerBar } from "@/lib/model";
import { drawPeaks, usePeaks } from "@/lib/peaks";
import { execute, useEngine } from "@/state/engine";

// Each kind of content reads just what it needs from the song, so a clip is drawn again only
// when its pattern, sample or curve changes, not on every change to the song.

/** The pattern's notes, repeated over the clip: one row per channel and key, like a drum grid,
    all in one path. */
export const PatternContent = memo(function PatternContent({ clip, width, height, color }: {
  clip: PatternClip; width: number; height: number; color: string;
}) {
  const pattern = useEngine((s) => s.doc?.patterns.find((p) => p.id === clip.pattern));
  // The channels' order, as a string so it only changes when the order does.
  const channelOrder = useEngine((s) => s.doc?.channels.map((c) => c.id).join("\n") ?? "");
  const bar = useEngine((s) => ticksPerBar(s.doc));

  const rows = useMemo(() => {
    if (!pattern) return [];
    const order = new Map(channelOrder.split("\n").map((id, i) => [id, i]));
    const keys = [...new Set(pattern.notes.map((n) => `${n.channel}:${n.key ?? 60}`))];
    keys.sort((a, b) => {
      const [ca, ka] = a.split(":"), [cb, kb] = b.split(":");
      return (order.get(ca) ?? 0) - (order.get(cb) ?? 0) || Number(kb) - Number(ka);
    });
    return keys;
  }, [pattern, channelOrder]);

  const path = useMemo(() => {
    if (!pattern || pattern.notes.length === 0 || height < 6) return "";
    const loop = patternLoop(pattern, bar);
    const offset = clip.offset ?? 0;
    const rowH = Math.min(6, height / Math.max(1, rows.length));
    const top = (height - rowH * rows.length) / 2;
    const index = new Map(rows.map((r, i) => [r, i]));
    const h = Math.max(1, rowH - 1);
    const parts: string[] = [];
    let count = 0;

    for (let rep = Math.floor(offset / loop) * loop - offset; rep < clip.length; rep += loop) {
      for (const n of pattern.notes) {
        const start = rep + n.start;
        if (start + n.length <= 0 || start >= clip.length) continue;
        const row = index.get(`${n.channel}:${n.key ?? 60}`) ?? 0;
        const x = (Math.max(0, start) / clip.length) * width;
        const y = top + row * rowH;
        const w = Math.max(1.5, (Math.min(n.length, clip.length - start) / clip.length) * width - 0.5);
        // A rectangle with corners rounded by r.
        const r = Math.min(1, w / 2, h / 2);
        parts.push(`M${(x + r).toFixed(2)} ${y.toFixed(2)}h${(w - 2 * r).toFixed(2)}a${r} ${r} 0 0 1 ${r} ${r}v${(h - 2 * r).toFixed(2)}a${r} ${r} 0 0 1 -${r} ${r}h-${(w - 2 * r).toFixed(2)}a${r} ${r} 0 0 1 -${r} -${r}v-${(h - 2 * r).toFixed(2)}a${r} ${r} 0 0 1 ${r} -${r}z`);
        if (++count > 4000) return parts.join("");
      }
    }
    return parts.join("");
  }, [pattern, rows, bar, clip.offset, clip.length, width, height]);

  if (!path) return null;

  return (
    <svg width={width} height={height} className="absolute left-0" style={{ fill: `color-mix(in srgb, ${color} 88%, white)` }}>
      <path d={path} />
    </svg>
  );
});

/** The part of the file the clip plays, with handles at its top corners that drag its fades. */
export const AudioContent = memo(function AudioContent({ clip, width, height, color }: {
  clip: AudioClip; width: number; height: number; color: string;
}) {
  // The channel's first layer, which the clip plays; the same object until it changes.
  const layer = useEngine((s) => {
    const channel = s.doc?.channels.find((c) => c.id === clip.channel);
    return channel?.instrument.type === "sampler" ? channel.instrument.layers[0] : undefined;
  });
  const tempo = useEngine((s) => s.doc?.tempo ?? 120);
  const sample = layer?.sample, reverse = !!layer?.reverse, partStart = layer?.start ?? 0, partEnd = layer?.end;
  // The tempo the file plays at in the song when it's fitted to it (within the speed limit).
  const sourceTempo = layer?.sourceTempo ? tempo / fitSpeed({ tempo }, layer.sourceTempo) : undefined;
  const peaks = usePeaks(sample);
  const canvas = useRef<HTMLCanvasElement>(null);
  // Drawn at the screen's own resolution, so it's sharp on a Retina display.
  const dpr = typeof window === "undefined" ? 1 : Math.min(3, window.devicePixelRatio || 1);
  const pixels = Math.max(1, Math.min(8000, Math.round(width * dpr)));
  const [fades, setFades] = useState<{ fadeIn?: number; fadeOut?: number } | null>(null);

  useEffect(() => {
    if (!peaks || !canvas.current) return;
    // Stretched clips play a beat of the file per beat of the song.
    const fileSecondsPerTick = 60 / ((sourceTempo ?? tempo) * 960);
    const from = clip.offset ?? 0;
    const to = from + clip.length * fileSecondsPerTick;
    // The clip plays the layer's part of the file; reversed, its offset counts from the part's
    // end and it's drawn mirrored.
    const end = partEnd ?? peaks.durationSeconds;
    if (reverse) drawPeaks(canvas.current, peaks, end - to, end - from, color);
    else drawPeaks(canvas.current, peaks, partStart + from, partStart + to, color);
  }, [peaks, clip.offset, clip.length, sourceTempo, reverse, partStart, partEnd, tempo, color, pixels, height, dpr]);

  if (height < 4) return null;
  const ticksPerSecond = (tempo * 960) / 60;
  const pxPerSecond = (ticksPerSecond * width) / clip.length;
  const seconds = clip.length / ticksPerSecond;
  const fadeIn = (fades?.fadeIn ?? clip.fadeIn ?? 0) * pxPerSecond;
  const fadeOut = (fades?.fadeOut ?? clip.fadeOut ?? 0) * pxPerSecond;

  const dragFade = (e: React.PointerEvent, which: "fadeIn" | "fadeOut") => {
    if (e.button !== 0) return;
    e.stopPropagation();
    const from = clip[which] ?? 0;
    let value = from;
    startDrag(e, (dx) => {
      value = Math.round(Math.min(seconds, Math.max(0, from + (which === "fadeIn" ? dx : -dx) / pxPerSecond)) * 100) / 100;
      setFades({ [which]: value });
    }, (_dx, _dy, _e, moved) => {
      setFades(null);
      if (moved) void execute({ type: "updateClip", clip: clip.id, changes: { [which]: value || null } }, { label: which === "fadeIn" ? "Fade in" : "Fade out" });
    }, "ew-resize");
  };

  const handle = "absolute top-0 z-10 h-2 w-2 cursor-ew-resize rounded-[1px] border border-black/50 bg-white/80 opacity-0 group-hover/clip:opacity-100";

  return (
    <>
      <canvas ref={canvas} width={pixels} height={Math.max(1, Math.round(height * dpr))} className="absolute left-0"
        style={{ width, height, transform: reverse ? "scaleX(-1)" : undefined }} />
      {(fadeIn > 0 || fadeOut > 0) && (
        <svg className="pointer-events-none absolute left-0 top-0" width={width} height={height}>
          {fadeIn > 0 && <path d={`M0 0 L${fadeIn} 0 L0 ${height} Z`} fill="rgba(0,0,0,0.45)" stroke="rgba(255,255,255,0.5)" strokeWidth={1} />}
          {fadeOut > 0 && <path d={`M${width} 0 L${width - fadeOut} 0 L${width} ${height} Z`} fill="rgba(0,0,0,0.45)" stroke="rgba(255,255,255,0.5)" strokeWidth={1} />}
        </svg>
      )}
      {width > 24 && (
        <>
          <div className={handle} style={{ left: Math.min(width / 2 - 8, fadeIn + 2) }} title="Drag to fade in" onPointerDown={(e) => dragFade(e, "fadeIn")}
            onDoubleClick={(e) => e.stopPropagation()} />
          <div className={handle} style={{ right: Math.min(width / 2 - 8, fadeOut + 2) }} title="Drag to fade out" onPointerDown={(e) => dragFade(e, "fadeOut")}
            onDoubleClick={(e) => e.stopPropagation()} />
        </>
      )}
    </>
  );
});

/** The automation curve, editable: drag points, double-click to add, Alt-click to remove. */
export const AutomationContent = memo(function AutomationContent({ clip, width, height, color, pxPerTick, snap }: {
  clip: AutomationClip; width: number; height: number; color: string; pxPerTick: number; snap: number;
}) {
  const catalogue = useCatalogue();
  const effect = useEngine((s) => automatedEffect(clip.target, s.doc));
  const range = useMemo(() => targetRangeOf(clip.target, effect, catalogue), [clip.target, effect, catalogue]);
  const pad = 4;
  const usable = Math.max(1, height - pad * 2);
  const toY = (v: number) => pad + (1 - rangePosition(range, v)) * usable;
  const toValue = (y: number) => Math.round(rangeValue(range, 1 - (y - pad) / usable) * 1000) / 1000;
  const points = [...clip.points].sort((a, b) => a.time - b.time);

  if (height < 8) return null;

  const path = points.length === 0 ? "" : [
    `M 0 ${toY(points[0].value)}`,
    ...points.map((p) => `L ${p.time * pxPerTick} ${toY(p.value)}`),
    `L ${width} ${toY(points[points.length - 1].value)}`,
  ].join(" ");

  const save = (next: AutomationPoint[], label: string) =>
    void execute({ type: "updateClip", clip: clip.id, changes: { points: [...next].sort((a, b) => a.time - b.time) } }, { label });

  return (
    <svg
      width={width} height={height} className="absolute left-0"
      onDoubleClick={(e) => {
        e.stopPropagation();
        const r = e.currentTarget.getBoundingClientRect();
        const time = Math.max(0, Math.min(clip.length, snapTo((e.clientX - r.left) / pxPerTick, snap / 4)));
        save([...points, { time, value: toValue(e.clientY - r.top) }], "Add automation point");
      }}
    >
      <path d={path} stroke={color} strokeWidth={1.5} fill="none" />
      {points.map((p, i) => (
        <circle
          key={i} cx={p.time * pxPerTick} cy={toY(p.value)} r={3.5} fill={color} stroke="#111" strokeWidth={1}
          className="cursor-pointer"
          onPointerDown={(e) => {
            e.stopPropagation();
            if (e.altKey) {
              save(points.filter((_, j) => j !== i), "Remove automation point");
              return;
            }
            const svg = (e.currentTarget as SVGCircleElement).ownerSVGElement!;
            const r = svg.getBoundingClientRect();
            let next = points;
            startDrag(e, (_dx, _dy, ev) => {
              const time = Math.max(0, Math.min(clip.length, snapTo((ev.clientX - r.left) / pxPerTick, snap / 4)));
              const value = toValue(Math.min(height - pad, Math.max(pad, ev.clientY - r.top)));
              next = points.map((q, j) => (j === i ? { ...q, time, value } : q));
              useEngine.setState((s) => (s.doc ? { doc: patchPoints(s.doc, clip.id, next) } : s));
            }, (_dx, _dy, _ev, moved) => moved && save(next, "Move automation point"), "grabbing");
          }}
        />
      ))}
    </svg>
  );
});

/** A local preview of moved points while dragging; the engine's patch replaces it on release. */
function patchPoints(doc: Project, clipId: string, points: AutomationPoint[]): Project {
  return {
    ...doc,
    playlist: {
      ...doc.playlist,
      clips: doc.playlist.clips.map((c) => (c.id === clipId && c.type === "automation" ? { ...c, points } : c)),
    },
  };
}
