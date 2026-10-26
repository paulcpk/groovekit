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

export type SvfType = "off" | "lowpass" | "highpass" | "bandpass";

/** Gain in dB of the sampler's state-variable filter (a bilinear-transformed 2-pole) at f Hz. */
export function svfGainDb(type: SvfType, cutoff: number, q: number, f: number, sampleRate = SAMPLE_RATE) {
  if (type === "off") return 0;
  const fc = Math.min(cutoff, sampleRate * 0.45);
  // The TPT filter matches the analog prototype at prewarped frequencies.
  const w = Math.tan((Math.PI * Math.min(f, sampleRate * 0.4999)) / sampleRate) / Math.tan((Math.PI * fc) / sampleRate);
  const k = 1 / q;
  const re = 1 - w * w;
  const im = k * w;
  const den = Math.hypot(re, im);
  const num = type === "lowpass" ? 1 : type === "highpass" ? w * w : k * w;
  return 20 * Math.log10(Math.max(1e-9, num / den));
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
