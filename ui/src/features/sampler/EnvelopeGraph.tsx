import type { Envelope } from "@groovekit/schema/project";
import { useEffect, useRef, useState } from "react";
import { Knob } from "@/components/controls";
import { cn } from "@/lib/cn";
import { startDrag } from "@/lib/drag";
import { useHeld } from "@/lib/held";
import { useNotePlayed } from "@/features/shell/liveInput";

export type Env = Required<Envelope>;

export const DEFAULT_ENVELOPE: Env = {
  delay: 0, attack: 0, hold: 0, decay: 0, sustain: 1, release: 0.01, attackCurve: 0, decayCurve: 0, releaseCurve: 0,
};

const W = 460, H = 128, PAD = 10, SUSTAIN_W = 56;
/** Each timed segment can take this much of the width, at its longest. */
const SEGMENT_W = (W - 2 * PAD - SUSTAIN_W) / 5;
/** Knobs and the graph place times on a cube curve up to this many seconds, so short times
    (where a drum's envelope lives) get most of the travel. */
const MAX_SECONDS = 10;

export const timeToPosition = (t: number) => Math.cbrt(Math.min(MAX_SECONDS, Math.max(0, t)) / MAX_SECONDS);
export const positionToTime = (p: number) => round(MAX_SECONDS * Math.pow(Math.min(1, Math.max(0, p)), 3));

/** How far along a segment the level has moved at progress p: the engine's bend
    (sampler::EnvelopeShape::bend). */
export function bend(p: number, curve: number) {
  if (!curve) return p;
  const e = Math.pow(2, -4 * curve);
  return p / (p + (1 - p) * e);
}

type TimeKey = "delay" | "attack" | "hold" | "decay" | "release";

/** An envelope drawn at a fixed scale, each segment as wide as its knob is turned, so changing
    one part never moves the others about. Drag the corners for times (and the sustain level),
    the small points on the slopes for their curves, or use the knobs. A note played from here
    runs through it as a glowing dot. */
