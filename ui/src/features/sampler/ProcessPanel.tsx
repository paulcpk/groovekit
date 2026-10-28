import type { SampleProcessing, SamplerLayer } from "@groovekit/schema/project";
import { Knob } from "@/components/controls";
import { cn } from "@/lib/cn";
import { formatSeconds, positionToTime, timeToPosition } from "./EnvelopeGraph";

type Change = (changes: Partial<SamplerLayer>, commit?: boolean, label?: string) => void;

/** A round on/off button with a light, for the tools and effects worked into a sample. */
export function Led({ on, label, title, color, onClick }: { on: boolean; label: string; title?: string; color: string; onClick: () => void }) {
  return (
    <button type="button" title={title} onClick={onClick} onPointerDown={(e) => e.stopPropagation()}
      className={cn("flex items-center gap-1.5 rounded-full py-0.5 pr-2 pl-1 text-2xs transition-colors", on ? "text-fg" : "text-dim hover:text-fg")}>
      <span className="h-3 w-3 shrink-0 rounded-full ring-1 transition-[background,box-shadow]"
        style={on
          ? { background: `radial-gradient(circle at 35% 35%, #fff 0%, ${color} 55%)`, boxShadow: `0 0 6px ${color}`, borderColor: color }
          : { background: "radial-gradient(circle at 35% 35%, #3a3a40, #1b1b1f)", boxShadow: "inset 0 1px 2px rgba(0,0,0,0.6)" }} />
      {label}
    </button>
  );
}

function Group({ title, children, className }: { title: string; children: React.ReactNode; className?: string }) {
  return (
    <div className={cn("flex flex-col gap-1.5 rounded-md bg-raised px-2.5 py-2", className)}>
      <div className="text-2xs uppercase tracking-wide text-faint">{title}</div>
      {children}
    </div>
  );
}

const DEFAULTS: Required<Omit<SampleProcessing, "trimDb">> = {
  removeDc: false, stretchSemitones: 0, stretchLength: 1, pitchBend: 0, ringMix: 0, ringHz: 440, eq: 0, cutoff: 20000, resonance: 0,
  boostDb: 0, clip: false, reverb: 0, reverbSize: "small", stereoDelayMs: 0, fadeStereo: false,
};

/** Everything worked into a layer's sample, in groups: the tools (playing and preparing it),
    the effects baked into it, stretching (pitch and length apart) and transient shaping. Round
    lights switch, knobs set amounts; each change redraws the wave as it will play. */
