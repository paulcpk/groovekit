import { useEngine } from "@/state/engine";
import { Knobs, useParams, type EditorProps } from "./editors";
import { font, useCanvas, useGlide, useMeterStream } from "./live";

const H = 180;
const QUARTERS = [0.25, 0.5, 1, 2, 4];
const SYNC_NAMES = ["1/16", "1/8", "1/4", "1/2", "1 bar"];

/** The engine's duck shape (effects::DuckPlugin::shape): the gain at a point through a period. */
function duckShape(phase: number, periodMs: number, depth: number, attackMs: number, holdMs: number, release: number, curve: number) {
  const t = phase * periodMs;
  const attack = Math.min(attackMs, periodMs * 0.5), hold = Math.min(holdMs, periodMs - attack), back = Math.max(1, release * periodMs);
  let dip: number;
  if (t < attack) dip = attack > 0 ? t / attack : 1;
  else if (t < attack + hold) dip = 1;
  else dip = 1 - Math.pow(Math.min(1, (t - attack - hold) / back), 1 + 2 * curve);
  return 1 - depth * dip;
}

/** The duck: two periods of its curve, the kick grid beneath it, and a light riding the curve
    where the music is now (from the engine while it plays). */
export function DuckEditor(props: EditorProps) {
  const p = useParams(props);
  const tempo = useEngine((s) => s.doc?.tempo ?? 120);
  const stream = useMeterStream(props.insert.id, props.effect.id, 2, (i) => i === 1);
  const glide = useGlide();
  const sync = Math.round(p("sync"));
  const periodMs = (QUARTERS[sync] * 60000) / tempo;

  const canvas = useCanvas((g, w, h, _now, dt) => {
    const depth = glide("d", p("depth") / 100, dt), attack = glide("a", p("attackMs"), dt), hold = glide("h", p("holdMs"), dt);
    const release = glide("r", p("release") / 100, dt), curve = glide("c", p("curve") / 100, dt), offset = glide("o", p("offset") / 100, dt);
    const left = 14, right = w - 14, top = 26, bottom = h - 22;
    const x = (period: number) => left + (period / 2) * (right - left);
    const y = (gain: number) => bottom - gain * (bottom - top);

    g.fillStyle = "#0b0b0e";
    g.fillRect(0, 0, w, h);

    // The grid: each period's beat, where a kick would hit.
    for (let k = 0; k <= 2; k++) {
      const bx = x(k + offset);
      if (bx > right) continue;
      g.fillStyle = "rgba(242,163,58,0.18)";
      g.fillRect(bx - 1, top, 2, bottom - top);
      g.fillStyle = "rgba(242,163,58,0.75)";
      g.beginPath();
      g.arc(bx, bottom + 8, 3, 0, 2 * Math.PI);
      g.fill();
    }
    g.fillStyle = "rgba(255,255,255,0.05)";
    g.fillRect(left, y(1), right - left, 1);

    // The curve, filled down to it.
    const gain = (at: number) => {
      let ph = at - offset;
      ph -= Math.floor(ph);
      return duckShape(ph, periodMs, depth, attack, hold, release, curve);
    };
    g.beginPath();
    g.moveTo(left, bottom);
    for (let i = 0; i <= 400; i++) {
      const at = (i / 400) * 2;
      g.lineTo(x(at), y(gain(at)));
    }
    g.lineTo(right, bottom);
    g.closePath();
    const fill = g.createLinearGradient(0, top, 0, bottom);
    fill.addColorStop(0, "rgba(88,183,214,0.35)");
    fill.addColorStop(1, "rgba(88,183,214,0.02)");
    g.fillStyle = fill;
    g.fill();
    g.beginPath();
    for (let i = 0; i <= 400; i++) {
      const at = (i / 400) * 2;
      i ? g.lineTo(x(at), y(gain(at))) : g.moveTo(x(at), y(gain(at)));
    }
    g.strokeStyle = "#58b7d6";
    g.lineWidth = 2;
    g.stroke();

    // Where it is now.
    const phase = stream.value(0, 0) / 100;
    const now = (phase + offset) % 1;
    const gx = x(now), gy = y(gain(now));
    g.shadowColor = "#ffffff";
    g.shadowBlur = 10;
    g.fillStyle = "#ffffff";
    g.beginPath();
    g.arc(gx, gy, 4, 0, 2 * Math.PI);
    g.fill();
    g.shadowBlur = 0;

    font(g, 11, 600);
    g.fillStyle = "rgba(230,230,240,0.9)";
    g.fillText(`Ducks every ${SYNC_NAMES[sync]} · ${Math.round(depth * 100)}% deep · back in ${Math.round(attack + hold + release * periodMs)} ms`, 12, 16);
  });

  return (
    <div className="flex flex-col gap-2">
      <div className="overflow-hidden rounded-md ring-1 ring-line">
        <canvas ref={canvas} className="block w-full select-none" style={{ height: H }} />
      </div>
      <Knobs props={props} ids={["sync", "depth", "attackMs", "holdMs", "release", "curve", "offset"]} names={{ attackMs: "Attack", holdMs: "Hold" }} />
      <div className="text-2xs text-faint">To duck under the kick itself rather than a grid, use a compressor keyed from the kick.</div>
    </div>
  );
}

