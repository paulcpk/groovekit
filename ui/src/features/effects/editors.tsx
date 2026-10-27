// Windows for our effects: a picture of what each does, drawn from its settings (and, where it
// helps, from the live output), above its controls.

import type { BuiltinEffect, Insert } from "@groovekit/schema/project";
import { useMemo } from "react";
import { useCatalogue } from "@/lib/catalogue";
import { useEngine } from "@/state/engine";
import { ParamKnob, paramValue } from "./params";
import { useEffectWaveform } from "./useEffectOutput";

export interface EditorProps { insert: Insert; effect: BuiltinEffect }

const W = 640, H = 170;
export const ACCENT = "#f2a33a", WET = "#58b7d6", FAINT = "#5b5b64";

export function useParams({ effect }: EditorProps) {
  const catalogue = useCatalogue();
  return (id: string) => paramValue(effect, id, catalogue?.[effect.kind]?.params[id]);
}

export function Screen({ children, label }: { children: React.ReactNode; label?: string }) {
  return (
    <div className="relative overflow-hidden rounded-md bg-[#0c0c0e] ring-1 ring-line">
      {label && <div className="pointer-events-none absolute top-1.5 right-2 text-2xs text-faint tabular">{label}</div>}
      <svg viewBox={`0 0 ${W} ${H}`} className="block w-full select-none">{children}</svg>
    </div>
  );
}

export function Knobs({ props, ids, names = {} }: { props: EditorProps; ids: string[]; names?: Record<string, string> }) {
  return (
    <div className="flex flex-wrap items-start justify-around gap-y-3 rounded-md bg-raised px-2 py-3">
      {ids.map((id) => <ParamKnob key={id} insert={props.insert} effect={props.effect} id={id} name={names[id]} />)}
    </div>
  );
}

function seconds(s: number) {
  return s >= 1 ? `${s.toFixed(2)} s` : `${Math.round(s * 1000)} ms`;
}

//==============================================================================
const SYNC_QUARTERS = [0, 0.125, 0.25, 0.375, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 2, 4];

/** The repeats on a time line: each one quieter by the feedback, bouncing between the sides
    when ping-pong is on (left above the line, right below). */
