import type { Project } from "@groovekit/schema/project";
import { useEffect, useState } from "react";
import { DropdownItem, DropdownLabel, DropdownSub } from "@/components/menu";
import { uniqueId } from "@/lib/model";
import { engine, execute } from "@/state/engine";
import { setUi } from "@/state/ui";

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

export function PluginPicker({ doc }: { doc: Project }) {
  return (
    <DropdownSub label="Plugin instrument">
      <PluginList instrument onPick={(p) => {
        const id = uniqueId(p.name, doc.channels);
        void execute({
          type: "addChannel",
          channel: { id, name: p.name, instrument: { type: "plugin", plugin: { format: p.format, uid: p.uid, name: p.name, manufacturer: p.manufacturer } } },
        }, { label: `Add ${p.name}` }).then(() => setUi({ selectedChannel: id }));
      }} />
    </DropdownSub>
  );
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