export function ProcessPanel({ layer, color, change }: { layer: SamplerLayer; color: string; change: Change }) {
  const p = { ...DEFAULTS, ...layer.process };
  const set = (changes: Partial<SampleProcessing>, commit = true, label?: string) => change({ process: { ...layer.process, ...changes } }, commit, label);
  const loop = layer.loop ?? "off";

  /** A knob over a mapped scale: k in 0..1 (or -1..1) to the value and back. */
  const knob = (o: {
    label: string; title: string; value: number; fallback: number; to: (v: number) => number; from: (k: number) => number;
    format: (v: number) => string; apply: (v: number, commit: boolean) => void; bipolar?: boolean; min?: number; disabled?: boolean;
  }) => (
    <div className={cn(o.disabled && "pointer-events-none opacity-35")}>
      <Knob size={28} color={color} label={o.label} title={o.title} value={o.to(o.value)} min={o.min ?? 0} max={1} defaultValue={o.to(o.fallback)} bipolar={o.bipolar}
        format={(k) => o.format(o.from(k))} onChange={(k) => o.apply(o.from(k), false)} onCommit={(k) => o.apply(o.from(k), true)} />
    </div>
  );
  const linear = (min: number, max: number) => ({ to: (v: number) => (v - min) / (max - min), from: (k: number) => min + k * (max - min) });
  const log = (min: number, max: number) => ({ to: (v: number) => Math.log(v / min) / Math.log(max / min), from: (k: number) => min * Math.pow(max / min, k) });
  const r2 = (v: number) => Math.round(v * 100) / 100;
  const percent = (v: number) => (Math.abs(v) < 0.005 ? "Off" : `${v > 0 ? "+" : ""}${Math.round(v * 100)}%`);
  const hz = (v: number) => (v >= 1000 ? `${(v / 1000).toFixed(v >= 10000 ? 1 : 2)} kHz` : `${Math.round(v)} Hz`);
  const fade = (field: "fadeIn" | "fadeOut", label: string, title: string) => knob({
    label, title, value: timeToPosition(layer[field] ?? 0), fallback: 0, to: (v) => v, from: (k) => k,
    format: (k) => (positionToTime(k) ? formatSeconds(positionToTime(k)) : "Off"), apply: (k, commit) => change({ [field]: positionToTime(k) }, commit),
  });

  return (
    <div className="flex flex-col gap-2">
      <Group title="Tools">
        <div className="flex flex-wrap gap-x-1 gap-y-0.5">
          <Led color={color} on={!!layer.normalize} label="Normalize" title="Bring the part that plays up to a peak of 0 dB" onClick={() => change({ normalize: !layer.normalize }, true, "Normalize")} />
          <Led color={color} on={!!layer.reverse} label="Reverse" title="Play the part backwards" onClick={() => change({ reverse: !layer.reverse }, true, layer.reverse ? "Play forwards" : "Reverse")} />
          <Led color={color} on={!!layer.invert} label="Polarity" title="Flip the polarity" onClick={() => change({ invert: !layer.invert }, true, "Invert polarity")} />
          <Led color={color} on={!!layer.swapStereo} label="Swap L/R" title="Swap left and right" onClick={() => change({ swapStereo: !layer.swapStereo }, true, "Swap stereo")} />
          <Led color={color} on={p.removeDc} label="Remove DC" title="Take out a DC offset, which clicks and upsets effects" onClick={() => set({ removeDc: !p.removeDc }, true, "Remove DC offset")} />
          <Led color={color} on={p.fadeStereo} label="Fade stereo" title="Fade from the left side to the right over the sample" onClick={() => set({ fadeStereo: !p.fadeStereo }, true, "Fade stereo")} />
        </div>
        <div className="flex items-start gap-3">
          {knob({ label: "Length", title: "Shorten the sample: its end bends into a natural decay, so it never sounds cut off", value: layer.length ?? 1, fallback: 1,
            to: (v) => Math.sqrt(v), from: (k) => Math.max(0.01, k * k), format: (v) => (v >= 0.995 ? "Full" : `${Math.round(v * 100)}%`),
            apply: (v, commit) => change({ length: v >= 0.995 ? 1 : Math.round(v * 1000) / 1000 }, commit) })}
          {fade("fadeIn", "In", "Fade in")}
          {fade("fadeOut", "Out", "Fade out")}
          {knob({ label: "X-fade", title: "Crossfade where a loop comes round, so it's smooth (loop on)", value: layer.crossfade ?? 0, fallback: 0,
            ...linear(0, 0.5), format: (v) => (v < 0.001 ? "Off" : formatSeconds(v)), apply: (v, commit) => change({ crossfade: Math.round(v * 1000) / 1000 }, commit),
            disabled: loop !== "forward" })}
          {knob({ label: "Trim", title: "Cut off the end where it stays below this level: its quiet tail or noise", value: p.trimDb ?? -120, fallback: -120,
            ...linear(-120, -20), format: (v) => (v <= -119.5 ? "Off" : `${Math.round(v)} dB`),
            apply: (v, commit) => set({ trimDb: v <= -119.5 ? (undefined as never) : Math.round(v) }, commit) })}
        </div>
      </Group>
      <div className="flex flex-wrap items-stretch gap-2">
        <Group title="Effects" className="flex-1">
          <div className="flex flex-wrap items-start gap-3">
            <div className="flex flex-col items-center gap-1">
              {knob({ label: "Boost", title: "Gain worked into the sample", value: p.boostDb, fallback: 0, ...linear(-24, 24), bipolar: true,
                format: (v) => `${v > 0 ? "+" : ""}${v.toFixed(1)} dB`, apply: (v, commit) => set({ boostDb: Math.round(v * 10) / 10 }, commit) })}
              <Led color={color} on={p.clip} label="Clip" title="Clip at 0 dB: hard distortion" onClick={() => set({ clip: !p.clip }, true, "Clip")} />
            </div>
            {knob({ label: "EQ", title: "Darker (left) or brighter (right)", value: p.eq, fallback: 0, to: (v) => v, from: r2, min: -1, bipolar: true, format: percent,
              apply: (v, commit) => set({ eq: v }, commit) })}
            {knob({ label: "Cutoff", title: "A low-pass worked into the sample", value: p.cutoff, fallback: 20000, ...log(20, 20000),
              format: (v) => (v >= 19900 ? "Open" : hz(v)), apply: (v, commit) => set({ cutoff: v >= 19900 ? 20000 : Math.round(v) }, commit) })}
            {knob({ label: "Res", title: "The low-pass's peak", value: p.resonance, fallback: 0, ...linear(0, 1), format: (v) => `${Math.round(v * 100)}%`,
              apply: (v, commit) => set({ resonance: r2(v) }, commit) })}
            {knob({ label: "Ring", title: "Ring modulation: metallic, bell-like", value: p.ringMix, fallback: 0, ...linear(0, 1), format: (v) => (v < 0.005 ? "Off" : `${Math.round(v * 100)}%`),
              apply: (v, commit) => set({ ringMix: r2(v) }, commit) })}
            {knob({ label: "Ring Hz", title: "The ring modulator's frequency", value: p.ringHz, fallback: 440, ...log(20, 5000), format: hz,
              apply: (v, commit) => set({ ringHz: Math.round(v) }, commit), disabled: p.ringMix === 0 })}
            <div className="flex flex-col items-center gap-1">
              {knob({ label: "Reverb", title: "Reverb worked into the sample, with its tail", value: p.reverb, fallback: 0, ...linear(0, 1),
                format: (v) => (v < 0.005 ? "Off" : `${Math.round(v * 100)}%`), apply: (v, commit) => set({ reverb: r2(v) }, commit) })}
              <div className="flex gap-px">
                {(["small", "large"] as const).map((size) => (
                  <button key={size} type="button" title={size === "small" ? "Close, in a tight room" : "Far away, in a big space"}
                    onClick={() => set({ reverbSize: size }, true, "Reverb size")}
                    className={cn("h-4 rounded-[3px] px-1.5 text-[9px] font-semibold", p.reverbSize === size ? "bg-fg text-bg" : "bg-control text-dim hover:text-fg")}>
                    {size === "small" ? "A" : "B"}
                  </button>
                ))}
              </div>
            </div>
            {knob({ label: "Delay", title: "Delays one side for width: right of centre the left side, left of centre the right", value: p.stereoDelayMs, fallback: 0,
              ...linear(-30, 30), bipolar: true, format: (v) => (Math.abs(v) < 0.1 ? "Off" : `${v > 0 ? "L" : "R"} ${Math.abs(v).toFixed(1)} ms`),
              apply: (v, commit) => set({ stereoDelayMs: Math.round(v * 10) / 10 }, commit) })}
            {knob({ label: "Bend", title: "Bends the pitch down: right falls towards the end (a tape stop), left rises from below at the start", value: p.pitchBend, fallback: 0,
              to: (v) => v, from: r2, min: -1, bipolar: true, format: percent, apply: (v, commit) => set({ pitchBend: v }, commit) })}
          </div>
        </Group>
        <Group title="Stretch">
          <div className="flex items-start gap-3">
            {knob({ label: "Shift", title: "Pitch shift with the length kept (worked into the sample at full quality)", value: p.stretchSemitones, fallback: 0,
              ...linear(-24, 24), bipolar: true, format: (v) => (Math.abs(v) < 0.05 ? "0 st" : `${v > 0 ? "+" : ""}${v.toFixed(1)} st`),
              apply: (v, commit) => set({ stretchSemitones: Math.round(v * 10) / 10 }, commit) })}
            {knob({ label: "Time", title: "Length multiplied with the pitch kept (worked into the sample at full quality)", value: p.stretchLength, fallback: 1, ...log(0.25, 4), bipolar: true,
              format: (v) => `${v.toFixed(2)}×`, apply: (v, commit) => set({ stretchLength: Math.abs(v - 1) < 0.02 ? 1 : r2(v) }, commit) })}
          </div>
        </Group>
        <Group title="Transients">
          <div className="flex items-start gap-3">
            {knob({ label: "Punch", title: "Sharpens each hit's attack, or softens it", value: layer.punch ?? 0, fallback: 0, to: (v) => v, from: r2, min: -1, bipolar: true,
              format: percent, apply: (v, commit) => change({ punch: v }, commit) })}
            {knob({ label: "Tail", title: "Brings up what rings on after each hit, or tightens it", value: layer.tail ?? 0, fallback: 0, to: (v) => v, from: r2, min: -1, bipolar: true,
              format: percent, apply: (v, commit) => change({ tail: v }, commit) })}
          </div>
        </Group>
      </div>
    </div>
  );
}