export function DelayEditor(props: EditorProps) {
  const p = useParams(props);
  const tempo = useEngine((s) => s.doc?.tempo ?? 120);
  const quarters = SYNC_QUARTERS[Math.round(p("sync"))] ?? 0;
  const delay = quarters > 0 ? (quarters * 60) / tempo : p("timeMs") / 1000;
  const feedback = p("feedback") / 100;
  const pingPong = Math.round(p("pingPong")) === 1;
  const span = Math.max(1, Math.min(8, delay * 9));
  const x = (t: number) => 14 + (t / span) * (W - 28);
  const mid = H / 2;

  const taps: { t: number; level: number; side: number }[] = [];
  for (let n = 1, level = 1; n <= 24 && n * delay <= span && level > 0.01; n++, level *= feedback)
    taps.push({ t: n * delay, level, side: pingPong ? (n % 2 ? -1 : 1) : 0 });

  return (
    <div className="flex flex-col gap-2">
      <Screen label={`${seconds(delay)} · ${taps.length} repeats shown`}>
        <line x1={0} x2={W} y1={mid} y2={mid} stroke="#2c2c31" />
        {Array.from({ length: Math.floor(span / delay) + 1 }, (_, i) => <line key={i} x1={x(i * delay)} x2={x(i * delay)} y1={8} y2={H - 8} stroke="#17171b" />)}
        <rect x={x(0) - 3} y={mid - 60} width={6} height={120} rx={2} fill={FAINT} />
        {taps.map((tap, i) => {
          const h = tap.level * 60;
          const y = tap.side < 0 ? mid - h : tap.side > 0 ? mid : mid - h;
          return <rect key={i} x={x(tap.t) - 3} y={y} width={6} height={tap.side === 0 ? h * 2 : h} rx={2} fill={tap.side > 0 ? WET : ACCENT} fillOpacity={0.35 + 0.65 * tap.level} />;
        })}
        {pingPong && <><text x={6} y={16} fontSize={9} fill={ACCENT}>L</text><text x={6} y={H - 8} fontSize={9} fill={WET}>R</text></>}
      </Screen>
      <Knobs props={props} ids={["sync", "timeMs", "feedback", "pingPong", "lowCut", "highCut", "width"]} names={{ timeMs: "Time", pingPong: "Ping-pong" }} />
      {quarters > 0 && <div className="text-2xs text-faint">Synced to the song's tempo; Time is used when sync is off.</div>}
    </div>
  );
}

//==============================================================================
/** The tail's shape: the pre-delay gap, early reflections, then the decay to -60 dB, with the
    highs (dashed) dying sooner the more it's damped. */
export function ReverbEditor(props: EditorProps) {
  const p = useParams(props);
  const pre = p("preDelayMs") / 1000, decay = p("decay"), size = p("size") / 100, damping = p("damping");
  const span = pre + decay * 1.1;
  const x = (t: number) => 10 + (t / span) * (W - 20);
  const y = (db: number) => 14 + (-db / 66) * (H - 28);
  // Highs lose more per pass the lower the damping; roughly how much sooner they're gone.
  const highDecay = decay * Math.min(1, Math.max(0.15, Math.log(damping / 1000) / Math.log(20)));

  const noise = useMemo(() => {
    let seed = 7;
    return Array.from({ length: 220 }, () => ((seed = (seed * 16807) % 2147483647) / 2147483647));
  }, []);

  const cloud = noise.map((r, i) => {
    const t = pre + (i / noise.length) * decay * 1.1;
    const level = (-60 * (t - pre)) / decay - r * 12;
    return `${i ? "L" : "M"} ${x(t)} ${y(Math.max(-66, level))}`;
  }).join(" ");
  const reflections = Array.from({ length: 7 }, (_, i) => pre + ((i + 1) * 0.012 + noise[i] * 0.01) * (0.4 + size * 2.6));

  return (
    <div className="flex flex-col gap-2">
      <Screen label={`${seconds(decay)} to -60 dB`}>
        {[-12, -24, -36, -48, -60].map((db) => <line key={db} x1={0} x2={W} y1={y(db)} y2={y(db)} stroke="#17171b" />)}
        <path d={`${cloud} L ${x(span)} ${y(-66)} L ${x(pre)} ${y(-66)} Z`} fill={WET} fillOpacity={0.18} />
        <path d={`M ${x(pre)} ${y(0)} L ${x(pre + decay)} ${y(-60)}`} stroke={WET} strokeWidth={2} />
        <path d={`M ${x(pre)} ${y(-3)} L ${x(pre + highDecay)} ${y(-60)}`} stroke={ACCENT} strokeWidth={1.5} strokeDasharray="4 3" />
        {reflections.map((t, i) => <line key={i} x1={x(t)} x2={x(t)} y1={y(-4 - i * 3)} y2={y(-66)} stroke="#d6d6db" strokeOpacity={0.6} />)}
        <line x1={x(0)} x2={x(0)} y1={y(0)} y2={y(-66)} stroke={FAINT} strokeWidth={3} />
        {pre > 0.005 && <text x={x(pre / 2)} y={y(-62)} fontSize={9} textAnchor="middle" fill={FAINT}>pre</text>}
        <text x={W - 12} y={y(-50)} fontSize={9} textAnchor="end" fill={ACCENT}>highs</text>
      </Screen>
      <Knobs props={props} ids={["preDelayMs", "size", "decay", "damping", "lowCut", "diffusion", "width"]} names={{ preDelayMs: "Pre-delay", lowCut: "Low cut" }} />
    </div>
  );
}

//==============================================================================
/** The three voices' delays sweeping over two seconds: depth is how far they move, rate how
    fast; the right side (dashed) runs a quarter cycle on. */
export function ChorusEditor(props: EditorProps) {
  const p = useParams(props);
  const rate = p("rate"), depth = p("depthMs"), base = p("delayMs"), width = p("width") / 100;
  const maxMs = 42;
  const y = (ms: number) => H - 12 - (ms / maxMs) * (H - 24);
  const voice = (v: number, offset: number) => {
    let d = "";
    for (let px = 0; px <= W; px += 4) {
      const t = (px / W) * 2;
      const lfo = Math.sin(2 * Math.PI * (t * rate + v / 3 + offset));
      d += `${px ? "L" : "M"} ${px} ${y(base + depth * 0.5 * (1 + lfo))} `;
    }
    return d;
  };
  const colors = [ACCENT, WET, "#c47ef0"];

  return (
    <div className="flex flex-col gap-2">
      <Screen label={`${rate.toFixed(2)} Hz · 2 s shown`}>
        <line x1={0} x2={W} y1={y(base)} y2={y(base)} stroke="#2c2c31" />
        {[0, 1, 2].map((v) => (
          <g key={v}>
            <path d={voice(v, 0)} fill="none" stroke={colors[v]} strokeWidth={1.8} />
            {width > 0 && <path d={voice(v, 0.25 * width)} fill="none" stroke={colors[v]} strokeOpacity={0.45} strokeWidth={1.2} strokeDasharray="4 3" />}
          </g>
        ))}
      </Screen>
      <Knobs props={props} ids={["rate", "depthMs", "delayMs", "feedback", "width"]} names={{ depthMs: "Depth", delayMs: "Delay" }} />
    </div>
  );
}

//==============================================================================
/** The shaper as in the engine (effects::DistortionPlugin::shape). */
export function distortionShape(type: number, x: number, driveDb: number) {
  const drive = Math.pow(10, driveDb / 20), input = x * drive;
  switch (type) {
    case 1: return Math.max(-1, Math.min(1, input));
    case 2: return Math.tanh(input + 0.25) - Math.tanh(0.25);
    case 3: return Math.sin((Math.PI / 2) * input);
    case 4: {
      const levels = Math.pow(2, 12 + (driveDb / 48) * (2 - 12));
      return Math.round(Math.max(-1, Math.min(1, x)) * levels) / levels;
    }
    default: return Math.tanh(input);
  }
}

/** The transfer curve (what each input level becomes), a sine through it, and the live output. */
export function DistortionEditor(props: EditorProps) {
  const p = useParams(props);
  const type = Math.round(p("type")), drive = p("driveDb"), out = Math.pow(10, p("outputDb") / 20);
  const wave = useEffectWaveform(props.insert.id, props.effect.id, 1024);
  const half = W / 2;

  const curve = (() => {
    let d = "";
    for (let i = 0; i <= 120; i++) {
      const x = -1 + (2 * i) / 120;
      d += `${i ? "L" : "M"} ${10 + ((x + 1) / 2) * (H - 20)} ${H / 2 - distortionShape(type, x, drive) * (H / 2 - 10)} `;
    }
    return d;
  })();

  const sine = (shaped: boolean) => {
    let d = "";
    for (let i = 0; i <= 160; i++) {
      const x = 0.9 * Math.sin((2 * Math.PI * 2 * i) / 160);
      const v = shaped ? distortionShape(type, x, drive) * out : x;
      d += `${i ? "L" : "M"} ${H + 10 + (i / 160) * (half - H - 10)} ${H / 2 - Math.max(-1.2, Math.min(1.2, v)) * (H / 2 - 14)} `;
    }
    return d;
  };

  // Scaled to its own peak, so quiet signals still show their shape.
  const peak = Math.max(0.05, ...wave.map(Math.abs));
  const live = wave.length
    ? wave.map((v, i) => `${i ? "L" : "M"} ${half + 10 + (i / wave.length) * (half - 20)} ${H / 2 - (v / peak) * (H / 2 - 14)}`).join(" ")
    : "";

  return (
    <div className="flex flex-col gap-2">
      <Screen>
        <rect x={10} y={10} width={H - 20} height={H - 20} fill="none" stroke="#1f1f24" />
        <line x1={10} x2={H - 10} y1={H / 2} y2={H / 2} stroke="#1f1f24" />
        <line x1={H / 2} x2={H / 2} y1={10} y2={H - 10} stroke="#1f1f24" />
        <path d={curve} fill="none" stroke={ACCENT} strokeWidth={2} />
        <text x={14} y={22} fontSize={9} fill={FAINT}>curve</text>
        <path d={sine(false)} fill="none" stroke={FAINT} strokeWidth={1} />
        <path d={sine(true)} fill="none" stroke={ACCENT} strokeWidth={1.8} />
        <text x={H + 14} y={22} fontSize={9} fill={FAINT}>a sine through it</text>
        <line x1={half} x2={half} y1={10} y2={H - 10} stroke="#1f1f24" />
        <line x1={half + 10} x2={W - 10} y1={H / 2} y2={H / 2} stroke="#1f1f24" />
        {live && <path d={live} fill="none" stroke={WET} strokeWidth={1.2} />}
        <text x={half + 14} y={22} fontSize={9} fill={FAINT}>live output</text>
      </Screen>
      <Knobs props={props} ids={["type", "driveDb", "tone", "outputDb"]} names={{ driveDb: "Drive", outputDb: "Output" }} />
    </div>
  );
}
