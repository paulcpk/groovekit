import type { Lfo } from "@groovekit/schema/project";
import { Knob, ToggleChip } from "@/components/controls";
import { cn } from "@/lib/cn";
import { useCanvas } from "@/features/effects/live";
import { formatSeconds, positionToTime, timeToPosition } from "./EnvelopeGraph";

export type LfoValues = Required<Omit<Lfo, "sync">> & { sync?: number };

export const DEFAULT_LFO: LfoValues = { shape: "sine", amount: 0, speed: 4, delay: 0, attack: 0, retrigger: true };

const SHAPES = ["sine", "triangle", "square", "saw", "random"] as const;

/** Note lengths a synced LFO can take a cycle over, in beats. */
const SYNC = [
  { beats: 16, label: "4 bars" }, { beats: 8, label: "2 bars" }, { beats: 4, label: "1 bar" }, { beats: 2, label: "1/2" },
  { beats: 1, label: "1/4" }, { beats: 2 / 3, label: "1/4 T" }, { beats: 0.5, label: "1/8" }, { beats: 1 / 3, label: "1/8 T" },
  { beats: 0.25, label: "1/16" }, { beats: 1 / 6, label: "1/16 T" }, { beats: 0.125, label: "1/32" },
];

/** Speeds from 0.01 to 50 Hz, spread evenly across a knob's turn. */
const speedFor = (p: number) => Math.round(0.01 * Math.pow(5000, p) * 1000) / 1000;
const speedPosition = (hz: number) => Math.log(hz / 0.01) / Math.log(5000);

/** The wave at a phase in cycles: the engine's (sampler::LfoSettings::wave). */
export function lfoWave(shape: LfoValues["shape"], phase: number) {
  const cycle = Math.floor(phase), f = phase - cycle;
  switch (shape) {
    case "sine": return Math.sin(2 * Math.PI * f);
    case "triangle": return f < 0.25 ? 4 * f : f < 0.75 ? 2 - 4 * f : 4 * f - 4;
    case "square": return f < 0.5 ? 1 : -1;
    case "saw": return f < 0.5 ? 2 * f : 2 * f - 2;
    case "random": {
      let h = Math.imul(cycle | 0, 2654435761) >>> 0;
      h = (h ^ (h >>> 15)) >>> 0;
      h = Math.imul(h, 2246822519) >>> 0;
      h = (h ^ (h >>> 13)) >>> 0;
      return (h & 0xffffff) / 0x800000 - 1;
    }
  }
}

/** One target's LFO: its shape, a live picture of it swinging, and its knobs. The amount is a
    share of the target's span, shown in the target's units. */
