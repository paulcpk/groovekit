import type { Envelope } from "@groovekit/schema/project";
import { useRef, useState } from "react";
import { Knob } from "@/components/controls";
import { startDrag } from "@/lib/drag";

type Env = Required<Envelope>;

export const DEFAULT_ENVELOPE: Env = { attack: 0, decay: 0, sustain: 1, release: 0.01 };

const W = 460, H = 132, PAD = 10;

/** An attack-decay-sustain-release envelope drawn to scale, edited by dragging its corners or
    with the knobs below. While a corner moves the scale holds still, so the drag stays under the
    pointer. */
export function EnvelopeGraph({ envelope, color, label, onChange, onCommit }: {
  envelope: Envelope | undefined; color: string; label?: string;
  onChange: (env: Env) => void; onCommit: (env: Env) => void;
}) {
  const saved = { ...DEFAULT_ENVELOPE, ...envelope };
  const [live, setLive] = useState<Env | null>(null);
  const env = live ?? saved;
  const scaleRef = useRef<number | null>(null);

  const total = (e: Env) => {
    const moving = e.attack + e.decay + e.release;
    return moving + Math.max(0.15, moving * 0.3);
  };
  const secondsPerPx = scaleRef.current ?? total(env) / (W - 2 * PAD);
  const hold = total(env) - env.attack - env.decay - env.release;
  const x = (t: number) => PAD + t / secondsPerPx;
  const y = (level: number) => PAD + (1 - level) * (H - 2 * PAD);

  const points = [
    [x(0), y(0)],
    [x(env.attack), y(1)],
    [x(env.attack + env.decay), y(env.sustain)],
    [x(env.attack + env.decay + hold), y(env.sustain)],
    [x(env.attack + env.decay + hold + env.release), y(0)],
  ];
  const line = points.map(([px, py], i) => `${i ? "L" : "M"} ${px} ${py}`).join(" ");
  const round = (v: number) => Math.round(v * 1000) / 1000;

  const drag = (e: React.PointerEvent, change: (start: Env, dx: number, dy: number) => Partial<Env>) => {
    e.stopPropagation();
    const start = env;
    scaleRef.current = secondsPerPx;
    let next = start;
    startDrag(e, (dx, dy) => {
      next = { ...start, ...change(start, dx * secondsPerPx, -dy / (H - 2 * PAD)) };
      next = { attack: round(clamp(next.attack, 0, 10)), decay: round(clamp(next.decay, 0, 10)), sustain: round(clamp(next.sustain, 0, 1)), release: round(clamp(next.release, 0, 10)) };
      setLive(next);
      onChange(next);
    }, (_dx, _dy, _ev, moved) => {
      scaleRef.current = null;
      setLive(null);
      if (moved) onCommit(next);
    }, "grabbing");
  };

  const handles: { at: number[]; title: string; change: (s: Env, dt: number, dl: number) => Partial<Env> }[] = [
    { at: points[1], title: "Attack", change: (s, dt) => ({ attack: s.attack + dt }) },
    { at: points[2], title: "Decay and sustain", change: (s, dt, dl) => ({ decay: s.decay + dt, sustain: s.sustain + dl }) },
    { at: points[3], title: "Sustain", change: (s, _dt, dl) => ({ sustain: s.sustain + dl }) },
    { at: points[4], title: "Release", change: (s, dt) => ({ release: s.release + dt }) },
  ];

  const knob = (key: keyof Env, short: string, max: number) => (
    <Knob size={28} label={short} title={key[0].toUpperCase() + key.slice(1)} value={env[key]} min={0} max={max} defaultValue={DEFAULT_ENVELOPE[key]}
      format={(v) => (key === "sustain" ? `${Math.round(v * 100)}%` : formatSeconds(v))}
      onChange={(v) => {
        const next = { ...env, [key]: round(v) };
        setLive(next);
        onChange(next);
      }}
      onCommit={(v) => {
        setLive(null);
        onCommit({ ...env, [key]: round(v) });
      }} />
  );

  return (
    <div className="flex flex-col gap-2">
      <div className="flex items-baseline justify-between px-0.5 text-2xs text-faint">
        <span className="uppercase tracking-wide">{label}</span>
        <span className="tabular">{formatSeconds(total(env))} shown</span>
      </div>
      <div className="relative -mt-1 rounded-md bg-bg ring-1 ring-line">
        <svg viewBox={`0 0 ${W} ${H}`} className="block w-full">
          {[0.25, 0.5, 0.75].map((l) => <line key={l} x1={PAD} x2={W - PAD} y1={y(l)} y2={y(l)} stroke="#1f1f24" />)}
          <path d={`${line} L ${points[4][0]} ${y(0)} L ${points[0][0]} ${y(0)} Z`} fill={color} fillOpacity={0.14} />
          <path d={line} fill="none" stroke={color} strokeWidth={2} strokeLinejoin="round" />
          {handles.map((h) => (
            <circle key={h.title} cx={h.at[0]} cy={h.at[1]} r={5} fill="#111113" stroke={color} strokeWidth={2}
              className="cursor-grab hover:fill-[#2a2a30]" onPointerDown={(e) => drag(e, h.change)}>
              <title>{h.title}</title>
            </circle>
          ))}
        </svg>
      </div>
      <div className="flex justify-around">
        {knob("attack", "Attack", 4)}
        {knob("decay", "Decay", 6)}
        {knob("sustain", "Sustain", 1)}
        {knob("release", "Release", 6)}
      </div>
    </div>
  );
}

function clamp(v: number, lo: number, hi: number) {
  return Math.min(hi, Math.max(lo, v));
}

export function formatSeconds(s: number) {
  return s >= 1 ? `${s.toFixed(2)} s` : `${Math.round(s * 1000)} ms`;
}
