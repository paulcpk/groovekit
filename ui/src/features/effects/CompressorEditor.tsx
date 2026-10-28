import { useRef, useState } from "react";
import { startDrag } from "@/lib/drag";
import { useLiveControl } from "@/lib/liveControl";
import { ACCENT, Knobs, SidechainPicker, useParams, WET, type EditorProps } from "./editors";
import { font, gainChangeDb, useCanvas, useGlide, useMeterStream } from "./live";

const H = 200, GAP = 18, METER_W = 26, FLOOR = -60, GR_RANGE = 30;
const RED = "#ef6f6c", GRID = "#1a1a1f", LABEL = "#5b5b64";

/** The compressor: its curve (input level across, output up) with the sound riding it as a dot,
    and the last two seconds scrolling past: input in grey, output in amber, gain reduction
    hanging from the top in red. Drag the curve sideways for the threshold, up and down for the
    ratio; drag in the history for the threshold. */
export function CompressorEditor(props: EditorProps) {
  const p = useParams(props);
  const [dragged, setDragged] = useState<Record<string, number>>({});
  const v = (id: string) => dragged[id] ?? p(id);
  const stream = useMeterStream(props.insert.id, props.effect.id, 3, (i) => i !== 2);
  const glide = useGlide();
  const control = useLiveControl<Record<string, number>>((params) => ({ type: "updateEffect", insert: props.insert.id, effect: props.effect.id, changes: { params } }), "Compressor");
  const anim = useRef({ hold: 0, trail: [] as { x: number; y: number }[] });

  const layout = (w: number, h: number) => {
    const cs = h - 20;
    const hx = 10 + cs + GAP;
    return { cx: 10, cy: 10, cs, hx, hy: 10, hw: w - hx - METER_W - 18, hh: h - 20, mx: w - METER_W - 8 };
  };

  const canvas = useCanvas((g, w, h, _now, dt) => {
    const { cx, cy, cs, hx, hy, hw, hh, mx } = layout(w, h);
    const curve = { threshold: glide("t", v("thresholdDb"), dt), ratio: glide("r", v("ratio"), dt), knee: glide("k", v("kneeDb"), dt) };
    const makeup = glide("m", v("outputDb"), dt);
    const inDb = glide("in", stream.value(0, 0), dt, 25), grDb = glide("gr", stream.value(0, 2), dt, 25);

    // The curve.
    const px = (db: number) => cx + ((db - FLOOR) / -FLOOR) * cs;
    const py = (db: number) => cy + cs - ((db - FLOOR) / -FLOOR) * cs;
    g.save();
    g.beginPath();
    g.roundRect(cx, cy, cs, cs, 4);
    g.fillStyle = "#0f0f12";
    g.fill();
    g.clip();
    g.strokeStyle = GRID;
    g.lineWidth = 1;
    for (let db = -48; db < 0; db += 12) {
      g.beginPath();
      g.moveTo(px(db), cy); g.lineTo(px(db), cy + cs);
      g.moveTo(cx, py(db)); g.lineTo(cx + cs, py(db));
      g.stroke();
    }
    g.setLineDash([3, 4]);
    g.strokeStyle = "#2c2c31";
    g.beginPath(); g.moveTo(px(FLOOR), py(FLOOR)); g.lineTo(px(0), py(0)); g.stroke();
    g.setLineDash([]);

    // Above the threshold, shaded: where it works.
    g.fillStyle = "rgba(88,183,214,0.05)";
    g.fillRect(px(curve.threshold), cy, cx + cs - px(curve.threshold), cs);
    g.strokeStyle = "rgba(88,183,214,0.45)";
    g.beginPath(); g.moveTo(px(curve.threshold), cy); g.lineTo(px(curve.threshold), cy + cs); g.stroke();

    g.beginPath();
    for (let db = FLOOR; db <= 0; db += 0.5) {
      const out = db + gainChangeDb(db, curve) + makeup;
      if (db === FLOOR) g.moveTo(px(db), py(out)); else g.lineTo(px(db), py(out));
    }
    g.shadowColor = ACCENT;
    g.shadowBlur = 10;
    g.strokeStyle = ACCENT;
    g.lineWidth = 2;
    g.stroke();
    g.shadowBlur = 0;

    // The sound on it, with a short trail: off the curve while attack and release catch up.
    const a = anim.current;
    const dot = { x: px(Math.max(FLOOR, inDb)), y: py(inDb + grDb + makeup) };
    a.trail.push(dot);
    if (a.trail.length > 28) a.trail.shift();
    a.trail.forEach((t, i) => {
      g.fillStyle = `rgba(242,163,58,${(i / a.trail.length) * 0.35})`;
      g.beginPath(); g.arc(t.x, t.y, 1.5 + (i / a.trail.length) * 2, 0, Math.PI * 2); g.fill();
    });
    if (inDb > FLOOR + 1) {
      const halo = g.createRadialGradient(dot.x, dot.y, 0, dot.x, dot.y, 14);
      halo.addColorStop(0, "rgba(255,214,150,0.55)");
      halo.addColorStop(1, "rgba(255,214,150,0)");
      g.fillStyle = halo;
      g.beginPath(); g.arc(dot.x, dot.y, 14, 0, Math.PI * 2); g.fill();
      g.fillStyle = "#fff4e0";
      g.beginPath(); g.arc(dot.x, dot.y, 3.5, 0, Math.PI * 2); g.fill();
    }
    g.restore();

    font(g, 13, 600);
    g.fillStyle = ACCENT;
    g.fillText(`${curve.ratio.toFixed(1)}:1`, cx + 8, cy + 18);
    font(g, 9);
    g.fillStyle = LABEL;
    g.fillText(`knee ${curve.knee.toFixed(0)} dB`, cx + 8, cy + 31);
    g.textAlign = "right";
    g.fillText("in →", cx + cs - 6, cy + cs - 6);
    g.textAlign = "left";

    // The history: one frame (5 ms) per pixel, newest at the right.
    const ly = (db: number) => hy + hh - ((Math.max(FLOOR, Math.min(0, db)) - FLOOR) / -FLOOR) * hh;
    const gy = (gr: number) => hy + (Math.min(GR_RANGE, Math.max(0, -gr)) / GR_RANGE) * hh;
    g.save();
    g.beginPath();
    g.roundRect(hx, hy, hw, hh, 4);
    g.fillStyle = "#0f0f12";
    g.fill();
    g.clip();
    g.strokeStyle = GRID;
    for (let db = -48; db < 0; db += 12) {
      g.beginPath(); g.moveTo(hx, ly(db)); g.lineTo(hx + hw, ly(db)); g.stroke();
    }
    const n = Math.floor(hw);
    const area = (index: number, y: (v: number) => number, base: number) => {
      g.beginPath();
      g.moveTo(hx, base);
      for (let i = 0; i < n; i++) g.lineTo(hx + i, y(stream.value(n - 1 - i, index)));
      g.lineTo(hx + n, base);
      g.closePath();
    };
    area(0, ly, hy + hh);
    g.fillStyle = "rgba(141,141,150,0.22)";
    g.fill();
    area(1, ly, hy + hh);
    const amber = g.createLinearGradient(0, hy, 0, hy + hh);
    amber.addColorStop(0, "rgba(242,163,58,0.55)");
    amber.addColorStop(1, "rgba(242,163,58,0.05)");
    g.fillStyle = amber;
    g.fill();
    g.strokeStyle = ACCENT;
    g.lineWidth = 1.2;
    g.stroke();
    area(2, gy, hy);
    g.fillStyle = "rgba(239,111,108,0.22)";
    g.fill();
    g.strokeStyle = RED;
    g.stroke();

    const ty = ly(curve.threshold);
    g.setLineDash([5, 4]);
    g.strokeStyle = WET;
    g.lineWidth = 1;
    g.beginPath(); g.moveTo(hx, ty); g.lineTo(hx + hw, ty); g.stroke();
    g.setLineDash([]);
    g.restore();

    font(g, 9);
    g.fillStyle = LABEL;
    for (const db of [-12, -24, -36, -48]) g.fillText(`${db}`, hx + 4, ly(db) - 2);
    pill(g, `threshold ${curve.threshold.toFixed(1)} dB`, hx + hw - 6, ty - 3, WET);
    pill(g, `in ${fmt(stream.value(0, 0))} · out ${fmt(stream.value(0, 1))}`, hx + hw - 6, hy + hh - 6, "#b4b4bc");

    // Gain reduction now, with a hold that falls back 10 dB a second.
    a.hold = Math.min(0, a.hold + dt * 0.01, grDb);
    g.fillStyle = "#0f0f12";
    g.beginPath(); g.roundRect(mx, hy, METER_W, hh, 3); g.fill();
    const top = hy + 16, height = hh - 32;
    const my = (gr: number) => top + (Math.min(GR_RANGE, -gr) / GR_RANGE) * height;
    const bar = g.createLinearGradient(0, top, 0, top + height);
    bar.addColorStop(0, "#f2a33a");
    bar.addColorStop(1, RED);
    g.fillStyle = bar;
    g.fillRect(mx + 7, top, METER_W - 14, my(Math.min(0, grDb)) - top);
    g.fillStyle = "#fff";
    g.fillRect(mx + 5, my(a.hold) - 1, METER_W - 10, 2);
    font(g, 8, 600);
    g.fillStyle = LABEL;
    g.textAlign = "center";
    g.fillText("GR", mx + METER_W / 2, hy + 10);
    font(g, 9);
    g.fillStyle = grDb < -0.05 ? "#f0f0f3" : LABEL;
    g.fillText(grDb < -0.05 ? grDb.toFixed(1) : "0", mx + METER_W / 2, hy + hh - 4);
    g.textAlign = "left";
  });

  const set = (params: Record<string, number>, commit: boolean) => {
    if (commit) {
      setDragged({});
      control.onCommit(params);
    } else {
      setDragged((d) => ({ ...d, ...params }));
      control.onChange(params);
    }
  };

  return (
    <div className="flex flex-col gap-2">
      <div className="overflow-hidden rounded-md bg-[#0c0c0e] ring-1 ring-line">
        <canvas ref={canvas} className="block w-full select-none" style={{ height: H }}
          onPointerMove={(e) => {
            const r = e.currentTarget.getBoundingClientRect();
            const { hx } = layout(r.width, r.height);
            e.currentTarget.style.cursor = e.clientX - r.left < hx - GAP / 2 ? "grab" : "ns-resize";
          }}
          onPointerDown={(e) => {
            if (e.button !== 0) return;
            const r = e.currentTarget.getBoundingClientRect();
            const { hx, cs, hh } = layout(r.width, r.height);
            const t0 = v("thresholdDb"), r0 = v("ratio");
            const onCurve = e.clientX - r.left < hx - GAP / 2;
            let axis: "x" | "y" | undefined = onCurve ? undefined : "y";
            let last: Record<string, number> = {};
            startDrag(e, (dx, dy) => {
              axis ??= Math.abs(dx) + Math.abs(dy) > 4 ? (Math.abs(dx) > Math.abs(dy) ? "x" : "y") : undefined;
              if (axis === "x" || (axis === "y" && !onCurve))
                last = { thresholdDb: clampRound(t0 + (axis === "x" ? dx / cs : -dy / hh) * 60, -60, 0) };
              else if (axis === "y")
                last = { ratio: clampRound(r0 * Math.pow(2, dy / 50), 1, 20) };
              else return;
              set(last, false);
            }, (_dx, _dy, _ev, moved) => moved && Object.keys(last).length > 0 && set(last, true), onCurve ? "grabbing" : "ns-resize");
          }} />
      </div>
      <Knobs props={props} ids={["thresholdDb", "ratio", "kneeDb", "attackMs", "releaseMs", "detectLowCut", "outputDb"]}
        names={{ thresholdDb: "Threshold", kneeDb: "Knee", detectLowCut: "Detector cut", outputDb: "Make-up" }} />
      <SidechainPicker {...props} />
    </div>
  );
}

/** A label on a dark backing, right-aligned at x, its baseline at y. */
function pill(g: CanvasRenderingContext2D, text: string, x: number, y: number, color: string) {
  const width = g.measureText(text).width;
  g.fillStyle = "rgba(12,12,14,0.8)";
  g.beginPath(); g.roundRect(x - width - 6, y - 10, width + 10, 14, 3); g.fill();
  g.fillStyle = color;
  g.textAlign = "right";
  g.fillText(text, x, y);
  g.textAlign = "left";
}

function fmt(db: number) {
  return db <= -99 ? "-∞" : `${db.toFixed(1)} dB`;
}

function clampRound(v: number, min: number, max: number) {
  return Math.round(Math.min(max, Math.max(min, v)) * 10) / 10;
}
