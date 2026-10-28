import { Square } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { stopPreview } from "@/features/shell/actions";
import { cn } from "@/lib/cn";
import { call, useEngine } from "@/state/engine";
import type { Arrangement } from "./maps";

const PARTS = ["kick", "rumble", "bass", "hats", "percussion", "music", "vocal"];

const ROLE: Record<string, string> = {
  intro: "var(--color-faint)", outro: "var(--color-faint)", groove: "color-mix(in srgb, var(--color-agent) 45%, transparent)",
  build: "var(--color-accent)", break: "var(--color-dim)", drop: "var(--color-agent)",
};

const time = (s: number) => `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, "0")}`;

/** A track's arrangement as lanes: the sections, which parts play in each bar (brighter when
    louder), and the loudness. Hovering names the bar; clicking plays the track from it. */
export function ArrangementView({ arrangement: a, file }: { arrangement: Arrangement; file?: string }) {
  const [hover, setHover] = useState<number>();
  const [playing, setPlaying] = useState<{ bar: number; at: number }>();
  const [, tick] = useState(0);
  const previewing = useEngine((s) => s.previewing);
  const box = useRef<HTMLDivElement>(null);
  const barSeconds = (4 * 60) / a.tempo;

  // The playhead moves while this track plays.
  const isPlaying = !!playing && !!file && previewing === file;
  useEffect(() => {
    if (!isPlaying) return;
    const t = setInterval(() => tick((n) => n + 1), 100);
    return () => clearInterval(t);
  }, [isPlaying]);

  const barAt = (e: { clientX: number }) => {
    const r = box.current!.getBoundingClientRect();
    return Math.max(0, Math.min(a.bars - 1, Math.floor(((e.clientX - r.left) / r.width) * a.bars)));
  };

  const play = (bar: number) => {
    if (!file) return;
    useEngine.setState({ previewing: file });
    setPlaying({ bar, at: Date.now() });
    void call("preview.play", { path: file, from: a.downbeat + bar * barSeconds });
  };

  const head = isPlaying ? playing.bar + (Date.now() - playing.at) / 1000 / barSeconds : undefined;
  const section = hover !== undefined ? a.sections.find((s) => hover >= s.start && hover < s.end) : undefined;
  const loudest = Math.max(...a.loudness);
  const quietest = loudest - 24;
  const curve = a.loudness.map((l, i) => `${i + 0.5},${24 - Math.max(0, Math.min(1, (l - quietest) / 24)) * 22}`).join(" ");

  return (
    <div>
      <div className="mb-1 flex h-4 items-center gap-2 text-2xs text-dim tabular">
        {hover !== undefined ? (
          <span>Bar {hover + 1} · {time(a.downbeat + hover * barSeconds)}{section && ` · ${section.role}`}{file ? " · click to play from here" : ""}</span>
        ) : (
          <span className="text-faint">{a.tempo.toFixed(1)} BPM · {a.bars} bars · {time(a.seconds)}</span>
        )}
        <span className="flex-1" />
        {isPlaying && (
          <button type="button" onClick={() => void stopPreview()} className="flex items-center gap-1 text-agent hover:text-fg">
            <Square size={9} fill="currentColor" /> Stop
          </button>
        )}
      </div>

      <div className="flex gap-2">
        <div className="flex w-16 shrink-0 flex-col text-2xs text-faint">
          <div className="h-3.5" />
          {PARTS.map((p) => <div key={p} className="flex h-3 items-center capitalize">{p}</div>)}
          <div className="mt-1 flex h-6 items-center">loudness</div>
        </div>

        <div ref={box} className={cn("relative min-w-0 flex-1", file && "cursor-pointer")}
          onPointerMove={(e) => setHover(barAt(e))} onPointerLeave={() => setHover(undefined)} onClick={(e) => play(barAt(e))}>
          <svg viewBox={`0 0 ${a.bars} 1`} preserveAspectRatio="none" className="block h-3 w-full">
            {a.sections.map((s) => <rect key={s.start} x={s.start + 0.15} y={0} width={s.end - s.start - 0.3} height={1} fill={ROLE[s.role] ?? "var(--color-faint)"} />)}
          </svg>
          <div className="h-0.5" />
          <svg viewBox={`0 0 ${a.bars} ${PARTS.length}`} preserveAspectRatio="none" className="block w-full" style={{ height: PARTS.length * 12 }}>
            {PARTS.map((p, row) => {
              const lane = a.parts[p];
              if (!lane) return null;
              const top = Math.max(...lane.level);
              return [...lane.on].map((on, bar) => on === "1" && (
                <rect key={`${p}${bar}`} x={bar} y={row + 0.15} width={1.02} height={0.7} fill="var(--color-agent)"
                  opacity={0.35 + 0.65 * Math.max(0, Math.min(1, (lane.level[bar] - (top - 18)) / 18))} />
              ));
            })}
          </svg>
          <svg viewBox={`0 0 ${a.bars} 24`} preserveAspectRatio="none" className="mt-1 block h-6 w-full rounded-[2px] bg-bg">
            <polyline points={curve} fill="none" stroke="var(--color-dim)" strokeWidth={1} vectorEffect="non-scaling-stroke" />
          </svg>
          {hover !== undefined && <div className="pointer-events-none absolute inset-y-0 w-px bg-fg/40" style={{ left: `${((hover + 0.5) / a.bars) * 100}%` }} />}
          {head !== undefined && head < a.bars && <div className="pointer-events-none absolute inset-y-0 w-px bg-agent" style={{ left: `${(head / a.bars) * 100}%` }} />}
        </div>
      </div>

      <div className="mt-2 flex flex-wrap gap-x-3 gap-y-0.5 pl-[72px] text-2xs text-faint">
        {Object.entries({ intro: "intro / outro", groove: "groove", build: "build", break: "break", drop: "drop" }).map(([role, label]) => (
          <span key={role} className="flex items-center gap-1"><span className="h-2 w-2 rounded-[2px]" style={{ background: ROLE[role] }} />{label}</span>
        ))}
      </div>
    </div>
  );
}