export function EnvelopeGraph({ envelope, color, label, dim, extra, onChange, onCommit }: {
  envelope: Envelope | undefined; color: string; label?: string;
  /** Drawn faded: the envelope does nothing (its amount is 0). */
  dim?: boolean;
  /** More knobs, at the end of the row (an amount). */
  extra?: React.ReactNode;
  onChange: (env: Env) => void; onCommit: (env: Env) => void;
}) {
  const [env, hold] = useHeld<Env>({ ...DEFAULT_ENVELOPE, ...envelope });
  const [readout, setReadout] = useState<string>();
  const svg = useRef<SVGSVGElement>(null);

  const layout = segments(env);
  const y = (level: number) => PAD + (1 - level) * (H - 2 * PAD);

  // The outline, with the curves sampled along each slope.
  const outline: [number, number][] = [[layout.start, y(0)], [layout.delayEnd, y(0)]];
  const slope = (x0: number, x1: number, level: (p: number) => number) => {
    for (let i = 1; i <= 24; i++) outline.push([x0 + ((x1 - x0) * i) / 24, y(level(i / 24))]);
  };
  slope(layout.delayEnd, layout.attackEnd, (p) => bend(p, env.attackCurve));
  outline.push([layout.holdEnd, y(1)]);
  slope(layout.holdEnd, layout.decayEnd, (p) => 1 - (1 - env.sustain) * bend(p, env.decayCurve));
  outline.push([layout.sustainEnd, y(env.sustain)]);
  slope(layout.sustainEnd, layout.releaseEnd, (p) => env.sustain * (1 - bend(p, env.releaseCurve)));
  const line = outline.map(([px, py], i) => `${i ? "L" : "M"} ${px.toFixed(1)} ${py.toFixed(1)}`).join(" ");

  const set = (next: Env, commit: boolean) => {
    hold(next);
    (commit ? onCommit : onChange)(next);
  };

  // A drag in screen pixels, in the graph's own units.
  const scale = () => W / (svg.current?.getBoundingClientRect().width || W);

  const dragTime = (e: React.PointerEvent, key: TimeKey, sustainToo = false) => {
    e.stopPropagation();
    const start = env, k = scale(), p0 = timeToPosition(start[key]);
    let next = start;
    startDrag(e, (dx, dy) => {
      next = { ...start, [key]: positionToTime(p0 + (dx * k) / SEGMENT_W) };
      if (sustainToo) next.sustain = round(clamp(start.sustain - (dy * k) / (H - 2 * PAD), 0, 1));
      setReadout(`${nameOf(key)} ${formatSeconds(next[key])}${sustainToo ? ` · Sustain ${Math.round(next.sustain * 100)}%` : ""}`);
      set(next, false);
    }, (_dx, _dy, _ev, moved) => {
      setReadout(undefined);
      if (moved) set(next, true);
    }, "grabbing");
  };

  const dragCurve = (e: React.PointerEvent, key: "attackCurve" | "decayCurve" | "releaseCurve") => {
    e.stopPropagation();
    const start = env, k = scale();
    // Pulling the attack up, or the decay and release down, bows them out: a quick start.
    const sign = key === "attackCurve" ? -1 : 1;
    let next = start;
    startDrag(e, (_dx, dy) => {
      next = { ...start, [key]: round(clamp(start[key] + (sign * dy * k) / 50, -1, 1)) };
      setReadout(`${nameOf(key)} ${formatCurve(next[key])}`);
      set(next, false);
    }, (_dx, _dy, _ev, moved) => {
      setReadout(undefined);
      if (moved) set(next, true);
    }, "ns-resize");
  };

  const corner = (x: number, level: number, title: string, onDown: (e: React.PointerEvent) => void) => (
    <circle key={title} cx={x} cy={y(level)} r={5} fill="#111113" stroke={color} strokeWidth={2} className="cursor-grab hover:fill-[#2a2a30]" onPointerDown={onDown}>
      <title>{title}</title>
    </circle>
  );
  const curvePoint = (x0: number, x1: number, level: (p: number) => number, title: string, key: "attackCurve" | "decayCurve" | "releaseCurve") =>
    x1 - x0 > 14 && (
      <circle key={key} cx={(x0 + x1) / 2} cy={y(level(0.5))} r={3.5} fill={color} fillOpacity={0.6} className="cursor-ns-resize hover:fill-opacity-100"
        onPointerDown={(e) => dragCurve(e, key)} onDoubleClick={() => set({ ...env, [key]: 0 }, true)}>
        <title>{title}: drag up or down; double-click for straight</title>
      </circle>
    );

  const timeKnob = (key: TimeKey) => (
    <Knob size={28} label={nameOf(key)} title={nameOf(key)} value={timeToPosition(env[key])} min={0} max={1} defaultValue={timeToPosition(DEFAULT_ENVELOPE[key])}
      color={color} format={(p) => formatSeconds(positionToTime(p))}
      onChange={(p) => set({ ...env, [key]: positionToTime(p) }, false)} onCommit={(p) => set({ ...env, [key]: positionToTime(p) }, true)} />
  );
  const curveKnob = (key: "attackCurve" | "decayCurve" | "releaseCurve") => (
    <Knob size={20} label="Curve" title={`${nameOf(key)} (positive: fast, then easing)`} value={env[key]} min={-1} max={1} defaultValue={0} bipolar color={color}
      format={formatCurve}
      onChange={(v) => set({ ...env, [key]: round(v) }, false)} onCommit={(v) => set({ ...env, [key]: round(v) }, true)} />
  );

  return (
    <div className="flex flex-col gap-2">
      <div className={cn("relative rounded-md bg-bg ring-1 ring-line transition-opacity", dim && "opacity-50")}>
        <svg ref={svg} viewBox={`0 0 ${W} ${H}`} className="block w-full select-none">
          {[0.25, 0.5, 0.75].map((l) => <line key={l} x1={PAD} x2={W - PAD} y1={y(l)} y2={y(l)} stroke="#1f1f24" />)}
          <line x1={layout.decayEnd} x2={layout.decayEnd} y1={PAD} y2={H - PAD} stroke="#1f1f24" />
          <line x1={layout.sustainEnd} x2={layout.sustainEnd} y1={PAD} y2={H - PAD} stroke="#26262c" strokeDasharray="2 3" />
          <path d={`${line} L ${layout.releaseEnd} ${y(0)} L ${layout.start} ${y(0)} Z`} fill={color} fillOpacity={0.14} />
          <path d={line} fill="none" stroke={color} strokeWidth={2} strokeLinejoin="round" />
          {curvePoint(layout.delayEnd, layout.attackEnd, (p) => bend(p, env.attackCurve), "Attack curve", "attackCurve")}
          {curvePoint(layout.holdEnd, layout.decayEnd, (p) => 1 - (1 - env.sustain) * bend(p, env.decayCurve), "Decay curve", "decayCurve")}
          {curvePoint(layout.sustainEnd, layout.releaseEnd, (p) => env.sustain * (1 - bend(p, env.releaseCurve)), "Release curve", "releaseCurve")}
          {corner(layout.delayEnd, 0, "Delay", (e) => dragTime(e, "delay"))}
          {corner(layout.attackEnd, 1, "Attack", (e) => dragTime(e, "attack"))}
          {corner(layout.holdEnd, 1, "Hold", (e) => dragTime(e, "hold"))}
          {corner(layout.decayEnd, env.sustain, "Decay (and sustain, up and down)", (e) => dragTime(e, "decay", true))}
          {corner(layout.releaseEnd, 0, "Release", (e) => dragTime(e, "release"))}
          <PlayingDot env={env} layout={layout} y={y} color={color} />
        </svg>
        <div className="pointer-events-none absolute top-1.5 right-2 text-2xs text-dim tabular">{readout ?? label}</div>
      </div>
      <div className={cn("grid justify-items-center gap-y-1", extra ? "grid-cols-7" : "grid-cols-6")}>
        {timeKnob("delay")}
        {timeKnob("attack")}
        {timeKnob("hold")}
        {timeKnob("decay")}
        <Knob size={28} label="Sustain" value={env.sustain} min={0} max={1} defaultValue={1} color={color} format={(v) => `${Math.round(v * 100)}%`}
          onChange={(v) => set({ ...env, sustain: round(v) }, false)} onCommit={(v) => set({ ...env, sustain: round(v) }, true)} />
        {timeKnob("release")}
        {extra && <div className="row-span-2 self-center">{extra}</div>}
        <span />
        {curveKnob("attackCurve")}
        <span />
        {curveKnob("decayCurve")}
        <span />
        {curveKnob("releaseCurve")}
      </div>
    </div>
  );

  function nameOf(key: keyof Env) {
    return { delay: "Delay", attack: "Attack", hold: "Hold", decay: "Decay", sustain: "Sustain", release: "Release",
      attackCurve: "Attack curve", decayCurve: "Decay curve", releaseCurve: "Release curve" }[key];
  }
}

