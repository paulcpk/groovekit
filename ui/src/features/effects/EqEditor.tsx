import type { BuiltinEffect, Insert } from "@groovekit/schema/project";
import { useMemo, useState } from "react";
import { DragNumber } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuTrigger } from "@/components/menu";
import { useCatalogue } from "@/lib/catalogue";
import { cn } from "@/lib/cn";
import { startDrag } from "@/lib/drag";
import { EQ_BAND_LABELS, EQ_BAND_TYPES, eqBandGainDb, formatHz, freqToX, xToFreq, type EqBandType } from "@/lib/filters";
import { useLiveControl } from "@/lib/liveControl";
import { useEffectSpectrum } from "./useEffectOutput";

const W = 640, H = 240, RANGE_DB = 24, BANDS = 7;
const SPECTRUM_TOP = 0, SPECTRUM_BOTTOM = -84;
const COLORS = ["#ef6f6c", "#f2a33a", "#e8d44d", "#6fd08c", "#58b7d6", "#7c8cf8", "#c47ef0"];
const GRID_HZ = [30, 50, 100, 200, 500, 1000, 2000, 5000, 10000];

interface Band { type: EqBandType; freq: number; gainDb: number; q: number }

/** The seven-band EQ: its response over the live spectrum of what comes out, with a point per
    band to drag (sideways for frequency, up and down for gain, the wheel for width) and the
    exact values below. */
