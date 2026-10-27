import { useEffect, useState } from "react";
import { engine } from "@/state/engine";

export interface Peaks {
  durationSeconds: number;
  resolution: number;
  peaks: number[];
}

const cache = new Map<string, Promise<Peaks>>();

/** A file's waveform from the engine, fetched once. */
export function getPeaks(path: string): Promise<Peaks> {
  let p = cache.get(path);
  if (!p) {
    p = engine.call<Peaks>("audio.peaks", { path, resolution: 200 });
    p.catch(() => cache.delete(path));
    cache.set(path, p);
  }
  return p;
}

export function usePeaks(path: string | undefined): Peaks | undefined {
  const [peaks, setPeaks] = useState<Peaks>();

  useEffect(() => {
    if (!path) return;
    let live = true;
    getPeaks(path).then((p) => live && setPeaks(p), () => {});
    return () => {
      live = false;
    };
  }, [path]);

  return peaks;
}

/** Draws peaks from fileStart to fileEnd (seconds) across a canvas, mirrored around the centre. */
export function drawPeaks(canvas: HTMLCanvasElement, peaks: Peaks, fileStart: number, fileEnd: number, color: string) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = color;

  const perPixel = ((fileEnd - fileStart) * peaks.resolution) / width;
  const mid = height / 2;

  for (let x = 0; x < width; x++) {
    const from = Math.floor((fileStart * peaks.resolution) + x * perPixel);
    const to = Math.max(from + 1, Math.floor((fileStart * peaks.resolution) + (x + 1) * perPixel));
    let v = 0;
    for (let i = Math.max(0, from); i < Math.min(peaks.peaks.length, to); i++) v = Math.max(v, peaks.peaks[i]);
    const h = Math.max(v > 0 ? 1 : 0, v * mid * 0.95);
    ctx.fillRect(x, mid - h, 1, h * 2);
  }
}

export interface Wave {
  durationSeconds: number;
  resolution: number;
  min: number[];
  max: number[];
}

const waves = new Map<string, Promise<Wave>>();

/** A file's wave in detail (the lowest and highest value of about 6000 slices, however long
    it is), fetched once. */
export function getWave(path: string): Promise<Wave> {
  let p = waves.get(path);
  if (!p) {
    p = getPeaks(path).then((overview) =>
      engine.call<Wave>("audio.peaks", { path, signed: true, resolution: Math.min(48000, Math.max(100, 6000 / Math.max(0.001, overview.durationSeconds))) }));
    p.catch(() => waves.delete(path));
    waves.set(path, p);
  }
  return p;
}

export function useWave(path: string | undefined): Wave | undefined {
  const [wave, setWave] = useState<Wave>();

  useEffect(() => {
    if (!path) return;
    let live = true;
    getWave(path).then((w) => live && setWave(w), () => {});
    return () => {
      live = false;
    };
  }, [path]);

  return wave;
}
