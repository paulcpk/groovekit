import { useRef, useState } from "react";
import { ToggleChip } from "@/components/controls";
import { cn } from "@/lib/cn";
import { startDrag } from "@/lib/drag";
import { formatHz, freqToX, xToFreq } from "@/lib/filters";
import { useLiveControl } from "@/lib/liveControl";
import { useEffectSpectrum } from "./useEffectOutput";
import { ParamKnob } from "./params";
import { useParams, type EditorProps } from "./editors";
import { font, gainChangeDb, useCanvas, useGlide, useMeterStream } from "./live";

const STRIP_H = 118, LANE_H = 62, LABEL_W = 148, CURVE_W = 58;
const FLOOR = -60, GR_RANGE = 24;
const RED = "#ef6f6c", LIFT = "#5fe0c4", LABEL = "#5b5b64", MASTER = "#9fe870", CEILING = "#ff9a5c";
const COLORS = { 3: ["#c47ef0", "#f2a33a", "#58b7d6"], 4: ["#c47ef0", "#f2a33a", "#6fd08c", "#58b7d6"] } as const;
const NAMES = { 3: ["Low", "Mid", "High"], 4: ["Low", "Low mid", "High mid", "High"] } as const;
const MODES = ["on", "bypass", "solo", "mute"] as const;

/** Meter frame layout (MultibandPlugin::readMeter): input, output, then per band input, output,
    gain, then the master's input and gain. */
const bandIn = (b: number) => 2 + 3 * b, bandOut = (b: number) => 3 + 3 * b, bandGain = (b: number) => 4 + 3 * b;
const OUT = 1, MASTER_IN = 14, MASTER_GAIN = 15;
const isLevel = (i: number) => (i < 14 ? i < 2 || (i - 2) % 3 !== 2 : i === MASTER_IN);

/** The multiband compressor. On top, the spectrum split into its bands, with the crossovers to
    drag and each band's gain change; below, a lane per band with the last three seconds of it
    scrolling past (what goes in, faint, and what comes out), its thresholds to drag, its gain
    change hanging from the top and its curve with the sound riding it; then the selected band's
    controls and the global ones. The master lane at the bottom shows the summed bands going
    into the master compressor and the ceiling. */
