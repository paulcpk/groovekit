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

/** The lanes worked out from the stems, with their names for people. */
const LANES: [string, string][] = [
  ["drums-low", "Kick & low"], ["drums-mid", "Claps & perc"], ["drums-high", "Hats & cymbals"], ["music-short", "Stabs & leads"], ["music-sustained", "Pads & atmosphere"],
];

/** A track's arrangement as lanes: the sections, which parts play in each bar (brighter when
    louder), and the loudness. Hovering names the bar; clicking plays the track from it. */
export function ArrangementView({ arrangement: a, file: track, stems, id }: {
  arrangement: Arrangement; file?: string; stems?: Record<string, string>; /** The map, for its lanes. */ id?: string;
}) {
  const [hover, setHover] = useState<number>();
  const [playing, setPlaying] = useState<{ bar: number; at: number }>();
  const [source, setSource] = useState("track");
  const [lanes, setLanes] = useState<Record<string, string>>({});
  const [splitting, setSplitting] = useState<string>();
  const [, tick] = useState(0);
  const previewing = useEngine((s) => s.previewing);
  const box = useRef<HTMLDivElement>(null);
  const barSeconds = (4 * 60) / a.tempo;

  // The whole track, one of its stems or a lane worked out from one (they all run in time with it).
  const fileOf = (name: string) => (name === "track" ? track : stems?.[name] ?? lanes[name] ?? track);
  const file = fileOf(source);

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

  // Another source while it plays: on from the same place. A lane is made the first time.
  const listenTo = async (next: string) => {
    let nextFile = fileOf(next);

    if (LANES.some(([name]) => name === next) && !lanes[next] && id) {
      setSplitting(next);

      try {
        nextFile = (await call<{ file: string }>("maps.lane", { id, lane: next })).file;
        setLanes((l) => ({ ...l, [next]: nextFile! }));
      } catch {
        return;
      } finally {
        setSplitting(undefined);
      }
    }

    setSource(next);

    if (head === undefined || !nextFile) return;
    useEngine.setState({ previewing: nextFile });
    setPlaying({ bar: head, at: Date.now() });
    void call("preview.play", { path: nextFile, from: a.downbeat + head * barSeconds });
  };
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
        {stems && (
          <div className="flex items-center gap-0.5" title="What plays when you click a bar: the whole track or one of its stems">
            {["track", "drums", "bass", "music", "vocals"].filter((s) => s === "track" || stems[s]).map((s) => (
              <button key={s} type="button" onClick={() => void listenTo(s)}
                className={cn("h-4 rounded-[3px] px-1.5 capitalize", source === s ? "bg-agent/20 text-fg" : "text-faint hover:text-fg")}>
                {s === "track" ? "Whole" : s}
              </button>
            ))}
          </div>
        )}
        {isPlaying && (
          <button type="button" onClick={() => void stopPreview()} className="flex items-center gap-1 text-agent hover:text-fg">
            <Square size={9} fill="currentColor" /> Stop
          </button>
        )}
      </div>

      {stems && id && (
        <div className="mb-1 flex flex-wrap items-center justify-end gap-0.5 text-2xs"
          title="Finer lanes worked out from the stems by filtering: rough, but the parts on their own">
          <span className="mr-1 text-faint">Lanes</span>
          {LANES.map(([name, label]) => (
            <button key={name} type="button" disabled={!!splitting} onClick={() => void listenTo(name)}
              className={cn("h-4 rounded-[3px] px-1.5", source === name ? "bg-agent/20 text-fg" : "text-faint hover:text-fg disabled:opacity-50")}>
              {splitting === name ? "Splitting…" : label}
            </button>
          ))}
        </div>
      )}

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