/** The engine's clipping curve (effects::ClipperPlugin::clip). */
function clipCurve(x: number, ceiling: number, softness: number) {
  const knee = 0.5 * Math.max(0, Math.min(1, softness)) * ceiling, linear = ceiling - knee, a = Math.abs(x);
  if (a <= linear) return x;
  const y = knee > 0 ? linear + knee * Math.tanh((a - linear) / knee) : ceiling;
  return x < 0 ? -y : y;
}

/** The clipper: its curve (in across, out up) with the ceiling, the level riding it, and meters
    for what goes in, what comes out and how much is taken off the peaks. */
export function ClipperEditor(props: EditorProps) {
  const p = useParams(props);
  const stream = useMeterStream(props.insert.id, props.effect.id, 3, (i) => i !== 2);
  const glide = useGlide();

  const canvas = useCanvas((g, w, h, _now, dt) => {
    const drive = Math.pow(10, glide("d", p("driveDb"), dt) / 20), ceiling = Math.pow(10, glide("c", p("ceilingDb"), dt) / 20);
    const softness = glide("s", p("softness"), dt) / 100;
    const size = h - 20, cx = 10, cy = 10;
    const range = 2; // the curve shows input up to +6 dB
    const px = (v: number) => cx + ((v + range) / (2 * range)) * size;
    const py = (v: number) => cy + size - ((v + range) / (2 * range)) * size;

    g.fillStyle = "#0b0b0e";
    g.fillRect(0, 0, w, h);
    g.fillStyle = "#101014";
    g.fillRect(cx, cy, size, size);
    g.strokeStyle = "#1c1c22";
    g.beginPath();
    g.moveTo(px(0), cy); g.lineTo(px(0), cy + size);
    g.moveTo(cx, py(0)); g.lineTo(cx + size, py(0));
    g.stroke();

    // The ceiling.
    g.setLineDash([4, 4]);
    g.strokeStyle = "rgba(239,111,108,0.6)";
    for (const sign of [1, -1]) {
      g.beginPath();
      g.moveTo(cx, py(sign * ceiling)); g.lineTo(cx + size, py(sign * ceiling));
      g.stroke();
    }
    g.setLineDash([]);

    // The curve: what comes out for what goes in (after the drive).
    g.beginPath();
    for (let i = 0; i <= 200; i++) {
      const v = -range + (i / 200) * 2 * range;
      const out = clipCurve(v * drive, ceiling, softness);
      i ? g.lineTo(px(v), py(out)) : g.moveTo(px(v), py(out));
    }
    g.strokeStyle = "#f2a33a";
    g.lineWidth = 2;
    g.stroke();

    // The level riding it.
    const inDb = glide("in", stream.value(0, 0), dt, 30);
    const level = Math.pow(10, inDb / 20) / drive;
    if (inDb > -60) {
      const out = clipCurve(level * drive, ceiling, softness);
      g.shadowColor = "#fff";
      g.shadowBlur = 10;
      g.fillStyle = "#fff";
      g.beginPath();
      g.arc(px(Math.min(range, level)), py(out), 4, 0, 2 * Math.PI);
      g.fill();
      g.shadowBlur = 0;
    }

    // Meters: in, out, and what's taken off.
    const meterX = cx + size + 24, meterW = 26, gap = 18;
    const meterY = (db: number) => cy + size * (Math.max(0, Math.min(36, -db)) / 36);
    const labels = ["In", "Out", "Clip"];
    const values = [inDb, glide("out", stream.value(0, 1), dt, 30), glide("cut", stream.value(0, 2), dt, 30)];
    font(g, 9, 600);
    values.forEach((db, k) => {
      const mx = meterX + k * (meterW + gap);
      g.fillStyle = "#16161b";
      g.fillRect(mx, cy, meterW, size);
      if (k < 2) {
        const top = meterY(db);
        const grad = g.createLinearGradient(0, cy + size, 0, cy);
        grad.addColorStop(0, "#3b8f5a");
        grad.addColorStop(0.75, "#e0d257");
        grad.addColorStop(1, "#ef6f6c");
        g.fillStyle = grad;
        g.fillRect(mx, top, meterW, cy + size - top);
      } else {
        // Taken off, hanging from the top.
        g.fillStyle = "#ef6f6c";
        g.fillRect(mx, cy, meterW, size * Math.min(1, -db / 12));
      }
      g.fillStyle = "rgba(230,230,240,0.7)";
      g.textAlign = "center";
      g.fillText(labels[k], mx + meterW / 2, h - 2);
      g.fillText(k < 2 ? (db <= -99 ? "–" : db.toFixed(1)) : db < -0.05 ? `${db.toFixed(1)}` : "0", mx + meterW / 2, cy + 12);
      g.textAlign = "left";
    });

    font(g, 11, 600);
    g.fillStyle = "rgba(230,230,240,0.9)";
    const cut = values[2];
    g.fillText(cut < -0.05 ? `Taking ${(-cut).toFixed(1)} dB off the peaks` : "Not clipping", meterX + 3 * (meterW + gap), cy + 14);
  });

  return (
    <div className="flex flex-col gap-2">
      <div className="overflow-hidden rounded-md ring-1 ring-line">
        <canvas ref={canvas} className="block w-full select-none" style={{ height: H }} />
      </div>
      <Knobs props={props} ids={["driveDb", "ceilingDb", "softness", "outputDb"]} names={{ driveDb: "Drive", ceilingDb: "Ceiling", outputDb: "Output" }} />
    </div>
  );
}
