// Live drawing for effect windows: a canvas redrawn every frame, the engine's meter frames fed
// in at a steady pace so scrolling is smooth, and values that glide to where they're going.

import { useEffect, useRef } from "react";
import { engine, useEngine } from "@/state/engine";

/** Calls back on every animation frame with the time in ms and the ms since the last frame. */
export function useAnimationFrame(callback: (now: number, dt: number) => void) {
  const ref = useRef(callback);
  ref.current = callback;

  useEffect(() => {
    let frame = 0, last = performance.now();
    const tick = (now: number) => {
      ref.current(now, Math.min(100, now - last));
      last = now;
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, []);
}

/** A canvas sized to its box at the screen's pixel density, drawn every frame. The drawing gets
    the context scaled to CSS pixels and the box's width and height. */
export function useCanvas(draw: (g: CanvasRenderingContext2D, width: number, height: number, now: number, dt: number) => void) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const size = useRef({ width: 0, height: 0 });

  useEffect(() => {
    const el = canvas.current;
    if (!el) return;
    const observer = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      size.current = { width, height };
      el.width = Math.round(width * devicePixelRatio);
      el.height = Math.round(height * devicePixelRatio);
    });
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  useAnimationFrame((now, dt) => {
    const el = canvas.current;
    const g = el?.getContext("2d");
    const { width, height } = size.current;
    if (!g || width === 0) return;
    g.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
    g.clearRect(0, 0, width, height);
    draw(g, width, height, now, dt);
  });

  return canvas;
}

/** Moves `current` towards `target`, most of the way in `ms` (exponential, frame-rate independent). */
export function approach(current: number, target: number, dt: number, ms = 80) {
  return target + (current - target) * Math.exp(-dt / ms);
}

/** Values that glide: `get(key, target, dt)` returns the key's value a step closer to target. */
export function useGlide() {
  const values = useRef(new Map<string, number>());
  return (key: string, target: number, dt: number, ms = 80) => {
    const v = values.current.get(key);
    const next = v === undefined || !Number.isFinite(v) ? target : approach(v, target, dt, ms);
    values.current.set(key, next);
    return next;
  };
}

//==============================================================================
const FRAME_MS = 5; // the engine's meter hop

/** Meter frames from an effect (see `mixer.meter`): they arrive in bursts with each poll and are
    let out at the engine's pace, so whatever scrolls them moves evenly. Keeps the latest
    `capacity` frames. */
export class MeterStream {
  private data: Float32Array;
  private pending: number[][] = [];
  private due = 0;
  /** Frames shown so far. */
  count = 0;

  /** `levels`: which values are levels in dB (the rest, gain changes, start at 0 rather than the floor). */
  constructor(readonly width: number, private levels: (index: number) => boolean = () => true, readonly capacity = 1200, readonly floor = -100) {
    this.data = new Float32Array(width * capacity);
    for (let i = 0; i < this.data.length; i++) this.data[i] = this.start(i % width);
  }

  private start(index: number) {
    return this.levels(index) ? this.floor : 0;
  }

  receive(frames: number[][], backlog: boolean) {
    // On opening, the last few seconds go straight in, so there's something to see.
    if (backlog) {
      for (const f of frames) this.append(f);
      return;
    }
    this.pending.push(...frames);
    // Far behind (the window was hidden): catch up.
    if (this.pending.length > 200) this.pending.splice(0, this.pending.length - 40);
  }

  /** Lets out the frames due since the last call. */
  advance(dt: number) {
    this.due += dt / FRAME_MS;
    // A little faster when the queue builds up, so latency stays around one poll.
    if (this.pending.length > 16) this.due += 1;
    while (this.due >= 1 && this.pending.length) {
      this.append(this.pending.shift()!);
      this.due -= 1;
    }
    if (!this.pending.length) this.due = Math.min(this.due, 1);
  }

  /** Value `index` of the frame `ago` frames before the newest (0 is the newest). */
  value(ago: number, index: number) {
    if (ago >= Math.min(this.count, this.capacity)) return this.start(index);
    const slot = (this.count - 1 - ago) % this.capacity;
    return this.data[slot * this.width + index];
  }

  private append(frame: number[]) {
    const slot = this.count % this.capacity;
    for (let i = 0; i < this.width; i++) this.data[slot * this.width + i] = frame[i] ?? this.floor;
    this.count++;
  }
}

/** Streams an effect's meter frames while the window is open. */
export function useMeterStream(insert: string, effect: string, width: number, levels?: (index: number) => boolean) {
  const stream = useRef<MeterStream>(null);
  stream.current ??= new MeterStream(width, levels);
  const playing = useEngine((s) => s.transport.playing);

  useEffect(() => {
    let live = true, busy = false;
    let position: number | undefined;
    const poll = () => {
      if (busy) return;
      busy = true;
      engine.call<{ position?: number; frames?: number[][] }>("mixer.meter", { insert, effect, ...(position !== undefined ? { since: position } : {}) })
        .then((r) => {
          if (!live || r.position === undefined || !r.frames) return;
          stream.current!.receive(r.frames, position === undefined);
          position = r.position;
        }, () => {})
        .finally(() => (busy = false));
    };
    const timer = setInterval(poll, playing ? 33 : 100);
    poll();
    return () => {
      live = false;
      clearInterval(timer);
    };
  }, [insert, effect, playing]);

  useAnimationFrame((_now, dt) => stream.current!.advance(dt));
  return stream.current;
}

/** The latest stereo output of an effect that keeps one (the stereo field), polled while shown. */
export function useStereoSamples(insert: string, effect: string, samples = 512) {
  const latest = useRef<{ left: number[]; right: number[] }>({ left: [], right: [] });
  const playing = useEngine((s) => s.transport.playing);

  useEffect(() => {
    let live = true, busy = false;
    const poll = () => {
      if (busy) return;
      busy = true;
      engine.call<{ left?: number[]; right?: number[] }>("mixer.meter", { insert, effect, samples })
        .then((r) => {
          if (live && r.left && r.right) latest.current = { left: r.left, right: r.right };
        }, () => {})
        .finally(() => (busy = false));
    };
    const timer = setInterval(poll, playing ? 33 : 150);
    poll();
    return () => {
      live = false;
      clearInterval(timer);
    };
  }, [insert, effect, samples, playing]);

  return latest;
}

//==============================================================================
/** The engine's compressor curve (effects::GainComputer): the gain change in dB for a level. */
export function gainChangeDb(levelDb: number, c: { threshold: number; ratio: number; knee: number; below?: number; belowRatio?: number }) {
  let gain = 0;
  const over = levelDb - c.threshold;
  const slope = 1 / c.ratio - 1;
  if (c.knee > 0 && Math.abs(over) < c.knee / 2) gain = (slope * (over + c.knee / 2) ** 2) / (2 * c.knee);
  else if (over > 0) gain = slope * over;
  if (c.belowRatio && c.belowRatio > 1 && c.below !== undefined && levelDb < c.below) {
    const lift = Math.min(24, (c.below - levelDb) * (1 - 1 / c.belowRatio));
    gain += lift * Math.max(0, Math.min(1, (levelDb + 100) / 20));
  }
  return gain;
}

/** Sets up a canvas context's font: the UI's, at a size. */
export function font(g: CanvasRenderingContext2D, size: number, weight = 400) {
  g.font = `${weight} ${size}px -apple-system, BlinkMacSystemFont, "SF Pro Text", Inter, system-ui, sans-serif`;
}
