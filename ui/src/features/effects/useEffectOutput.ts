import { useEffect, useRef, useState } from "react";
import { engine, useEngine } from "@/state/engine";

/** The spectrum of what comes out of an effect slot, polled while shown: fast while playing,
    slowly while stopped. Falls back smoothly (1.2 dB a frame) so it reads like an analyser. */
export function useEffectSpectrum(insert: string, effect: string) {
  const playing = useEngine((s) => s.transport.playing);
  const [bins, setBins] = useState<number[]>([]);
  const shown = useRef<number[]>([]);

  useEffect(() => {
    let live = true;
    let busy = false;
    const poll = () => {
      if (busy) return;
      busy = true;
      engine.call<{ bins: number[] }>("mixer.spectrum", { insert, effect })
        .then(({ bins: next }) => {
          if (!live) return;
          const prev = shown.current;
          shown.current = next.map((v, i) => Math.max(v, (prev[i] ?? -120) - 1.2));
          setBins(shown.current);
        }, () => {})
        .finally(() => (busy = false));
    };
    const timer = setInterval(poll, playing ? 40 : 250);
    poll();
    return () => {
      live = false;
      clearInterval(timer);
    };
  }, [insert, effect, playing]);

  return bins;
}

/** The latest output of an effect slot as a waveform, polled while shown. */
export function useEffectWaveform(insert: string, effect: string, samples = 1024) {
  const playing = useEngine((s) => s.transport.playing);
  const [wave, setWave] = useState<number[]>([]);

  useEffect(() => {
    let live = true;
    let busy = false;
    const poll = () => {
      if (busy) return;
      busy = true;
      engine.call<{ samples: number[] }>("mixer.waveform", { insert, effect, samples })
        .then((r) => live && setWave(r.samples), () => {})
        .finally(() => (busy = false));
    };
    const timer = setInterval(poll, playing ? 50 : 300);
    poll();
    return () => {
      live = false;
      clearInterval(timer);
    };
  }, [insert, effect, samples, playing]);

  return wave;
}
