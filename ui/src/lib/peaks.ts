import type { SampleProcessing } from "@groovekit/schema/project";
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
  /** Seconds of this wave per second of the file (processing can stretch it). */
  timeScale: number;
  min: number[];
  max: number[];
}

/** What a sampler layer works into its sample before it plays; the wave is drawn with it. */
export interface WaveProcessing {
  process?: SampleProcessing;
  punch?: number;
  tail?: number;
}

const waves = new Map<string, Promise<Wave>>();

/** A file's wave in detail (the lowest and highest value of about 6000 slices, however long
    it is), fetched once; with processing, as a sampler layer plays it. */
export function getWave(path: string, processing?: WaveProcessing): Promise<Wave> {
  const key = processing ? `${path}\n${JSON.stringify(processing)}` : path;
  let p = waves.get(key);
  if (!p) {
    p = getPeaks(path).then((overview) =>
      engine.call<Wave>("audio.peaks", {
        path, signed: true, ...processing,
        resolution: Math.min(48000, Math.max(100, 6000 / Math.max(0.001, overview.durationSeconds))),
      })).then((w) => ({ ...w, timeScale: w.timeScale ?? 1 }));
    p.catch(() => waves.delete(key));
    if (waves.size > 48) waves.clear();
    waves.set(key, p);
  }
  return p;
}

/** The wave; while a new one loads (processing changed), the one before stays. */
export function useWave(path: string | undefined, processing?: WaveProcessing): Wave | undefined {
  const [wave, setWave] = useState<Wave>();
  const key = processing ? JSON.stringify(processing) : "";

  useEffect(() => {
    if (!path) return;
    let live = true;
    // Settings that move fast (a knob turning) ask once they've settled for a moment.
    const timer = setTimeout(() => getWave(path, processing).then((w) => live && setWave(w), () => {}), processing ? 120 : 0);
    return () => {
      live = false;
      clearTimeout(timer);
    };
  }, [path, key]); // eslint-disable-line react-hooks/exhaustive-deps

  return wave;
}