export function LfoPanel({ lfo, color, tempo, formatAmount, onChange, onCommit }: {
  lfo: LfoValues; color: string; tempo: number;
  formatAmount: (amount: number) => string;
  onChange: (lfo: LfoValues) => void; onCommit: (lfo: LfoValues) => void;
}) {
  const on = lfo.amount !== 0;
  const cyclesPerSecond = lfo.sync ? tempo / 60 / lfo.sync : lfo.speed;
  const syncIndex = lfo.sync ? nearestSync(lfo.sync) : 4;
  const set = (change: Partial<LfoValues>, commit: boolean) => (commit ? onCommit : onChange)({ ...lfo, ...change });

  // Two cycles scroll by at the LFO's speed (slowed down when they'd blur), the dot riding the wave.
  const canvas = useCanvas((g, w, h, now) => {
    g.clearRect(0, 0, w, h);
    const mid = h / 2, amp = (h / 2 - 6) * (on ? Math.max(0.12, Math.abs(lfo.amount)) : 0.5) * (lfo.amount < 0 ? -1 : 1);
    const shown = Math.min(cyclesPerSecond, 6);
    const phase = (now / 1000) * shown;

    g.strokeStyle = "#1f1f24";
    g.beginPath();
    g.moveTo(0, mid);
    g.lineTo(w, mid);
    g.stroke();

    g.beginPath();
    for (let x = 0; x <= w; x++) {
      const p = phase + (x / w) * 2 - 2;
      const yy = mid - lfoWave(lfo.shape, p) * amp;
      if (x) g.lineTo(x, yy);
      else g.moveTo(x, yy);
    }
    g.strokeStyle = color;
    g.globalAlpha = on ? 1 : 0.35;
    g.lineWidth = 2;
    g.lineJoin = "round";
    g.stroke();

    if (on) {
      const yy = mid - lfoWave(lfo.shape, phase) * amp;
      g.fillStyle = color;
      g.globalAlpha = 0.3;
      g.beginPath();
      g.arc(w - 1, yy, 8, 0, 2 * Math.PI);
      g.fill();
      g.globalAlpha = 1;
      g.fillStyle = "white";
      g.beginPath();
      g.arc(w - 1, yy, 3.5, 0, 2 * Math.PI);
      g.fill();
    }
    g.globalAlpha = 1;
  });

  return (
    <div className="flex flex-col gap-2">
      <div className="flex items-center gap-0.5">
        {SHAPES.map((shape) => (
          <button key={shape} type="button" title={shape[0].toUpperCase() + shape.slice(1)}
            className={cn("flex h-6 w-7 items-center justify-center rounded-[4px]", lfo.shape === shape ? "bg-control" : "hover:bg-control/60")}
            onClick={() => set({ shape }, true)}>
            <ShapeIcon shape={shape} color={lfo.shape === shape ? color : "#8d8d96"} />
          </button>
        ))}
        <span className="ml-auto text-2xs uppercase tracking-wide text-faint">LFO</span>
      </div>
      <div className={cn("overflow-hidden rounded-md bg-bg ring-1 ring-line", !on && "opacity-60")}>
        <canvas ref={canvas} className="block h-[60px] w-full" />
      </div>
      <div className="flex items-start justify-between">
        <Knob size={26} label="Delay" value={timeToPosition(lfo.delay)} min={0} max={1} defaultValue={0} color={color} format={(p) => formatSeconds(positionToTime(p))}
          onChange={(p) => set({ delay: positionToTime(p) }, false)} onCommit={(p) => set({ delay: positionToTime(p) }, true)} />
        <Knob size={26} label="Attack" value={timeToPosition(lfo.attack)} min={0} max={1} defaultValue={0} color={color} format={(p) => formatSeconds(positionToTime(p))}
          onChange={(p) => set({ attack: positionToTime(p) }, false)} onCommit={(p) => set({ attack: positionToTime(p) }, true)} />
        <Knob size={26} label="Amount" value={lfo.amount} min={-1} max={1} defaultValue={0} bipolar color={color} format={formatAmount}
          onChange={(v) => set({ amount: round(v) }, false)} onCommit={(v) => set({ amount: round(v) }, true)} />
        {lfo.sync ? (
          <Knob size={26} label="Speed" value={syncIndex} min={0} max={SYNC.length - 1} defaultValue={4} color={color} format={(v) => SYNC[Math.round(v)].label}
            onChange={(v) => set({ sync: SYNC[Math.round(v)].beats }, false)} onCommit={(v) => set({ sync: SYNC[Math.round(v)].beats }, true)} />
        ) : (
          <Knob size={26} label="Speed" value={speedPosition(lfo.speed)} min={0} max={1} defaultValue={speedPosition(4)} color={color}
            format={(p) => formatHz(speedFor(p))}
            onChange={(p) => set({ speed: speedFor(p) }, false)} onCommit={(p) => set({ speed: speedFor(p) }, true)} />
        )}
      </div>
      <div className="flex items-center justify-end gap-1.5">
        <ToggleChip on={!!lfo.sync} label="Tempo" title="Follow the song's tempo" onClick={() => set({ sync: lfo.sync ? undefined : 1 }, true)} />
        <ToggleChip on={lfo.retrigger} label="Retrigger" title="Each note starts the cycle; off, every note shares one running cycle"
          onClick={() => set({ retrigger: !lfo.retrigger }, true)} />
      </div>
    </div>
  );
}

function nearestSync(beats: number) {
  let best = 0;
  SYNC.forEach((s, i) => Math.abs(s.beats - beats) < Math.abs(SYNC[best].beats - beats) && (best = i));
  return best;
}

function formatHz(hz: number) {
  return hz >= 10 ? `${hz.toFixed(1)} Hz` : hz >= 1 ? `${hz.toFixed(2)} Hz` : `${hz.toFixed(3)} Hz`;
}

function round(v: number) {
  return Math.round(v * 1000) / 1000;
}

function ShapeIcon({ shape, color }: { shape: (typeof SHAPES)[number]; color: string }) {
  let d = "";
  for (let i = 0; i <= 32; i++) {
    const x = 2 + (i / 32) * 16, y = 7 - lfoWave(shape, (i / 32) * (shape === "random" ? 4 : 1) + (shape === "random" ? 3 : 0)) * 5;
    d += `${i ? "L" : "M"} ${x.toFixed(1)} ${y.toFixed(1)} `;
  }
  return (
    <svg width={20} height={14} viewBox="0 0 20 14">
      <path d={d} fill="none" stroke={color} strokeWidth={1.5} strokeLinejoin="round" />
    </svg>
  );
}
