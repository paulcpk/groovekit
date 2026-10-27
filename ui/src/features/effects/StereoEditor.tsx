import { useRef } from "react";
import { ParamKnob } from "./params";
import { ACCENT, useParams, type EditorProps } from "./editors";
import { approach, font, useCanvas, useStereoSamples } from "./live";

const H = 220;
const SCOPE = "#5fe0c4", INVERTED = "#8f7cf8", LABEL = "#5b5b64";

/** The stereo field: a vectorscope of what comes out (the middle up, the sides across, so mono
    is a vertical line and wide sound a cloud) with the correlation below, and the routing it's
    set to: each input side flowing to each output side, thicker the more of it goes, violet
    where it's inverted. */
export function StereoEditor(props: EditorProps) {
  const p = useParams(props);
  const samples = useStereoSamples(props.insert.id, props.effect.id, 512);
  const anim = useRef({ scale: 1, correlation: 1, trails: [] as { left: number[]; right: number[] }[], last: samples.current });

  const canvas = useCanvas((g, w, h, now, dt) => {
    const a = anim.current;
    const { left, right } = samples.current;
    if (samples.current !== a.last) {
      a.trails.push(samples.current);
      if (a.trails.length > 5) a.trails.shift();
      a.last = samples.current;
    }

    // The scope, scaled to what's playing so quiet sound still fills it.
    const size = h - 34, cx = 14 + size / 2, cy = 10 + size / 2, r = size / 2;
    const peak = Math.max(0.02, ...left.map(Math.abs), ...right.map(Math.abs));
    a.scale = approach(a.scale, 0.9 / peak, dt, a.scale > 0.9 / peak ? 60 : 600);
    g.fillStyle = "#0b0b0d";
    g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.fill();
    g.strokeStyle = "#1d1d22";
    g.lineWidth = 1;
    for (const k of [0.33, 0.66]) { g.beginPath(); g.arc(cx, cy, r * k, 0, Math.PI * 2); g.stroke(); }
    g.beginPath();
    g.moveTo(cx, cy - r); g.lineTo(cx, cy + r);
    g.moveTo(cx - r, cy); g.lineTo(cx + r, cy);
    g.moveTo(cx - r * 0.707, cy - r * 0.707); g.lineTo(cx + r * 0.707, cy + r * 0.707);
    g.moveTo(cx + r * 0.707, cy - r * 0.707); g.lineTo(cx - r * 0.707, cy + r * 0.707);
    g.stroke();
    font(g, 9, 600);
    g.fillStyle = LABEL;
    g.textAlign = "center";
    g.fillText("M", cx, cy - r + 11);
    g.fillText("L", cx - r * 0.707 + 2, cy - r * 0.707 + 2);
    g.fillText("R", cx + r * 0.707 - 2, cy - r * 0.707 + 2);
    g.fillText("+S", cx + r - 9, cy + 3);
    g.fillText("−S", cx - r + 9, cy + 3);

    g.save();
    g.beginPath(); g.arc(cx, cy, r - 1, 0, Math.PI * 2); g.clip();
    g.globalCompositeOperation = "lighter";
    a.trails.forEach((trail, age) => {
      const alpha = ((age + 1) / a.trails.length) * 0.5;
      g.fillStyle = `rgba(95,224,196,${alpha})`;
      for (let i = 0; i < trail.left.length; i++) {
        const l = trail.left[i] * a.scale, rt = trail.right[i] * a.scale;
        g.fillRect(cx + (rt - l) * 0.707 * r - 0.75, cy - (l + rt) * 0.707 * r - 0.75, 1.5, 1.5);
      }
    });
    g.restore();

    // Correlation: +1 is mono, 0 unrelated sides, below 0 the sides fight (it thins out in mono).
    let lr = 0, ll = 0, rr = 0;
    for (let i = 0; i < left.length; i++) { lr += left[i] * right[i]; ll += left[i] ** 2; rr += right[i] ** 2; }
    const corr = ll > 1e-9 && rr > 1e-9 ? lr / Math.sqrt(ll * rr) : 1;
    a.correlation = approach(a.correlation, corr, dt, 150);
    const bx = 14, by = h - 16, bw = size;
    g.fillStyle = "#18181c";
    g.beginPath(); g.roundRect(bx, by, bw, 6, 3); g.fill();
    const grad = g.createLinearGradient(bx, 0, bx + bw, 0);
    grad.addColorStop(0, "#ef6f6c"); grad.addColorStop(0.5, "#e8d44d"); grad.addColorStop(1, SCOPE);
    g.fillStyle = grad;
    const mx = bx + ((a.correlation + 1) / 2) * bw;
    g.beginPath(); g.roundRect(Math.min(mx, bx + bw / 2), by, Math.abs(mx - (bx + bw / 2)), 6, 3); g.fill();
    g.fillStyle = "#fff";
    g.fillRect(mx - 1, by - 2, 2, 10);
    font(g, 8);
    g.fillStyle = LABEL;
    g.textAlign = "left"; g.fillText("−1", bx, by - 3);
    g.textAlign = "right"; g.fillText("+1", bx + bw, by - 3);
    g.textAlign = "center"; g.fillText(`correlation ${a.correlation.toFixed(2)}`, bx + bw / 2, by - 3);

    // The routing: inputs on the left, outputs on the right.
    const x0 = 14 + size + 40, x1 = w - 150;
    const ys = { L: 48, R: h - 48 };
    const routes: [keyof typeof ys, keyof typeof ys, number][] = [
      ["L", "L", p("leftToLeft") / 100], ["R", "L", p("rightToLeft") / 100],
      ["L", "R", p("leftToRight") / 100], ["R", "R", p("rightToRight") / 100],
    ];
    for (const [from, to, k] of routes) {
      if (Math.abs(k) < 0.005) continue;
      const color = k < 0 ? INVERTED : ACCENT;
      g.beginPath();
      g.moveTo(x0 + 14, ys[from]);
      g.bezierCurveTo((x0 + x1) / 2, ys[from], (x0 + x1) / 2, ys[to], x1 - 14, ys[to]);
      g.strokeStyle = color + "40";
      g.lineWidth = 2 + Math.abs(k) * 7;
      g.stroke();
      // Signal flowing along it, faster the more goes through.
      g.setLineDash([2, 12]);
      g.lineDashOffset = -(now / 1000) * (20 + 60 * Math.abs(k));
      g.strokeStyle = color;
      g.lineWidth = 2 + Math.abs(k) * 2;
      g.lineCap = "round";
      g.stroke();
      g.setLineDash([]);
      g.lineCap = "butt";
    }
    for (const [side, y] of Object.entries(ys)) {
      for (const x of [x0, x1]) {
        g.fillStyle = "#202024";
        g.strokeStyle = "#3a3a41";
        g.beginPath(); g.arc(x, y, 13, 0, Math.PI * 2); g.fill(); g.stroke();
        font(g, 10, 600);
        g.fillStyle = "#d6d6db";
        g.textAlign = "center";
        g.fillText(side, x, y + 3.5);
      }
    }
    font(g, 9);
    g.fillStyle = LABEL;
    g.fillText("in", x0, 20);
    g.fillText("out", x1, 20);

    // Then the rest of the chain, in order.
    const delay = p("delayMs"), width = p("width"), mono = p("monoBelow"), pan = p("pan");
    const steps = [
      { label: "Delay", value: Math.abs(delay) < 0.05 ? "off" : `${delay > 0 ? "R" : "L"} +${Math.abs(delay).toFixed(1)} ms`, on: Math.abs(delay) >= 0.05 },
      { label: "Width", value: `${Math.round(width)}%`, on: Math.round(width) !== 100 },
      { label: "Mono below", value: mono >= 20 ? `${Math.round(mono)} Hz` : "off", on: mono >= 20 },
      { label: "Balance", value: Math.abs(pan) < 0.5 ? "centre" : `${pan < 0 ? "L" : "R"} ${Math.abs(Math.round(pan))}%`, on: Math.abs(pan) >= 0.5 },
    ];
    const sx = x1 + 30;
    g.textAlign = "left";
    steps.forEach((s, i) => {
      const y = 26 + i * ((h - 40) / 4);
      g.fillStyle = s.on ? "rgba(242,163,58,0.12)" : "#141417";
      g.beginPath(); g.roundRect(sx, y, w - sx - 10, 32, 5); g.fill();
      font(g, 9);
      g.fillStyle = LABEL;
      g.fillText(s.label, sx + 8, y + 13);
      font(g, 11, 600);
      g.fillStyle = s.on ? ACCENT : "#8d8d96";
      g.fillText(s.value, sx + 8, y + 26);
      if (i < steps.length - 1) {
        g.fillStyle = "#2c2c31";
        g.fillRect(sx + 14, y + 32, 1, (h - 40) / 4 - 32);
      }
    });
    g.textAlign = "left";
  });

  const knob = (id: string, name: string) => <ParamKnob insert={props.insert} effect={props.effect} id={id} name={name} />;

  return (
    <div className="flex flex-col gap-2">
      <div className="overflow-hidden rounded-md bg-[#0c0c0e] ring-1 ring-line">
        <canvas ref={canvas} className="block w-full select-none" style={{ height: H }} />
      </div>
      <div className="flex gap-2">
        <div className="rounded-md bg-raised px-3 py-2">
          <div className="mb-1 text-2xs text-faint">Into left · into right</div>
          <div className="grid grid-cols-2 gap-x-3 gap-y-2">
            {knob("leftToLeft", "L → L")}{knob("leftToRight", "L → R")}
            {knob("rightToLeft", "R → L")}{knob("rightToRight", "R → R")}
          </div>
        </div>
        <div className="flex flex-1 flex-wrap items-center justify-around rounded-md bg-raised px-2 py-3">
          {knob("delayMs", "Delay")}{knob("width", "Width")}{knob("monoBelow", "Mono below")}{knob("pan", "Balance")}{knob("outputDb", "Output")}
        </div>
      </div>
    </div>
  );
}
