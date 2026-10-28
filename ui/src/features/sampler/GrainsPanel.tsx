import type { Grains, SamplerLayer } from "@groovekit/schema/project";
import { useRef } from "react";
import { Knob, ToggleChip } from "@/components/controls";
import { font, useCanvas } from "@/features/effects/live";
import { useNotePlayed } from "@/features/shell/liveInput";
import { useWave } from "@/lib/peaks";

type Settings = Required<Grains>;

export const DEFAULT_GRAINS: Settings = { on: false, size: 0.08, density: 40, speed: 1, spray: 0.005, detune: 0, width: 0, shape: "smooth" };

/** Starting points: what each is for, and the settings it brings. */
const STARTS: { name: string; title: string; grains: Partial<Settings> }[] = [
  { name: "Pitch only", title: "Keys change the pitch, not the timing", grains: { size: 0.08, density: 40, speed: 1, spray: 0.005, detune: 0, width: 0, shape: "smooth" } },
  { name: "Half speed", title: "Twice as long, at the same pitch", grains: { size: 0.1, density: 35, speed: 0.5, spray: 0.01, detune: 0, width: 0.1, shape: "smooth" } },
  { name: "Freeze", title: "Holds still where it is: a texture for as long as the note", grains: { size: 0.2, density: 30, speed: 0, spray: 0.08, detune: 12, width: 0.8, shape: "smooth" } },
  { name: "Cloud", title: "Scattered, detuned and wide", grains: { size: 0.15, density: 60, speed: 0.3, spray: 0.25, detune: 25, width: 1, shape: "smooth" } },
  { name: "Stutter", title: "Few short grains with space between them", grains: { size: 0.03, density: 10, speed: 1, spray: 0, detune: 0, width: 0.3, shape: "pluck" } },
];

/** The engine's grain shapes (sampler::Grains::window). */
function grainWindow(shape: Settings["shape"], x: number) {
  x = Math.max(0, Math.min(1, x));
  if (shape === "flat") {
    const edge = Math.min(x, 1 - x) / 0.2;
    return edge >= 1 ? 1 : 0.5 - 0.5 * Math.cos(Math.PI * edge);
  }
  if (shape === "pluck") return Math.min(1, x / 0.02) * Math.exp(-4.5 * x) * Math.min(1, (1 - x) / 0.05);
  return 0.5 - 0.5 * Math.cos(2 * Math.PI * x);
}

interface Spark { at: number; born: number; pan: number; detune: number }

/** Granular playback for a layer: the part as a wave, a playhead moving through it at the grains'
    speed and grains glowing where they read (as they would for a note played at its root),
    a lane of what comes out (the grains' rise and fall, overlapping or with space between),
    then the controls. */
