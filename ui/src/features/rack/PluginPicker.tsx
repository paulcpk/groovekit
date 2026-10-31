import { useEffect, useState } from "react";
import { DropdownItem, DropdownLabel, DropdownSub } from "@/components/menu";
import { engine } from "@/state/engine";

export interface PluginInfo {
  uid: string;
  name: string;
  manufacturer: string;
  format: "VST3" | "AU";
  instrument: boolean;
  category?: string;
}

/** Scanned plugins, fetched when a menu needs them. */
export function usePlugins(instrument: boolean) {
  const [plugins, setPlugins] = useState<PluginInfo[] | null>(null);
  useEffect(() => {
    engine.call<{ plugins: PluginInfo[] }>("plugins.list", { instrument }).then((r) => setPlugins(r.plugins), () => setPlugins([]));
  }, [instrument]);
  return plugins;
}

/** GrooveKit's own instruments and effects, in the order the menus show them, with what each is for. */
export const OWN_PLUGINS: Record<string, string> = {
  Triad: "three-oscillator synth",
  Quartet: "FM chords and stabs",
  Sextet: "six-oscillator pads and basses",
  Kessel: "hard kicks with rumble",
  "Groove-8": "analogue rhythm machine, the booming one",
  "Groove-9": "rhythm machine, the punchy one",
  Chamber: "reverb",
  Echo: "delay",
};

/** The scanned plugins that are GrooveKit's own, in menu order. */
export function ownPlugins(plugins: PluginInfo[] | null) {
  return Object.keys(OWN_PLUGINS).map((name) => plugins?.find((p) => p.manufacturer === "GrooveKit" && p.name === name)).filter((p): p is PluginInfo => !!p);
}

export function PluginList({ instrument, onPick }: { instrument: boolean; onPick: (p: PluginInfo) => void }) {
  const plugins = usePlugins(instrument);
  if (plugins === null) return <DropdownLabel>Loading…</DropdownLabel>;
  if (plugins.length === 0) return <DropdownLabel>No plugins scanned yet (Settings → Plugins)</DropdownLabel>;

  const byMaker = new Map<string, PluginInfo[]>();
  for (const p of plugins) byMaker.set(p.manufacturer || "Other", [...(byMaker.get(p.manufacturer || "Other") ?? []), p]);

  return (
    <>
      {[...byMaker.entries()].sort(([a], [b]) => a.localeCompare(b)).map(([maker, list]) => (
        <DropdownSub key={maker} label={maker}>
          {list.map((p) => (
            <DropdownItem key={p.uid} onSelect={() => onPick(p)}>{p.name} <span className="text-2xs text-faint">{p.format}</span></DropdownItem>
          ))}
        </DropdownSub>
      ))}
    </>
  );
}
