import { useEffect } from "react";
import { create } from "zustand";
import { engine, useEngine } from "@/state/engine";

export interface EffectParam {
  unit: string;
  min: number;
  max: number;
  default: number;
  automatable: boolean;
  description: string;
}

export interface EffectInfo {
  description: string;
  params: Record<string, EffectParam>;
}

export type Catalogue = Record<string, EffectInfo>;

const useCatalogueStore = create<{ catalogue?: Catalogue; loading: boolean }>(() => ({ loading: false }));

/** The engine's built-in effects with their parameters, fetched once per connection. */
export function useCatalogue(): Catalogue | undefined {
  const connected = useEngine((s) => s.status === "connected");
  const { catalogue, loading } = useCatalogueStore();

  useEffect(() => {
    if (connected && !catalogue && !loading) {
      useCatalogueStore.setState({ loading: true });
      engine.call<Catalogue>("listEffects")
        .then((c) => useCatalogueStore.setState({ catalogue: c, loading: false }))
        .catch(() => useCatalogueStore.setState({ loading: false }));
    }
  }, [connected, catalogue, loading]);

  return catalogue;
}

export function getCatalogue() {
  return useCatalogueStore.getState().catalogue;
}

export const effectNames: Record<string, string> = {
  eq: "EQ", compressor: "Compressor", limiter: "Limiter", reverb: "Reverb", delay: "Delay",
  phaser: "Phaser", chorus: "Chorus", lowpass: "Low-pass", highpass: "High-pass",
};

export function formatParam(value: number, unit: string) {
  if (unit === "Hz") return value >= 1000 ? `${(value / 1000).toFixed(value >= 10000 ? 1 : 2)} kHz` : `${Math.round(value)} Hz`;
  if (unit === "dB") return `${value > 0 ? "+" : ""}${value.toFixed(1)} dB`;
  if (unit === "ms") return value >= 1000 ? `${(value / 1000).toFixed(2)} s` : `${Math.round(value)} ms`;
  if (unit === "%") return `${Math.round(value)}%`;
  if (unit === ":1") return `${value.toFixed(1)}:1`;
  return `${Math.abs(value) >= 100 ? Math.round(value) : value.toFixed(2)}${unit ? ` ${unit}` : ""}`;
}