export function GrainsPanel({ layer, color, change, onPress, onRelease }: {
  layer: SamplerLayer; color: string; change: (changes: Partial<SamplerLayer>, commit?: boolean, label?: string) => void;
  onPress: () => void; onRelease: () => void;
}) {
  const g: Settings = { ...DEFAULT_GRAINS, ...layer.grains };
  const wave = useWave(layer.sample);
  const { on: noteOn, off: noteOff } = useNotePlayed();
  const settings = useRef({ g, layer, wave, noteOn, noteOff });
  settings.current = { g, layer, wave, noteOn, noteOff };
  const sim = useRef({ playhead: 0, untilGrain: 0, sparks: [] as Spark[], note: undefined as number | undefined, t: 0 });

  const set = (changes: Partial<Settings>, commit = true, label?: string) => change({ grains: { ...g, ...changes } }, commit, label);

  const canvas = useCanvas((ctx, w, h, now, dt) => {
    const { g, layer, wave, noteOn, noteOff } = settings.current;
    const s = sim.current;
    const top = 8, waveH = h - 64, mid = top + waveH / 2;
    const duration = wave?.durationSeconds ?? 1;
    const start = layer.start ?? 0, end = Math.min(duration, layer.end ?? duration);
    const part = Math.max(0.01, end - start);
    const x = (seconds: number) => 8 + (seconds / part) * (w - 16);
    s.t += dt / 1000;

    // A note played from here restarts the playhead; between notes it runs on as a preview.
    const playing = noteOn !== undefined && (noteOff === undefined || now - noteOff < 300);
    if (noteOn !== s.note) {
      s.note = noteOn;
      s.playhead = 0;
      s.sparks = [];
    }
    const looping = (layer.loop ?? "off") !== "off";
    s.playhead += (g.speed * dt) / 1000;
    if (s.playhead >= part) s.playhead = looping || !playing ? s.playhead % part : part;

    // New grains at the density, a little early or late, as the engine starts them.
    s.untilGrain -= dt / 1000;
    while (s.untilGrain <= 0) {
      if (s.playhead < part || looping) s.sparks.push({ at: s.playhead + g.spray * (2 * Math.random() - 1), born: s.t, pan: g.width * (2 * Math.random() - 1), detune: g.detune * (2 * Math.random() - 1) });
      s.untilGrain += (1 / g.density) * (0.85 + 0.3 * Math.random());
    }
    s.sparks = s.sparks.filter((p) => s.t - p.born < g.size).slice(-160);

    // The wave, as it plays (reversed if the layer is).
    ctx.fillStyle = "#0b0b0e";
    ctx.fillRect(0, 0, w, h);
    if (wave) {
      const columns = Math.floor(w - 16);
      ctx.fillStyle = g.on ? "rgba(255,255,255,0.16)" : "rgba(255,255,255,0.1)";
      for (let c = 0; c < columns; c++) {
        let t = start + (c / columns) * part;
        if (layer.reverse) t = end - (t - start);
        const i = Math.min(wave.min.length - 1, Math.max(0, Math.floor(t * wave.resolution)));
        const span = Math.max(1, Math.ceil((part / columns) * wave.resolution));
        let lo = 0, hi = 0;
        for (let k = i; k < Math.min(wave.min.length, i + span); k++) {
          lo = Math.min(lo, wave.min[k]);
          hi = Math.max(hi, wave.max[k]);
        }
        ctx.fillRect(8 + c, mid - hi * (waveH / 2), 1, Math.max(1, (hi - lo) * (waveH / 2)));
      }
    }

    // The grains: each over the stretch it reads, rising and falling with its shape, up or down
    // by its place in stereo.
    const alpha = (g.on ? 1 : 0.35) * (playing ? 1 : 0.55);
    ctx.globalCompositeOperation = "lighter";
    for (const p of s.sparks) {
      const progress = (s.t - p.born) / g.size;
      const level = grainWindow(g.shape, progress);
      // At least a few pixels wide, so grains still show on a long sample.
      const at = ((p.at % part) + part) % part;
      const x0 = x(at), x1 = Math.max(x0 + 14, x(at + g.size));
      const y = mid - p.pan * waveH * 0.32;
      const lift = level * waveH * 0.3;
      const hue = p.detune === 0 ? undefined : p.detune > 0 ? "#f5b26b" : "#7db8ff";
      const gradient = ctx.createLinearGradient(0, y - lift, 0, y + lift);
      gradient.addColorStop(0, "rgba(0,0,0,0)");
      gradient.addColorStop(0.5, hue ?? color);
      gradient.addColorStop(1, "rgba(0,0,0,0)");
      ctx.globalAlpha = alpha * (0.25 + 0.6 * level);
      ctx.fillStyle = gradient;
      ctx.beginPath();
      ctx.moveTo(x0, y);
      for (let i = 0; i <= 16; i++) ctx.lineTo(x0 + ((x1 - x0) * i) / 16, y - grainWindow(g.shape, i / 16) * lift);
      for (let i = 16; i >= 0; i--) ctx.lineTo(x0 + ((x1 - x0) * i) / 16, y + grainWindow(g.shape, i / 16) * lift);
      ctx.fill();
    }
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";

    // The playhead.
    const px = x(Math.min(part, s.playhead));
    ctx.fillStyle = playing ? "#ffffff" : "rgba(255,255,255,0.45)";
    ctx.fillRect(Math.round(px), top, 1.5, waveH);
    ctx.shadowColor = color;
    ctx.shadowBlur = playing ? 10 : 0;
    ctx.beginPath();
    ctx.arc(px, top + 3, 3, 0, 2 * Math.PI);
    ctx.fill();
    ctx.shadowBlur = 0;

    // What comes out over a second: each grain's rise and fall, overlapping or apart.
    const laneTop = h - 44, laneH = 26;
    ctx.fillStyle = "rgba(255,255,255,0.03)";
    ctx.fillRect(8, laneTop, w - 16, laneH);
    ctx.strokeStyle = g.on ? color : "rgba(255,255,255,0.3)";
    ctx.lineWidth = 1.2;
    const second = (sec: number) => 8 + sec * (w - 16);
    const offset = (s.t * 0.25) % (1 / g.density);
    for (let t0 = -g.size - offset; t0 < 1; t0 += 1 / g.density) {
      ctx.beginPath();
      for (let i = 0; i <= 24; i++) {
        const t = t0 + (i / 24) * g.size;
        if (t < 0 || t > 1) continue;
        ctx.lineTo(second(t), laneTop + laneH - grainWindow(g.shape, i / 24) * (laneH - 4));
      }
      ctx.stroke();
    }
    font(ctx, 9);
    ctx.fillStyle = "rgba(255,255,255,0.35)";
    const overlap = g.size * g.density;
    ctx.fillText(`out over 1 s · ${overlap >= 1 ? `${overlap.toFixed(1)} grains at once` : `${Math.round((1 / g.density - g.size) * 1000)} ms between grains`}`, 10, h - 6);
    ctx.textAlign = "right";
    ctx.fillText(g.speed === 0 ? "held still" : `${g.speed === 1 ? "own pace" : `${g.speed.toFixed(2)}×`} · ${(part / Math.max(0.0001, g.speed)).toFixed(2)} s`, w - 10, h - 6);
    ctx.textAlign = "left";
  });

  // Knob scales: sizes and densities spread over their octaves, speed with 1 in the middle,
  // spray finer near zero.
  const logScale = (min: number, max: number) => ({
    to: (v: number) => Math.log(v / min) / Math.log(max / min),
    from: (k: number) => min * Math.pow(max / min, k),
  });
  const sizeScale = logScale(0.005, 0.5), densityScale = logScale(1, 200);
  const speedTo = (v: number) => (v <= 1 ? v / 2 : 0.5 + (v - 1) / 6), speedFrom = (k: number) => (k <= 0.5 ? k * 2 : 1 + (k - 0.5) * 6);

  const knob = (field: keyof Settings, label: string, value: number, to: (v: number) => number, from: (k: number) => number, format: (v: number) => string,
    round: (v: number) => number, title: string, fallback: number) => (
    <Knob size={30} color={color} label={label} title={title} value={to(value)} min={0} max={1} defaultValue={to(fallback)}
      format={(k) => format(from(k))}
      onChange={(k) => set({ [field]: round(from(k)) }, false)} onCommit={(k) => set({ [field]: round(from(k)) }, true)} />
  );
  const ms = (s: number) => (s >= 1 ? `${s.toFixed(2)} s` : `${Math.round(s * 1000)} ms`);

  return (
    <div className="flex flex-col gap-3">
      <div className="flex flex-wrap items-center gap-1.5">
        <ToggleChip on={g.on} label="Granular" title="Plays the sample as grains: keys change the pitch but not the timing, and the speed changes the timing but not the pitch"
          onClick={() => set({ on: !g.on }, true, g.on ? "Play straight" : "Play as grains")} />
        <span className="text-2xs text-faint">{g.on ? "Keys change the pitch, speed the timing" : "Off: pitch and speed move together"}</span>
        <div className="flex-1" />
        {STARTS.map((s) => (
          <button key={s.name} type="button" title={s.title}
            onClick={() => set({ ...s.grains, on: true }, true, `Grains: ${s.name}`)}
            className="h-[18px] rounded-[3px] bg-control px-1.5 text-2xs text-dim hover:text-fg">{s.name}</button>
        ))}
      </div>
      <div className="overflow-hidden rounded-md ring-1 ring-line" title="Press to play it">
        <canvas ref={canvas} className="block w-full cursor-pointer select-none" style={{ height: 190 }}
          onPointerDown={(e) => {
            if (e.button !== 0) return;
            e.currentTarget.setPointerCapture(e.pointerId);
            onPress();
          }}
          onPointerUp={onRelease} onPointerCancel={onRelease} />
      </div>
      <div className={`flex flex-wrap items-start gap-2 ${g.on ? "" : "opacity-50"}`}>
        <div className="flex items-start gap-3 rounded-md bg-raised px-2.5 py-2">
          {knob("size", "Size", g.size, sizeScale.to, sizeScale.from, ms, (v) => Math.round(v * 1000) / 1000, "How long each grain lasts", DEFAULT_GRAINS.size)}
          {knob("density", "Density", g.density, densityScale.to, densityScale.from, (v) => `${v < 10 ? v.toFixed(1) : Math.round(v)}/s`, (v) => Math.round(v * 10) / 10, "Grains started a second: fewer leave space between them", DEFAULT_GRAINS.density)}
          {knob("speed", "Speed", g.speed, speedTo, speedFrom, (v) => (v === 0 ? "Held" : `${v.toFixed(2)}×`), (v) => (v < 0.02 ? 0 : Math.round(v * 100) / 100), "How fast the playhead moves through the sample: 0 holds still, 0.5 is twice as long", DEFAULT_GRAINS.speed)}
          {knob("spray", "Spray", g.spray, (v) => Math.sqrt(v), (k) => k * k, ms, (v) => Math.round(v * 1000) / 1000, "How far grains scatter around the playhead", DEFAULT_GRAINS.spray)}
        </div>
        <div className="flex items-start gap-3 rounded-md bg-raised px-2.5 py-2">
          {knob("detune", "Detune", g.detune, (v) => v / 100, (k) => k * 100, (v) => `±${Math.round(v)} ct`, Math.round, "Random detuning of each grain: thickens", 0)}
          {knob("width", "Width", g.width, (v) => v, (k) => k, (v) => `${Math.round(v * 100)}%`, (v) => Math.round(v * 100) / 100, "How widely grains spread in stereo", 0)}
          <div className="flex flex-col items-center gap-1">
            <div className="flex gap-px">
              {(["smooth", "flat", "pluck"] as const).map((shape) => (
                <ToggleChip key={shape} on={g.shape === shape} label={shape[0].toUpperCase() + shape.slice(1)}
                  title={{ smooth: "A smooth rise and fall", flat: "Full in the middle: more of the sound", pluck: "Struck, then dying away" }[shape]}
                  onClick={() => set({ shape }, true, "Grain shape")} />
              ))}
            </div>
            <svg width={84} height={26} className="mt-1">
              <path d={Array.from({ length: 41 }, (_, i) => `${i ? "L" : "M"} ${2 + i * 2} ${24 - grainWindow(g.shape, i / 40) * 20}`).join(" ")} fill="none" stroke={color} strokeWidth={1.5} />
            </svg>
            <div className="text-2xs text-dim">Shape</div>
          </div>
        </div>
      </div>
    </div>
  );
}