export function EqEditor({ insert, effect }: { insert: Insert; effect: BuiltinEffect }) {
  const catalogue = useCatalogue();
  const spectrum = useEffectSpectrum(insert.id, effect.id);
  const [live, setLive] = useState<Record<string, number>>({});
  const [selected, setSelected] = useState(0);
  const control = useLiveControl<Record<string, number>>((params) => ({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { params } }), "EQ");

  const param = (id: string) => live[id] ?? effect.params?.[id] ?? catalogue?.eq?.params[id]?.default ?? 0;
  const bands: Band[] = Array.from({ length: BANDS }, (_, b) => ({
    type: EQ_BAND_TYPES[Math.round(param(`band${b + 1}Type`))] ?? "off",
    freq: param(`band${b + 1}Freq`), gainDb: param(`band${b + 1}GainDb`), q: param(`band${b + 1}Q`),
  }));

  const set = (params: Record<string, number>, commit: boolean) => {
    if (commit) {
      setLive({});
      control.onCommit(params);
    } else {
      setLive((l) => ({ ...l, ...params }));
      control.onChange(params);
    }
  };

  const y = (db: number) => H / 2 - (db / RANGE_DB) * (H / 2 - 8);
  const spectrumY = (db: number) => ((SPECTRUM_TOP - Math.max(SPECTRUM_BOTTOM, Math.min(SPECTRUM_TOP, db))) / (SPECTRUM_TOP - SPECTRUM_BOTTOM)) * H;

  const total = useMemo(() => {
    let d = "";
    for (let px = 0; px <= W; px += 2) {
      const f = xToFreq(px / W);
      const db = bands.reduce((sum, b) => sum + eqBandGainDb(b.type, b.freq, b.gainDb, b.q, f), 0);
      d += `${px ? "L" : "M"} ${px} ${y(Math.max(-RANGE_DB - 6, Math.min(RANGE_DB + 6, db)))} `;
    }
    return d;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [JSON.stringify(bands)]);

  const bandCurve = (b: Band) => {
    let d = "";
    for (let px = 0; px <= W; px += 4) d += `${px ? "L" : "M"} ${px} ${y(Math.max(-RANGE_DB - 6, Math.min(RANGE_DB + 6, eqBandGainDb(b.type, b.freq, b.gainDb, b.q, xToFreq(px / W)))))} `;
    return d;
  };

  const spectrumPath = spectrum.length
    ? spectrum.map((db, i) => `${i ? "L" : "M"} ${((i + 0.5) / spectrum.length) * W} ${spectrumY(db)}`).join(" ") + ` L ${W} ${H} L 0 ${H} Z`
    : "";

  const hasGain = (t: EqBandType) => t === "peak" || t === "lowShelf" || t === "highShelf";

  const dragBand = (e: React.PointerEvent, b: number) => {
    e.stopPropagation();
    setSelected(b);
    const svg = (e.currentTarget as SVGElement).ownerSVGElement!.getBoundingClientRect();
    const scale = W / svg.width;
    const start = bands[b];
    const x0 = freqToX(start.freq) * W;
    let last: Record<string, number> = {};
    startDrag(e, (dx, dy) => {
      last = { [`band${b + 1}Freq`]: Math.round(xToFreq((x0 + dx * scale) / W)) };
      if (hasGain(start.type)) last[`band${b + 1}GainDb`] = Math.round(Math.max(-RANGE_DB, Math.min(RANGE_DB, start.gainDb - (dy * scale / (H / 2 - 8)) * RANGE_DB)) * 10) / 10;
      set(last, false);
    }, (_dx, _dy, _ev, moved) => moved && set(last, true), "grabbing");
  };

  const sb = bands[selected];
  const n = selected + 1;

  return (
    <div className="flex flex-col gap-2">
      <div className="relative overflow-hidden rounded-md bg-[#0c0c0e] ring-1 ring-line">
        <svg viewBox={`0 0 ${W} ${H}`} className="block w-full select-none">
          <defs>
            <linearGradient id="eq-spectrum" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0" stopColor="#8d8d96" stopOpacity={0.35} />
              <stop offset="1" stopColor="#8d8d96" stopOpacity={0.04} />
            </linearGradient>
          </defs>
          {spectrumPath && <path d={spectrumPath} fill="url(#eq-spectrum)" />}
          {GRID_HZ.map((hz) => <line key={hz} x1={freqToX(hz) * W} x2={freqToX(hz) * W} y1={0} y2={H} stroke="#1c1c21" />)}
          {[-18, -12, -6, 6, 12, 18].map((db) => <line key={db} x1={0} x2={W} y1={y(db)} y2={y(db)} stroke="#1c1c21" />)}
          <line x1={0} x2={W} y1={y(0)} y2={y(0)} stroke="#2c2c31" />
          {[100, 1000, 10000].map((hz) => <text key={hz} x={freqToX(hz) * W + 3} y={H - 5} fontSize={9} fill="#5b5b64">{formatHz(hz)}</text>)}
          {[-12, 12].map((db) => <text key={db} x={4} y={y(db) - 3} fontSize={9} fill="#5b5b64">{db > 0 ? "+" : ""}{db} dB</text>)}
          {sb.type !== "off" && <path d={bandCurve(sb)} fill="none" stroke={COLORS[selected]} strokeOpacity={0.45} strokeWidth={1.5} />}
          <path d={total} fill="none" stroke="#f0f0f3" strokeWidth={2} />
          {bands.map((b, i) => {
            const cx = freqToX(b.freq) * W;
            const cy = hasGain(b.type) ? y(b.gainDb) : y(0);
            return (
              <ContextMenu key={i}>
                <ContextMenuTrigger asChild>
                  <g className="cursor-grab"
                    onPointerDown={(e) => e.button === 0 && dragBand(e, i)}
                    onDoubleClick={() => (b.type === "off" ? set({ [`band${i + 1}Type`]: EQ_BAND_TYPES.indexOf(defaultType(i)) }, true) : hasGain(b.type) && set({ [`band${i + 1}GainDb`]: 0 }, true))}
                    onWheel={(e) => set({ [`band${i + 1}Q`]: Math.round(Math.max(0.1, Math.min(18, b.q * Math.pow(1.1, -Math.sign(e.deltaY)))) * 100) / 100 }, true)}>
                    <circle cx={cx} cy={cy} r={11} fill="transparent" />
                    <circle cx={cx} cy={cy} r={b.type === "off" ? 4 : 7} fill={b.type === "off" ? "#18181b" : COLORS[i]} fillOpacity={b.type === "off" ? 1 : 0.9}
                      stroke={i === selected ? "#ffffff" : b.type === "off" ? "#5b5b64" : "#0c0c0e"} strokeWidth={i === selected ? 2 : 1.5} />
                    {b.type !== "off" && <text x={cx} y={cy + 3.2} fontSize={9} fontWeight={700} textAnchor="middle" fill="#111113" className="pointer-events-none">{i + 1}</text>}
                    <title>{`Band ${i + 1}: ${EQ_BAND_LABELS[b.type]} · ${formatHz(b.freq)}${hasGain(b.type) ? ` · ${b.gainDb.toFixed(1)} dB` : ""} · Q ${b.q.toFixed(2)}\nDrag to move; wheel for width; double-click ${b.type === "off" ? "to turn on" : "to reset the gain"}; right-click for the type`}</title>
                  </g>
                </ContextMenuTrigger>
                <ContextMenuContent>
                  {EQ_BAND_TYPES.filter((t) => t !== "off").map((t) => (
                    <ContextMenuItem key={t} onSelect={() => set({ [`band${i + 1}Type`]: EQ_BAND_TYPES.indexOf(t) }, true)}>{b.type === t ? "✓ " : ""}{EQ_BAND_LABELS[t]}</ContextMenuItem>
                  ))}
                  <ContextMenuSeparator />
                  <ContextMenuItem onSelect={() => set({ [`band${i + 1}Type`]: 0 }, true)}>{b.type === "off" ? "✓ " : ""}Off</ContextMenuItem>
                </ContextMenuContent>
              </ContextMenu>
            );
          })}
        </svg>
      </div>

      <div className="flex items-center gap-1">
        {bands.map((b, i) => (
          <button key={i} type="button" onClick={() => setSelected(i)}
            className={cn("flex h-6 flex-1 items-center justify-center gap-1 rounded-[4px] text-2xs", i === selected ? "bg-hover text-fg" : "bg-control text-dim hover:text-fg", b.type === "off" && "opacity-50")}>
            <span className="h-2 w-2 rounded-full" style={{ background: COLORS[i] }} />{i + 1}
          </button>
        ))}
      </div>

      <div className="grid grid-cols-[auto_1fr_1fr_1fr] items-center gap-x-3 gap-y-1 rounded-md bg-raised px-3 py-2 text-xs">
        <select value={sb.type} onChange={(e) => set({ [`band${n}Type`]: EQ_BAND_TYPES.indexOf(e.target.value as EqBandType) }, true)}
          className="h-6 rounded-[3px] bg-control px-1 text-xs text-fg outline-none">
          {EQ_BAND_TYPES.map((t) => <option key={t} value={t}>{EQ_BAND_LABELS[t]}</option>)}
        </select>
        <label className="flex items-center gap-1.5 text-dim">Freq
          <DragNumber value={sb.freq} min={20} max={20000} step={1} pixelsPerStep={1} format={formatHz} className="text-fg"
            onCommit={(v) => set({ [`band${n}Freq`]: Math.round(v) }, true)} />
        </label>
        <label className={cn("flex items-center gap-1.5 text-dim", !hasGain(sb.type) && "opacity-40")}>Gain
          <DragNumber value={sb.gainDb} min={-24} max={24} step={0.1} format={(v) => `${v > 0 ? "+" : ""}${v.toFixed(1)} dB`} className="text-fg"
            onCommit={(v) => set({ [`band${n}GainDb`]: Math.round(v * 10) / 10 }, true)} />
        </label>
        <label className="flex items-center gap-1.5 text-dim">Q
          <DragNumber value={sb.q} min={0.1} max={18} step={0.01} format={(v) => v.toFixed(2)} className="text-fg"
            onCommit={(v) => set({ [`band${n}Q`]: Math.round(v * 100) / 100 }, true)} />
        </label>
      </div>
    </div>
  );
}

/** What a band turns on as: shelves at the ends, peaks between. */
function defaultType(band: number): EqBandType {
  return band === 0 ? "lowShelf" : band === BANDS - 1 ? "highShelf" : "peak";
}