type Layout = ReturnType<typeof segments>;

function segments(env: Env) {
  const start = PAD;
  const delayEnd = start + timeToPosition(env.delay) * SEGMENT_W;
  const attackEnd = delayEnd + timeToPosition(env.attack) * SEGMENT_W;
  const holdEnd = attackEnd + timeToPosition(env.hold) * SEGMENT_W;
  const decayEnd = holdEnd + timeToPosition(env.decay) * SEGMENT_W;
  const sustainEnd = decayEnd + SUSTAIN_W;
  const releaseEnd = sustainEnd + timeToPosition(env.release) * SEGMENT_W;
  return { start, delayEnd, attackEnd, holdEnd, decayEnd, sustainEnd, releaseEnd };
}

/** A note played from here running through the envelope: along the segments in real time,
    resting on the sustain while held, then down the release. */
function PlayingDot({ env, layout, y, color }: { env: Env; layout: Layout; y: (level: number) => number; color: string }) {
  const { on, off } = useNotePlayed();
  const dot = useRef<SVGGElement>(null);
  const envRef = useRef(env);
  envRef.current = env;

  useEffect(() => {
    if (on === undefined) return;
    let frame = requestAnimationFrame(function tick() {
      const e = envRef.current, now = performance.now();
      const at = where(e, layout, (now - on) / 1000, off === undefined ? undefined : (off - on) / 1000, (now - (off ?? now)) / 1000);
      const g = dot.current;
      if (g) {
        g.style.display = at ? "block" : "none";
        if (at) g.setAttribute("transform", `translate(${at.x} ${y(at.level)})`);
      }
      if (at) frame = requestAnimationFrame(tick);
    });
    return () => cancelAnimationFrame(frame);
  }, [on, off]); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <g ref={dot} style={{ display: "none" }} className="pointer-events-none">
      <circle r={9} fill={color} opacity={0.25} />
      <circle r={4} fill="white" style={{ filter: `drop-shadow(0 0 4px ${color})` }} />
    </g>
  );
}

/** Where a note is on the drawn envelope: t seconds after it started, released after heldFor
    seconds (if it has been), sinceRelease seconds ago. Nothing once it's over. */
function where(env: Env, l: Layout, t: number, heldFor: number | undefined, sinceRelease: number) {
  const before = (time: number) => {
    let s = time;
    const pass = (length: number, x0: number, x1: number, level: (p: number) => number) => {
      if (s < length) return { x: x0 + ((x1 - x0) * s) / length, level: level(s / length) };
      s -= length;
      return undefined;
    };
    return pass(env.delay, l.start, l.delayEnd, () => 0)
      ?? pass(env.attack, l.delayEnd, l.attackEnd, (p) => bend(p, env.attackCurve))
      ?? pass(env.hold, l.attackEnd, l.holdEnd, () => 1)
      ?? pass(env.decay, l.holdEnd, l.decayEnd, (p) => 1 - (1 - env.sustain) * bend(p, env.decayCurve))
      ?? { x: l.decayEnd + Math.min(1, s / 0.6) * (l.sustainEnd - l.decayEnd), level: env.sustain };
  };

  if (heldFor === undefined) return before(t);
  if (env.release <= 0 || sinceRelease >= env.release) return undefined;
  const from = before(heldFor).level, p = sinceRelease / env.release;
  return { x: l.sustainEnd + p * (l.releaseEnd - l.sustainEnd), level: from * (1 - bend(p, env.releaseCurve)) };
}

function clamp(v: number, lo: number, hi: number) {
  return Math.min(hi, Math.max(lo, v));
}

function round(v: number) {
  return Math.round(v * 1000) / 1000;
}

function formatCurve(v: number) {
  return Math.abs(v) < 0.005 ? "straight" : `${v > 0 ? "+" : ""}${Math.round(v * 100)}%`;
}

export function formatSeconds(s: number) {
  return s >= 1 ? `${s.toFixed(2)} s` : s >= 0.01 ? `${Math.round(s * 1000)} ms` : `${(s * 1000).toFixed(1)} ms`;
}