export function MultibandEditor(props: EditorProps) {
  const p = useParams(props);
  const [dragged, setDragged] = useState<Record<string, number>>({});
  const [selected, setSelected] = useState<number | "master">(1);
  const v = (id: string) => dragged[id] ?? p(id);
  const count = Math.round(v("bands")) === 1 ? 4 : 3;
  const colors = COLORS[count], names = NAMES[count];
  const master = selected === "master";
  const sel = master ? count - 1 : Math.min(selected, count - 1);
  const stream = useMeterStream(props.insert.id, props.effect.id, 16, isLevel);
  const masterOn = Math.round(v("masterMode")) === 0;
  const spectrum = useEffectSpectrum(props.insert.id, props.effect.id);
  const glide = useGlide();
  const control = useLiveControl<Record<string, number>>((params) => ({ type: "updateEffect", insert: props.insert.id, effect: props.effect.id, changes: { params } }), "Multiband");
  const pulse = useRef<number[]>([0, 0, 0, 0]);

  const set = (params: Record<string, number>, commit: boolean) => {
    if (commit) {
      setDragged({});
      control.onCommit(params);
    } else {
      setDragged((d) => ({ ...d, ...params }));
      control.onChange(params);
    }
  };

  const crossovers = count === 4 ? ["lowFreq", "midFreq", "highFreq"] : ["lowFreq", "highFreq"];
  const edges = (dt: number) => [20, ...crossovers.map((id) => glide(id, v(id), dt, 60)), 20000];
  const mode = (b: number) => MODES[Math.round(v(`band${b + 1}Mode`))] ?? "on";
  const anySolo = Array.from({ length: count }, (_, b) => mode(b)).includes("solo");
  const heard = (b: number) => mode(b) !== "mute" && (!anySolo || mode(b) === "solo");

  //==============================================================================
  const strip = useCanvas((g, w, h, _now, dt) => {
    const x = (hz: number) => freqToX(hz) * w;
    const bounds = edges(dt);

    // The spectrum of what comes out.
    if (spectrum.length) {
      const y = (db: number) => h - ((Math.max(-84, Math.min(0, db)) + 84) / 84) * (h - 18);
      g.beginPath();
      g.moveTo(0, h);
      spectrum.forEach((db, i) => g.lineTo(((i + 0.5) / spectrum.length) * w, y(db)));
      g.lineTo(w, h);
      g.closePath();
      const fill = g.createLinearGradient(0, 0, 0, h);
      fill.addColorStop(0, "rgba(214,214,219,0.30)");
      fill.addColorStop(1, "rgba(214,214,219,0.03)");
      g.fillStyle = fill;
      g.fill();
    }

    for (let b = 0; b < count; b++) {
      const x0 = x(bounds[b]), x1 = x(bounds[b + 1]), mid = (x0 + x1) / 2;
      const on = heard(b);
      g.fillStyle = colors[b] + (b === sel && !master ? "26" : "12");
      g.fillRect(x0, 0, x1 - x0, h);
      g.fillStyle = colors[b] + (on ? "" : "55");
      g.fillRect(x0 + 1, 0, x1 - x0 - 2, 3);

      font(g, 9, 600);
      g.textAlign = "center";
      g.fillStyle = on ? colors[b] : LABEL;
      g.fillText(names[b].toUpperCase(), mid, 16);

      // The band's gain change: reduction down from the top, lift up from the bottom.
      const gain = glide(`g${b}`, stream.value(0, bandGain(b)), dt, 40);
      const cap = { x: mid - 7, w: 14, top: 24, h: h - 46 };
      g.fillStyle = "rgba(0,0,0,0.35)";
      g.beginPath(); g.roundRect(cap.x, cap.top, cap.w, cap.h, 7); g.fill();
      const amount = (Math.min(GR_RANGE, Math.abs(gain)) / GR_RANGE) * cap.h;
      if (Math.abs(gain) > 0.05) {
        g.save();
        g.beginPath(); g.roundRect(cap.x, cap.top, cap.w, cap.h, 7); g.clip();
        g.fillStyle = g.shadowColor = gain < 0 ? RED : LIFT;
        g.shadowBlur = 8;
        g.fillRect(cap.x, gain < 0 ? cap.top : cap.top + cap.h - amount, cap.w, amount);
        g.restore();
      }
      font(g, 9);
      g.fillStyle = Math.abs(gain) > 0.05 ? "#d6d6db" : LABEL;
      g.fillText(Math.abs(gain) > 0.05 ? `${gain > 0 ? "+" : ""}${gain.toFixed(1)}` : "0", mid, h - 8);
    }

    // The crossovers.
    crossovers.forEach((id, i) => {
      const cx = x(bounds[i + 1]);
      g.strokeStyle = "rgba(240,240,243,0.6)";
      g.lineWidth = 1;
      g.beginPath(); g.moveTo(cx, 0); g.lineTo(cx, h); g.stroke();
      g.fillStyle = "#f0f0f3";
      g.beginPath(); g.roundRect(cx - 4, h / 2 - 12, 8, 24, 4); g.fill();
      g.fillStyle = "#111113";
      for (const dy of [-5, 0, 5]) g.fillRect(cx - 2, h / 2 + dy - 0.5, 4, 1);
      font(g, 9);
      g.textAlign = cx > w - 60 ? "right" : "left";
      g.fillStyle = "#d6d6db";
      g.fillText(formatHz(v(id)), cx + (cx > w - 60 ? -8 : 8), h / 2 + 3);
    });
    g.textAlign = "left";
  });

  //==============================================================================
  const lanes = useCanvas((g, w, h, _now, dt) => {
    const laneH = h / (count + 1);
    const waveW = w - CURVE_W - 12;
    const n = Math.floor(waveW / 1.5);   // frames shown: 1.5 px each, about three seconds

    for (let b = 0; b < count; b++) {
      const y0 = b * laneH, cy = y0 + laneH / 2, half = laneH / 2 - 5;
      const color = colors[b];
      const on = heard(b), bypassed = mode(b) === "bypass";
      // The square root of the amplitude: shapes like a waveform, with quiet parts still visible.
      const amp = (db: number) => Math.min(1, Math.pow(10, Math.max(FLOOR, db) / 40)) * half;
      const threshold = glide(`t${b}`, v(`band${b + 1}ThresholdDb`), dt);
      const below = glide(`b${b}`, v(`band${b + 1}BelowDb`), dt);
      const belowRatio = v(`band${b + 1}BelowRatio`);

      // Pulses with the band's level, for a little life on the selected lane.
      pulse.current[b] = glide(`p${b}`, Math.max(0, (stream.value(0, bandOut(b)) + 40) / 40), dt, 120);
      if (b === sel && !master) {
        g.fillStyle = color + "14";
        g.fillRect(0, y0, w, laneH);
      }

      g.save();
      g.beginPath(); g.rect(0, y0, waveW, laneH); g.clip();
      g.globalAlpha = on ? 1 : 0.3;

      // What goes in, faint, and what comes out, mirrored like a waveform.
      const shape = (index: number) => {
        g.beginPath();
        for (let i = 0; i < n; i++) g.lineTo(waveW - i * 1.5, cy - amp(stream.value(i, index)));
        for (let i = n - 1; i >= 0; i--) g.lineTo(waveW - i * 1.5, cy + amp(stream.value(i, index)));
        g.closePath();
      };
      shape(bandIn(b));
      g.fillStyle = "rgba(141,141,150,0.16)";
      g.fill();
      shape(bandOut(b));
      const fill = g.createLinearGradient(0, cy - half, 0, cy + half);
      fill.addColorStop(0, color + "30");
      fill.addColorStop(0.5, color + "c0");
      fill.addColorStop(1, color + "30");
      g.fillStyle = fill;
      g.fill();

      // The thresholds: the line above which it pulls down, and (when lifting) below which it lifts.
      g.lineWidth = 1;
      g.setLineDash([5, 4]);
      g.strokeStyle = bypassed ? LABEL : "#f0f0f3aa";
      for (const s of [-1, 1]) {
        g.beginPath(); g.moveTo(0, cy + s * amp(threshold)); g.lineTo(waveW, cy + s * amp(threshold)); g.stroke();
      }
      if (belowRatio > 1) {
        g.setLineDash([1, 3]);
        g.strokeStyle = LIFT + "cc";
        for (const s of [-1, 1]) {
          g.beginPath(); g.moveTo(0, cy + s * amp(below)); g.lineTo(waveW, cy + s * amp(below)); g.stroke();
        }
      }
      g.setLineDash([]);

      // Gain change over time: reduction from the top, lift from the bottom.
      if (!bypassed) {
        for (const sign of [-1, 1]) {
          g.beginPath();
          let any = false;
          for (let i = 0; i < n; i++) {
            const gain = stream.value(i, bandGain(b));
            const d = sign < 0 ? Math.max(0, -gain) : Math.max(0, gain);
            if (d > 0.3) any = true;
            const y = sign < 0 ? y0 + 2 + (Math.min(GR_RANGE, d) / GR_RANGE) * (laneH - 4) : y0 + laneH - 2 - (Math.min(GR_RANGE, d) / GR_RANGE) * (laneH - 4);
            g.lineTo(waveW - i * 1.5, y);
          }
          if (any) {
            g.strokeStyle = sign < 0 ? RED : LIFT;
            g.lineWidth = 1.3;
            g.stroke();
          }
        }
      }
      g.restore();

      // The band's curve, with the sound on it.
      const s = Math.min(CURVE_W - 4, laneH - 10), qx = w - CURVE_W - 2 + (CURVE_W - s) / 2, qy = cy - s / 2;
      const curve = { threshold, ratio: glide(`r${b}`, v(`band${b + 1}Ratio`), dt), knee: glide("knee", v("kneeDb"), dt), below, belowRatio };
      const makeup = glide(`m${b}`, v(`band${b + 1}GainDb`), dt);
      const cpx = (db: number) => qx + ((db - FLOOR) / -FLOOR) * s, cpy = (db: number) => qy + s - ((Math.max(FLOOR, Math.min(0, db)) - FLOOR) / -FLOOR) * s;
      g.fillStyle = "#0b0b0d";
      g.beginPath(); g.roundRect(qx, qy, s, s, 3); g.fill();
      g.strokeStyle = "#232328";
      g.beginPath(); g.moveTo(qx, qy + s); g.lineTo(qx + s, qy); g.stroke();
      g.beginPath();
      for (let db = FLOOR; db <= 0; db += 1) g.lineTo(cpx(db), cpy(db + (bypassed ? 0 : gainChangeDb(db, curve) + makeup)));
      g.strokeStyle = on ? color : LABEL;
      g.lineWidth = 1.5;
      g.stroke();
      const inDb = stream.value(0, bandIn(b));
      if (inDb > FLOOR + 1 && on) {
        const gain = bypassed ? 0 : stream.value(0, bandGain(b)) + makeup;
        const dx = cpx(inDb), dy = cpy(inDb + gain);
        g.fillStyle = color + "55";
        g.beginPath(); g.arc(dx, dy, 3 + pulse.current[b] * 4, 0, Math.PI * 2); g.fill();
        g.fillStyle = "#fff";
        g.beginPath(); g.arc(dx, dy, 2.2, 0, Math.PI * 2); g.fill();
      }

      if (b > 0) {
        g.fillStyle = "#26262b";
        g.fillRect(0, y0, w, 1);
      }
    }

    drawMaster(g, w, count * laneH, laneH, dt);
  });

  /** The master lane: the summed bands going in (faint) and what comes out, the master's
      threshold and the ceiling, the gain they take off hanging from the top, and the curve. */
  const drawMaster = (g: CanvasRenderingContext2D, w: number, y0: number, laneH: number, dt: number) => {
    const waveW = w - CURVE_W - 12, n = Math.floor(waveW / 1.5);
    const cy = y0 + laneH / 2, half = laneH / 2 - 5;
    const amp = (db: number) => Math.min(1, Math.pow(10, Math.max(FLOOR, db) / 40)) * half;
    const threshold = glide("mt", v("masterThresholdDb"), dt), ceiling = glide("mc", v("ceilingDb"), dt);

    g.fillStyle = "#26262b";
    g.fillRect(0, y0, w, 1);
    if (master) {
      g.fillStyle = MASTER + "14";
      g.fillRect(0, y0, w, laneH);
    }

    g.save();
    g.beginPath(); g.rect(0, y0, waveW, laneH); g.clip();
    g.globalAlpha = masterOn ? 1 : 0.4;
    const shape = (index: number) => {
      g.beginPath();
      for (let i = 0; i < n; i++) g.lineTo(waveW - i * 1.5, cy - amp(stream.value(i, index)));
      for (let i = n - 1; i >= 0; i--) g.lineTo(waveW - i * 1.5, cy + amp(stream.value(i, index)));
      g.closePath();
    };
    shape(MASTER_IN);
    g.fillStyle = "rgba(141,141,150,0.16)";
    g.fill();
    shape(OUT);
    const fill = g.createLinearGradient(0, cy - half, 0, cy + half);
    fill.addColorStop(0, MASTER + "30");
    fill.addColorStop(0.5, MASTER + "c0");
    fill.addColorStop(1, MASTER + "30");
    g.fillStyle = fill;
    g.fill();

    g.lineWidth = 1;
    if (v("masterRatio") > 1.01) {
      g.setLineDash([5, 4]);
      g.strokeStyle = "#f0f0f3aa";
      for (const s of [-1, 1]) {
        g.beginPath(); g.moveTo(0, cy + s * amp(threshold)); g.lineTo(waveW, cy + s * amp(threshold)); g.stroke();
      }
      g.setLineDash([]);
    }
    // The ceiling: a solid line nothing crosses.
    g.strokeStyle = CEILING;
    g.shadowColor = CEILING;
    g.shadowBlur = 6;
    for (const s of [-1, 1]) {
      g.beginPath(); g.moveTo(0, cy + s * amp(ceiling)); g.lineTo(waveW, cy + s * amp(ceiling)); g.stroke();
    }
    g.shadowBlur = 0;

    if (masterOn) {
      g.beginPath();
      let any = false;
      for (let i = 0; i < n; i++) {
        const d = Math.max(0, -stream.value(i, MASTER_GAIN));
        if (d > 0.3) any = true;
        g.lineTo(waveW - i * 1.5, y0 + 2 + (Math.min(GR_RANGE, d) / GR_RANGE) * (laneH - 4));
      }
      if (any) {
        g.strokeStyle = RED;
        g.lineWidth = 1.3;
        g.stroke();
      }
    }
    g.restore();

    // The master's curve: the compressor, then flat at the ceiling.
    const s = Math.min(CURVE_W - 4, laneH - 10), qx = w - CURVE_W - 2 + (CURVE_W - s) / 2, qy = cy - s / 2;
    const curve = { threshold, ratio: glide("mr", v("masterRatio"), dt), knee: glide("knee", v("kneeDb"), dt), below: -100, belowRatio: 1 };
    const makeup = glide("mg", v("masterGainDb"), dt);
    const cpx = (db: number) => qx + ((db - FLOOR) / -FLOOR) * s, cpy = (db: number) => qy + s - ((Math.max(FLOOR, Math.min(0, db)) - FLOOR) / -FLOOR) * s;
    const through = (db: number) => (masterOn ? Math.min(ceiling, db + gainChangeDb(db, curve) + makeup) : db);
    g.fillStyle = "#0b0b0d";
    g.beginPath(); g.roundRect(qx, qy, s, s, 3); g.fill();
    g.strokeStyle = "#232328";
    g.beginPath(); g.moveTo(qx, qy + s); g.lineTo(qx + s, qy); g.stroke();
    g.beginPath();
    for (let db = FLOOR; db <= 0; db += 1) g.lineTo(cpx(db), cpy(through(db)));
    g.strokeStyle = masterOn ? MASTER : LABEL;
    g.lineWidth = 1.5;
    g.stroke();
    const inDb = stream.value(0, MASTER_IN);
    if (inDb > FLOOR + 1 && masterOn) {
      const dx = cpx(Math.min(0, inDb)), dy = cpy(inDb + stream.value(0, MASTER_GAIN));
      g.fillStyle = MASTER + "55";
      g.beginPath(); g.arc(dx, dy, 4, 0, Math.PI * 2); g.fill();
      g.fillStyle = "#fff";
      g.beginPath(); g.arc(dx, dy, 2.2, 0, Math.PI * 2); g.fill();
    }
  };

  //==============================================================================
  const dragCrossover = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const r = e.currentTarget.getBoundingClientRect();
    const px = e.clientX - r.left;
    const ids = crossovers;
    const near = ids.map((id) => Math.abs(freqToX(v(id)) * r.width - px));
    const index = near.indexOf(Math.min(...near));

    if (near[index] > 10) {
      // A click in a band selects it.
      const hz = xToFreq(px / r.width);
      const bounds = [20, ...ids.map(v), 20000];
      const b = bounds.findIndex((f, i) => i < count && hz >= f && hz < bounds[i + 1]);
      if (b >= 0) setSelected(b);
      return;
    }

    const id = ids[index];
    const min = index > 0 ? v(ids[index - 1]) * 1.12 : 20;
    const max = index < ids.length - 1 ? v(ids[index + 1]) / 1.12 : 16000;
    const x0 = freqToX(v(id)) * r.width;
    let last: Record<string, number> = {};
    startDrag(e, (dx) => {
      const limits = id === "lowFreq" ? [20, 2000] : id === "midFreq" ? [100, 10000] : [500, 16000];
      last = { [id]: Math.round(Math.min(max, limits[1], Math.max(min, limits[0], xToFreq((x0 + dx) / r.width)))) };
      set(last, false);
    }, (_dx, _dy, _ev, moved) => moved && set(last, true), "ew-resize");
  };

  const dragLane = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const r = e.currentTarget.getBoundingClientRect();
    const laneH = r.height / (count + 1);
    const b = Math.min(count, Math.floor((e.clientY - r.top) / laneH));
    setSelected(b === count ? "master" : b);
    if (e.clientX - r.left > r.width - CURVE_W - 12) return;
    const id = b === count ? "masterThresholdDb" : `band${b + 1}ThresholdDb`;
    const t0 = v(id);
    const upper = e.clientY - r.top < b * laneH + laneH / 2;
    const half = laneH / 2 - 5;
    let last: Record<string, number> = {};
    startDrag(e, (_dx, dy) => {
      // The line follows the pointer on the lane's scale (the square root of the amplitude).
      const reach = Math.max(1e-3, Math.pow(10, t0 / 40) + (upper ? -dy : dy) / half);
      last = { [id]: Math.round(Math.min(0, Math.max(-60, 40 * Math.log10(reach))) * 10) / 10 };
      set(last, false);
    }, (_dx, _dy, _ev, moved) => moved && set(last, true), "ns-resize");
  };

  const setMode = (b: number, m: (typeof MODES)[number]) =>
    set({ [`band${b + 1}Mode`]: MODES.indexOf(mode(b) === m ? "on" : m) }, true);

  const bounds = [20, ...crossovers.map(v), 20000];

  return (
    <div className="flex flex-col gap-2">
      <div className="overflow-hidden rounded-md bg-[#0c0c0e] ring-1 ring-line">
        <canvas ref={strip} className="block w-full cursor-pointer select-none" style={{ height: STRIP_H }}
          onPointerDown={(e) => e.button === 0 && dragCrossover(e)}
          onPointerMove={(e) => {
            const r = e.currentTarget.getBoundingClientRect();
            const near = crossovers.some((id) => Math.abs(freqToX(v(id)) * r.width - (e.clientX - r.left)) < 10);
            e.currentTarget.style.cursor = near ? "ew-resize" : "pointer";
          }} />
      </div>

      <div className="flex overflow-hidden rounded-md bg-[#0c0c0e] ring-1 ring-line">
        <div className="flex shrink-0 flex-col border-r border-line transition-[height] duration-300" style={{ width: LABEL_W, height: (count + 1) * LANE_H }}>
          {names.map((name, b) => (
            <div key={b} onPointerDown={() => setSelected(b)}
              className={cn("flex min-h-0 flex-1 cursor-pointer flex-col justify-center gap-1 px-2.5 transition-colors", b > 0 && "border-t border-[#26262b]", b === sel && !master ? "bg-raised" : "hover:bg-raised/50")}>
              <div className="flex items-center gap-1.5">
                <span className="h-2 w-2 rounded-full transition-shadow" style={{ background: colors[b], boxShadow: b === sel && !master ? `0 0 8px ${colors[b]}` : undefined }} />
                <span className={cn("whitespace-nowrap text-xs font-medium", heard(b) ? "text-fg" : "text-faint")}>{name}</span>
                <span className="ml-auto flex gap-0.5">
                  <ToggleChip label="S" title="Solo: hear only soloed bands" on={mode(b) === "solo"} onClick={() => setMode(b, "solo")} />
                  <ToggleChip label="M" title="Mute this band" tone="rec" on={mode(b) === "mute"} onClick={() => setMode(b, "mute")} />
                  <ToggleChip label="B" title="Bypass: the band passes uncompressed" tone="dim" on={mode(b) === "bypass"} onClick={() => setMode(b, "bypass")} />
                </span>
              </div>
              <div className="text-2xs text-faint tabular">{b === 0 ? `below ${formatHz(bounds[1])}` : b === count - 1 ? `above ${formatHz(bounds[b])}` : `${formatHz(bounds[b])} – ${formatHz(bounds[b + 1])}`}</div>
            </div>
          ))}
          <div onPointerDown={() => setSelected("master")}
            className={cn("flex min-h-0 flex-1 cursor-pointer flex-col justify-center gap-1 border-t border-line px-2.5 transition-colors", master ? "bg-raised" : "hover:bg-raised/50")}>
            <div className="flex items-center gap-1.5">
              <span className="h-2 w-2 rounded-full transition-shadow" style={{ background: MASTER, boxShadow: master ? `0 0 8px ${MASTER}` : undefined }} />
              <span className={cn("text-xs font-medium", masterOn ? "text-fg" : "text-faint")}>Master</span>
              <span className="ml-auto flex gap-0.5">
                <ToggleChip label="B" title="Bypass the master compressor and the ceiling" tone="dim" on={!masterOn} onClick={() => set({ masterMode: masterOn ? 1 : 0 }, true)} />
              </span>
            </div>
            <div className="text-2xs text-faint tabular">ceiling {v("ceilingDb").toFixed(1)} dB{Math.round(v("bandsMode")) === 1 ? " · bands off" : ""}</div>
          </div>
        </div>
        <canvas ref={lanes} className="block min-w-0 flex-1 cursor-ns-resize select-none transition-[height] duration-300"
          style={{ height: (count + 1) * LANE_H }} onPointerDown={(e) => e.button === 0 && dragLane(e)} />
      </div>

      <div className="flex gap-2">
        {master ? (
          <div className="flex flex-1 flex-col gap-2 rounded-md bg-raised px-3 py-2.5" style={{ boxShadow: `inset 3px 0 0 ${MASTER}` }}>
            <div className="flex items-center gap-2 text-xs">
              <span className="font-medium" style={{ color: MASTER }}>Master</span>
              <span className="text-2xs text-faint">the summed bands: pulled down above the threshold, lifted below “Below”, never past the ceiling</span>
              <span className="ml-auto">
                <ToggleChip label="Master only" title="Leave the bands alone: only the master compressor and the ceiling work" tone="accent"
                  on={Math.round(v("bandsMode")) === 1} onClick={() => set({ bandsMode: Math.round(v("bandsMode")) === 1 ? 0 : 1 }, true)} />
              </span>
            </div>
            <div className="flex flex-wrap items-start justify-between gap-y-2">
              {([["inputDb", "Pre"], ["lowCutHz", "Low cut"], ["masterThresholdDb", "Threshold"], ["masterRatio", "Ratio"], ["masterBelowDb", "Below"],
                ["masterBelowRatio", "Below ratio"], ["masterAttackMs", "Attack"], ["masterReleaseMs", "Release"], ["masterGainDb", "Gain"], ["outputDb", "Post"]] as const).map(([id, name]) => (
                <ParamKnob key={id} insert={props.insert} effect={props.effect} id={id} name={name} color={MASTER} />
              ))}
              <ParamKnob insert={props.insert} effect={props.effect} id="ceilingDb" name="Ceiling" color={CEILING} />
            </div>
          </div>
        ) : (
          <div className="flex flex-1 flex-col gap-2 rounded-md bg-raised px-3 py-2.5" style={{ boxShadow: `inset 3px 0 0 ${colors[sel]}` }}>
            <div className="flex items-center gap-2 text-xs">
              <span className="font-medium" style={{ color: colors[sel] }}>{names[sel]} band</span>
              <span className="text-2xs text-faint">pulls down above the threshold; lifts below “Below” when its ratio is over 1:1</span>
            </div>
            <div className="flex flex-wrap items-start justify-between gap-y-2">
              {(["ThresholdDb", "Ratio", "BelowDb", "BelowRatio", "AttackMs", "ReleaseMs", "GainDb", "Width"] as const).map((suffix) => (
                <ParamKnob key={`${sel}${suffix}`} insert={props.insert} effect={props.effect} id={`band${sel + 1}${suffix}`} color={colors[sel]}
                  name={{ ThresholdDb: "Threshold", Ratio: "Ratio", BelowDb: "Below", BelowRatio: "Below ratio", AttackMs: "Attack", ReleaseMs: "Release", GainDb: "Gain", Width: "Width" }[suffix]} />
              ))}
            </div>
          </div>
        )}
        <div className="flex w-[200px] shrink-0 flex-col gap-2 rounded-md bg-raised px-3 py-2.5">
          <div className="flex items-center justify-between text-xs">
            <span className="font-medium text-dim">All bands</span>
            <div className="flex gap-0.5 rounded-[4px] bg-control p-0.5">
              {[3, 4].map((c, i) => (
                <button key={c} type="button" onClick={() => set({ bands: i }, true)}
                  className={cn("h-5 rounded-[3px] px-2 text-2xs transition-colors", count === c ? "bg-accent text-black" : "text-dim hover:text-fg")}>{c}</button>
              ))}
            </div>
          </div>
          <div className="flex items-start justify-around">
            <ParamKnob insert={props.insert} effect={props.effect} id="kneeDb" name="Knee" />
            <ParamKnob insert={props.insert} effect={props.effect} id="detect" name="Detection" />
          </div>
        </div>
      </div>
    </div>
  );
}
