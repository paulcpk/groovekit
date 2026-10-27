import { useEffectWaveform } from "./useEffectOutput";
import { Knobs, useParams, type EditorProps } from "./editors";
import { useCanvas } from "./live";

const H = 180, PIXEL = 3;
const LCD = "#8dffae", LCD_DIM = "rgba(141,255,174,0.16)", LCD_BG = "#07110a";

/** The bitcrusher on a small LCD: a tone drifting through it (the smooth wave faint, the crushed
    one in steps as wide as the lowered sample rate and as tall as the bits allow), and the live
    output drawn in pixels. */
export function BitcrusherEditor(props: EditorProps) {
  const p = useParams(props);
  const wave = useEffectWaveform(props.insert.id, props.effect.id, 512);
  const bits = p("bits"), rate = p("rateHz"), drive = Math.pow(10, p("driveDb") / 20), jitter = p("jitter") / 100;

  const canvas = useCanvas((g, w, h, now) => {
    g.fillStyle = LCD_BG;
    g.fillRect(0, 0, w, h);

    const half = Math.floor(w * 0.56);
    const mid = h / 2 + 8, amp = h / 2 - 26;
    const levels = Math.pow(2, bits - 1);
    const quantise = (x: number) => Math.round(Math.max(-1, Math.min(1, x)) * levels) / levels;

    // The quantisation steps, when there are few enough to see.
    if (levels <= 32) {
      g.fillStyle = "rgba(141,255,174,0.07)";
      for (let k = -levels; k <= levels; k++) g.fillRect(8, Math.round(mid - (k / levels) * amp), half - 16, 1);
    }

    // A 440 Hz tone over 7 ms, drifting so the steps shimmer.
    const span = 0.007, hz = 440, phase = now / 4000;
    const signal = (t: number) => 0.9 * Math.sin(2 * Math.PI * (hz * t + phase));
    const tx = (t: number) => 8 + (t / span) * (half - 16);
    g.beginPath();
    for (let i = 0; i <= 200; i++) {
      const t = (i / 200) * span;
      g.lineTo(tx(t), mid - signal(t) * amp);
    }
    g.strokeStyle = LCD_DIM;
    g.lineWidth = 1.5;
    g.stroke();

    // Held samples: each lasts a period of the lowered rate, give or take the jitter.
    let seed = 17;
    const random = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
    g.beginPath();
    let t = 0, held = quantise(signal(0) * drive);
    g.moveTo(tx(0), mid - held * amp);
    while (t < span) {
      const next = Math.min(span, t + (1 / rate) * (1 + 0.5 * jitter * random()));
      g.lineTo(tx(next), mid - held * amp);
      t = next;
      held = quantise(signal(t) * drive);
      g.lineTo(tx(t), mid - held * amp);
    }
    g.shadowColor = LCD;
    g.shadowBlur = 8;
    g.strokeStyle = LCD;
    g.lineWidth = 2;
    g.stroke();
    g.shadowBlur = 0;

    // The live output, in chunky pixels.
    const x0 = half + 8, width = w - x0 - 8;
    g.fillStyle = "rgba(141,255,174,0.05)";
    g.fillRect(x0, 22, width, h - 30);
    if (wave.length) {
      const peak = Math.max(0.05, ...wave.map(Math.abs));
      const columns = Math.floor(width / PIXEL);
      g.fillStyle = LCD;
      // Each column of pixels spans the samples under it, lowest to highest, like a waveform.
      for (let c = 0; c < columns; c++) {
        const from = Math.floor((c / columns) * wave.length), to = Math.max(from + 1, Math.floor(((c + 1) / columns) * wave.length));
        let lo = Infinity, hi = -Infinity;
        for (let i = from; i < to; i++) { lo = Math.min(lo, wave[i]); hi = Math.max(hi, wave[i]); }
        const top = Math.round((mid - (hi / peak) * amp) / PIXEL), bottom = Math.round((mid - (lo / peak) * amp) / PIXEL);
        for (let y = top; y <= bottom; y++) g.fillRect(x0 + c * PIXEL, y * PIXEL, PIXEL - 1, PIXEL - 1);
      }
    }

    // A pixel grid and scanlines over everything, like a real panel.
    g.fillStyle = "rgba(0,0,0,0.28)";
    for (let y = 0; y < h; y += PIXEL) g.fillRect(0, y, w, 1);
    for (let x = 0; x < w; x += PIXEL) g.fillRect(x, 0, 1, h);

    g.font = `600 13px "SF Mono", ui-monospace, Menlo, monospace`;
    g.fillStyle = LCD;
    g.fillText(`${bits.toFixed(bits % 1 ? 1 : 0)} BIT`, 10, 18);
    g.fillText(`${rate >= 1000 ? (rate / 1000).toFixed(1) + " kHz" : Math.round(rate) + " Hz"}`, 90, 18);
    g.font = `500 10px "SF Mono", ui-monospace, Menlo, monospace`;
    g.fillStyle = "rgba(141,255,174,0.55)";
    g.fillText("OUTPUT", x0 + 4, 18);
  });

  return (
    <div className="flex flex-col gap-2">
      <div className="overflow-hidden rounded-md ring-1 ring-line" style={{ boxShadow: "inset 0 0 24px rgba(0,0,0,0.9)" }}>
        <canvas ref={canvas} className="block w-full select-none" style={{ height: H }} />
      </div>
      <Knobs props={props} ids={["driveDb", "bits", "rateHz", "jitter", "tone", "outputDb"]} names={{ driveDb: "Drive", rateHz: "Rate", outputDb: "Output" }} />
    </div>
  );
}
