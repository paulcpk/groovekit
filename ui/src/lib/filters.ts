// Frequency responses for drawing filters, with the same maths as the engine's DSP so the curve is
// what's heard.

export const SAMPLE_RATE = 48000;

/** The sampler filter's Q from resonance (engine: sampler::Filter::qFor). */
export function qForResonance(resonance: number) {
  return 0.7071 * Math.pow(25 / 0.7071, Math.min(1, Math.max(0, resonance)));
}

export function resonanceForQ(q: number) {
  return Math.min(1, Math.max(0, Math.log(q / 0.7071) / Math.log(25 / 0.7071)));
}

export type SvfType = "off" | "lowpass" | "lowpass24" | "highpass" | "highpass24" | "bandpass" | "notch";

/** The second stage's Q of the 24 dB/octave types (engine: sampler::Filter::resonantStageQ). */
export function steepStageQ(resonance: number) {
  return 1.3066 * Math.pow(25 / 1.3066, Math.min(1, Math.max(0, resonance)));
}

/** Gain in dB of one state-variable stage (a bilinear-transformed 2-pole) at f Hz. */
function stageGainDb(type: "lowpass" | "highpass" | "bandpass" | "notch", cutoff: number, q: number, f: number, sampleRate: number) {
  const fc = Math.min(cutoff, sampleRate * 0.45);
  // The TPT filter matches the analog prototype at prewarped frequencies.
  const w = Math.tan((Math.PI * Math.min(f, sampleRate * 0.4999)) / sampleRate) / Math.tan((Math.PI * fc) / sampleRate);
  const k = 1 / q;
  const re = 1 - w * w;
  const im = k * w;
  const den = Math.hypot(re, im);
  const num = type === "lowpass" ? 1 : type === "highpass" ? w * w : type === "bandpass" ? k * w : Math.abs(1 - w * w);
  return 20 * Math.log10(Math.max(1e-9, num / den));
}

/** Gain in dB of the sampler's filter at f Hz: one stage, or for the 24 dB/octave types two
    (a fixed one and a resonant one), as the engine runs it. */
export function svfGainDb(type: SvfType, cutoff: number, resonance: number, f: number, sampleRate = SAMPLE_RATE) {
  if (type === "off") return 0;
  if (type === "lowpass24" || type === "highpass24") {
    const t = type === "lowpass24" ? "lowpass" : "highpass";
    return stageGainDb(t, cutoff, 0.5412, f, sampleRate) + stageGainDb(t, cutoff, steepStageQ(resonance), f, sampleRate);
  }
  return stageGainDb(type, cutoff, qForResonance(resonance), f, sampleRate);
}

/** Biquad coefficients (RBJ cookbook), normalised so a0 = 1. */
export interface Biquad { b0: number; b1: number; b2: number; a1: number; a2: number }

export function biquadGainDb(c: Biquad, f: number, sampleRate = SAMPLE_RATE) {
  const w = (2 * Math.PI * f) / sampleRate;
  const cos1 = Math.cos(w), sin1 = Math.sin(w), cos2 = Math.cos(2 * w), sin2 = Math.sin(2 * w);
  const nr = c.b0 + c.b1 * cos1 + c.b2 * cos2, ni = -(c.b1 * sin1 + c.b2 * sin2);
  const dr = 1 + c.a1 * cos1 + c.a2 * cos2, di = -(c.a1 * sin1 + c.a2 * sin2);
  return 10 * Math.log10(Math.max(1e-18, (nr * nr + ni * ni) / (dr * dr + di * di)));
}

/** x position (0..1) of a frequency on a log axis from 20 Hz to 20 kHz, and back. */
export const freqToX = (f: number) => Math.log(f / 20) / Math.log(1000);
export const xToFreq = (x: number) => 20 * Math.pow(1000, Math.min(1, Math.max(0, x)));

export function formatHz(f: number) {
  return f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 1 : 2)} kHz` : `${Math.round(f)} Hz`;
}

export const EQ_BAND_TYPES = ["off", "lowCut", "lowShelf", "peak", "notch", "highShelf", "highCut"] as const;
export type EqBandType = (typeof EQ_BAND_TYPES)[number];

export const EQ_BAND_LABELS: Record<EqBandType, string> = {
  off: "Off", lowCut: "Low cut", lowShelf: "Low shelf", peak: "Peak", notch: "Notch", highShelf: "High shelf", highCut: "High cut",
};

/** One band of the engine's EQ (effects::EqPlugin), with the same RBJ-cookbook coefficients. */
export function eqBandCoefficients(type: EqBandType, freq: number, gainDb: number, q: number, sampleRate = SAMPLE_RATE): Biquad {
  const w0 = (2 * Math.PI * Math.min(freq, sampleRate * 0.49)) / sampleRate;
  const cos = Math.cos(w0), alpha = Math.sin(w0) / (2 * Math.max(0.05, q));
  const A = Math.pow(10, gainDb / 40), rootA = Math.sqrt(A);
  let b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

  switch (type) {
    case "lowCut": b0 = (1 + cos) / 2; b1 = -(1 + cos); b2 = b0; a0 = 1 + alpha; a1 = -2 * cos; a2 = 1 - alpha; break;
    case "highCut": b0 = (1 - cos) / 2; b1 = 1 - cos; b2 = b0; a0 = 1 + alpha; a1 = -2 * cos; a2 = 1 - alpha; break;
    case "peak": b0 = 1 + alpha * A; b1 = -2 * cos; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cos; a2 = 1 - alpha / A; break;
    case "notch": b0 = 1; b1 = -2 * cos; b2 = 1; a0 = 1 + alpha; a1 = -2 * cos; a2 = 1 - alpha; break;
    case "lowShelf":
      b0 = A * ((A + 1) - (A - 1) * cos + 2 * rootA * alpha); b1 = 2 * A * ((A - 1) - (A + 1) * cos); b2 = A * ((A + 1) - (A - 1) * cos - 2 * rootA * alpha);
      a0 = (A + 1) + (A - 1) * cos + 2 * rootA * alpha; a1 = -2 * ((A - 1) + (A + 1) * cos); a2 = (A + 1) + (A - 1) * cos - 2 * rootA * alpha;
      break;
    case "highShelf":
      b0 = A * ((A + 1) + (A - 1) * cos + 2 * rootA * alpha); b1 = -2 * A * ((A - 1) + (A + 1) * cos); b2 = A * ((A + 1) + (A - 1) * cos - 2 * rootA * alpha);
      a0 = (A + 1) - (A - 1) * cos + 2 * rootA * alpha; a1 = 2 * ((A - 1) - (A + 1) * cos); a2 = (A + 1) - (A - 1) * cos - 2 * rootA * alpha;
      break;
    case "off": break;
  }
  return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
}

/** A band's gain in dB at f: cuts run twice in the engine, for 24 dB/octave. */
export function eqBandGainDb(type: EqBandType, freq: number, gainDb: number, q: number, f: number) {
  if (type === "off") return 0;
  const db = biquadGainDb(eqBandCoefficients(type, freq, gainDb, q), f);
  return type === "lowCut" || type === "highCut" ? 2 * db : db;
}
