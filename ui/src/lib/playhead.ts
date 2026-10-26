import { useEffect, useRef } from "react";
import { patternLength } from "@/lib/model";
import { useEngine } from "@/state/engine";
import { useUi } from "@/state/ui";

export interface LivePosition {
  ticks: number;
  seconds: number;
  playing: boolean;
}

/** Where the music being heard is: the engine's last report moved on by the time since it was
    read, less the output latency (Bluetooth headphones add a lot), wrapped at the pattern's or
    the loop's end. So the playhead neither waits for reports nor runs ahead of the sound. */
export function livePosition(now = Date.now()): LivePosition {
  const { transport: t, doc } = useEngine.getState();
  if (!t.playing || !t.time || !doc) return { ticks: t.position, seconds: t.seconds, playing: t.playing };

  const ticksPerMs = (doc.tempo * 960) / 60000;
  // A report older than a second means the engine is busy; don't run off without it.
  const elapsed = Math.min(1000, Math.max(0, now - t.time)) - (t.outputLatencyMs ?? 0);
  let ticks = t.position + elapsed * ticksPerMs;

  let range: [number, number] | undefined;
  if (t.mode === "pattern") {
    const pattern = doc.patterns.find((p) => p.id === t.pattern);
    if (pattern) range = [0, patternLength(pattern, doc)];
  } else {
    const { loop, looping } = useUi.getState();
    if (looping && loop) range = [loop.start, loop.end];
  }

  if (range && range[1] > range[0]) {
    const [start, end] = range;
    const length = end - start;
    if (ticks >= end) ticks = start + ((ticks - end) % length);
    // Just after the engine wrapped, what's heard is still the end of the loop.
    else if (ticks < start && t.position >= start) ticks = end - ((start - ticks) % length);
  }

  ticks = Math.max(0, ticks);
  return { ticks, seconds: ticks / (ticksPerMs * 1000), playing: true };
}

/** Calls onFrame with the live position on every frame while playing, and on every transport
    change otherwise, without re-rendering: onFrame updates the DOM itself. deps restart it. */
export function useLivePosition(onFrame: (position: LivePosition) => void, deps: unknown[] = []) {
  const callback = useRef(onFrame);
  callback.current = onFrame;

  useEffect(() => {
    let frame = 0;

    const run = () => {
      const position = livePosition();
      callback.current(position);
      frame = position.playing ? requestAnimationFrame(run) : 0;
    };

    run();
    const unsubscribe = useEngine.subscribe((s, prev) => {
      if (s.transport !== prev.transport && frame === 0) run();
    });

    return () => {
      cancelAnimationFrame(frame);
      unsubscribe();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
}
