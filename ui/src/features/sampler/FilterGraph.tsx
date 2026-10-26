import type { AutomationTarget, SamplerFilter } from "@groovekit/schema/project";
import { useState } from "react";
import { Knob } from "@/components/controls";
import { startDrag } from "@/lib/drag";
import { formatHz, freqToX, qForResonance, resonanceForQ, svfGainDb, xToFreq } from "@/lib/filters";
import { cn } from "@/lib/cn";
import { Automatable } from "@/features/playlist/Automatable";

type Filter = Required<Omit<SamplerFilter, "envelope">>;

const W = 460, H = 150, TOP_DB = 30, BOTTOM_DB = -36;
// Where the cutoff starts when a filter is first switched on, rather than wide open.
const DEFAULT_CUTOFF = { off: 20000, lowpass: 2000, highpass: 200, bandpass: 1000 };
const GRID_HZ = [50, 100, 200, 500, 1000, 2000, 5000, 10000];

/** The sampler filter's response, drawn from the same maths as the engine. Drag the point:
    sideways for the cutoff, up and down for the resonance. The faint curve is where the
    envelope takes the cutoff at its peak. With a channel, right-clicking a knob automates it. */
export function FilterGraph({ filter, color, channel, onChange, onCommit }: {
  filter: SamplerFilter | undefined; color: string; channel?: string;
  onChange: (f: Partial<SamplerFilter>) => void; onCommit: (f: Partial<SamplerFilter>) => void;
}) {
  const saved: Filter = { type: "off", cutoff: 20000, resonance: 0, envelopeAmount: 0, ...filter };
  const [live, setLive] = useState<Partial<Filter> | null>(null);
  const f: Filter = { ...saved, ...live };
  const off = f.type === "off";
  const q = qForResonance(f.resonance);

  const y = (db: number) => ((TOP_DB - Math.min(TOP_DB, Math.max(BOTTOM_DB, db))) / (TOP_DB - BOTTOM_DB)) * H;
  const curve = (cutoff: number) => {
    let d = "";
    for (let px = 0; px <= W; px += 2) d += `${px ? "L" : "M"} ${px} ${y(svfGainDb(f.type, cutoff, q, xToFreq(px / W)))} `;
    return d;
  };
  const peakCutoff = Math.min(20000, Math.max(20, f.cutoff * Math.pow(2, f.envelopeAmount)));
  const nodeX = freqToX(f.cutoff) * W;
  const nodeY = y(20 * Math.log10(q));

  const set = (change: Partial<Filter>, commit: boolean) => {
    if (commit) {
      setLive(null);
      onCommit(change);
    } else {
      setLive((l) => ({ ...l, ...change }));
      onChange(change);
    }
  };

  return (
    <div className="flex flex-col gap-2">
      <div className="flex gap-1">
        {(["off", "lowpass", "highpass", "bandpass"] as const).map((type) => (
          <button key={type} type="button"
            className={cn("h-6 rounded-[4px] px-2.5 text-xs", f.type === type ? "bg-accent text-black" : "bg-control text-dim hover:text-fg")}
            onClick={() => set({ type, ...(f.type === "off" && f.cutoff >= 19000 ? { cutoff: DEFAULT_CUTOFF[type] } : {}) }, true)}>
            {{ off: "Off", lowpass: "Low-pass", highpass: "High-pass", bandpass: "Band-pass" }[type]}
          </button>
        ))}
      </div>
      <div className={cn("relative rounded-md bg-bg ring-1 ring-line", off && "opacity-60")}>
        <svg viewBox={`0 0 ${W} ${H}`} className="block w-full">
          {GRID_HZ.map((hz) => <line key={hz} x1={freqToX(hz) * W} x2={freqToX(hz) * W} y1={0} y2={H} stroke="#1f1f24" />)}
          <line x1={0} x2={W} y1={y(0)} y2={y(0)} stroke="#2c2c31" />
          {GRID_HZ.filter((hz) => [100, 1000, 10000].includes(hz)).map((hz) => (
            <text key={hz} x={freqToX(hz) * W + 3} y={H - 4} fontSize={9} fill="#5b5b64">{formatHz(hz)}</text>
          ))}
          {!off && f.envelopeAmount !== 0 && <path d={curve(peakCutoff)} fill="none" stroke={color} strokeOpacity={0.35} strokeWidth={1.5} strokeDasharray="3 3" />}
          <path d={`${curve(f.cutoff)} L ${W} ${H} L 0 ${H} Z`} fill={color} fillOpacity={off ? 0.04 : 0.14} />
          <path d={curve(f.cutoff)} fill="none" stroke={color} strokeWidth={2} />
          {!off && (
            <circle cx={nodeX} cy={nodeY} r={6} fill="#111113" stroke={color} strokeWidth={2} className="cursor-grab"
              onPointerDown={(e) => {
                const svg = (e.currentTarget.ownerSVGElement as SVGSVGElement).getBoundingClientRect();
                const scale = W / svg.width;
                const x0 = nodeX, y0 = nodeY;
                let last: Partial<Filter> = {};
                startDrag(e, (dx, dy) => {
                  const db = TOP_DB - ((y0 + dy * scale) / H) * (TOP_DB - BOTTOM_DB);
                  last = { cutoff: Math.round(xToFreq((x0 + dx * scale) / W)), resonance: Math.round(resonanceForQ(Math.pow(10, db / 20)) * 100) / 100 };
                  set(last, false);
                }, (_dx, _dy, _e, moved) => moved && set(last, true), "grabbing");
              }}>
              <title>Drag: sideways for the cutoff, up and down for the resonance</title>
            </circle>
          )}
        </svg>
      </div>
      <div className={cn("flex justify-around", off && "pointer-events-none opacity-40")}>
        <Automated channel={channel} param="filterCutoff" value={f.cutoff}>
          <Knob size={28} label="Cutoff" value={freqToX(f.cutoff)} min={0} max={1} defaultValue={1}
            format={(v) => formatHz(xToFreq(v))}
            onChange={(v) => set({ cutoff: Math.round(xToFreq(v)) }, false)} onCommit={(v) => set({ cutoff: Math.round(xToFreq(v)) }, true)} />
        </Automated>
        <Automated channel={channel} param="filterResonance" value={f.resonance}>
          <Knob size={28} label="Resonance" value={f.resonance} min={0} max={1} defaultValue={0}
            format={(v) => `${Math.round(v * 100)}%`}
            onChange={(v) => set({ resonance: Math.round(v * 100) / 100 }, false)} onCommit={(v) => set({ resonance: Math.round(v * 100) / 100 }, true)} />
        </Automated>
        <Automated channel={channel} param="filterEnvelopeAmount" value={f.envelopeAmount}>
          <Knob size={28} label="Env amount" value={f.envelopeAmount} min={-8} max={8} defaultValue={0} bipolar
            format={(v) => `${v > 0 ? "+" : ""}${v.toFixed(1)} oct`}
            onChange={(v) => set({ envelopeAmount: Math.round(v * 10) / 10 }, false)} onCommit={(v) => set({ envelopeAmount: Math.round(v * 10) / 10 }, true)} />
        </Automated>
      </div>
    </div>
  );
}

/** A knob that, on a channel's filter, can be automated. */
function Automated({ channel, param, value, children }: { channel?: string; param: string; value: number; children: React.ReactNode }) {
  if (!channel) return <>{children}</>;
  const target: AutomationTarget = { scope: "channel", channel, param };
  return <Automatable target={target} value={value}>{children}</Automatable>;
}
