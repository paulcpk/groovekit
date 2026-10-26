import type { AudioClip, AutomationClip, AutomationPoint, PatternClip, Project } from "@groovekit/schema/project";
import { memo, useEffect, useMemo, useRef } from "react";
import { rangePosition, rangeValue, targetRange } from "@/lib/automation";
import { useCatalogue } from "@/lib/catalogue";
import { snapTo, startDrag } from "@/lib/drag";
import { patternLength } from "@/lib/model";
import { drawPeaks, usePeaks } from "@/lib/peaks";
import { execute, useEngine } from "@/state/engine";

/** The pattern's notes, repeated over the clip: one row per channel and key, like a drum grid. */
export const PatternContent = memo(function PatternContent({ clip, doc, width, height, color }: {
  clip: PatternClip; doc: Project; width: number; height: number; color: string;
}) {
  const pattern = doc.patterns.find((p) => p.id === clip.pattern);

  const rows = useMemo(() => {
    if (!pattern) return [];
    const order = new Map(doc.channels.map((c, i) => [c.id, i]));
    const keys = [...new Set(pattern.notes.map((n) => `${n.channel}:${n.key ?? 60}`))];
    keys.sort((a, b) => {
      const [ca, ka] = a.split(":"), [cb, kb] = b.split(":");
      return (order.get(ca) ?? 0) - (order.get(cb) ?? 0) || Number(kb) - Number(ka);
    });
    return keys;
  }, [pattern, doc.channels]);

  if (!pattern || pattern.notes.length === 0 || height < 6) return null;

  const loop = patternLength(pattern, doc);
  const offset = clip.offset ?? 0;
  const rowH = Math.min(6, height / Math.max(1, rows.length));
  const top = (height - rowH * rows.length) / 2;
  const index = new Map(rows.map((r, i) => [r, i]));
  const rects = [];

  for (let rep = Math.floor(offset / loop) * loop - offset; rep < clip.length; rep += loop) {
    for (const n of pattern.notes) {
      const start = rep + n.start;
      if (start + n.length <= 0 || start >= clip.length) continue;
      const row = index.get(`${n.channel}:${n.key ?? 60}`) ?? 0;
      rects.push(
        <rect key={`${rep}-${rects.length}`} x={(Math.max(0, start) / clip.length) * width} y={top + row * rowH}
          width={Math.max(1.5, (Math.min(n.length, clip.length - start) / clip.length) * width - 0.5)} height={Math.max(1, rowH - 1)} rx={0.5} />,
      );
      if (rects.length > 4000) break;
    }
  }

  return (
    <svg width={width} height={height} className="absolute left-0" style={{ fill: color }}>
      {rects}
    </svg>
  );
});

/** The part of the file the clip plays. */
export function AudioContent({ clip, doc, width, height, color }: {
  clip: AudioClip; doc: Project; width: number; height: number; color: string;
}) {
  const peaks = usePeaks(clip.sample);
  const canvas = useRef<HTMLCanvasElement>(null);
  const pixels = Math.max(1, Math.min(4000, Math.round(width)));

  useEffect(() => {
    if (!peaks || !canvas.current) return;
    // Stretched clips play a beat of the file per beat of the song.
    const fileSecondsPerTick = 60 / ((clip.sourceTempo ?? doc.tempo) * 960);
    const start = clip.offset ?? 0;
    drawPeaks(canvas.current, peaks, start, start + clip.length * fileSecondsPerTick, color);
  }, [peaks, clip.offset, clip.length, clip.sourceTempo, doc.tempo, color, pixels, height]);

  if (height < 4) return null;
  return <canvas ref={canvas} width={pixels} height={Math.max(1, Math.round(height))} className="absolute left-0" style={{ width, height }} />;
}

/** The automation curve, editable: drag points, double-click to add, Alt-click to remove. */
export function AutomationContent({ clip, doc, width, height, color, pxPerTick, snap }: {
  clip: AutomationClip; doc: Project; width: number; height: number; color: string; pxPerTick: number; snap: number;
}) {
  const catalogue = useCatalogue();
  const range = targetRange(clip.target, doc, catalogue);
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
}

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
