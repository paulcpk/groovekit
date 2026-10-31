import { AudioWaveform, Drum, KeyboardMusic, Puzzle } from "lucide-react";
import { useState } from "react";
import { Dialog } from "@/components/dialog";
import { cn } from "@/lib/cn";
import { colorFor } from "@/lib/model";
import { instrumentName, pictureOf, SAMPLER, type InstrumentChoice } from "./instruments";
import { OWN_PLUGINS, ownPlugins, usePlugins } from "./PluginPicker";

/** Every instrument as a card to pick from: the sampler and GrooveKit's own first, then the
    scanned plugins by maker. Picking one closes it. */
export function InstrumentPicker({ open, onOpenChange, title, onPick }: {
  open: boolean; onOpenChange: (open: boolean) => void; title: string; onPick: (choice: InstrumentChoice) => void;
}) {
  const plugins = usePlugins(true);
  const [query, setQuery] = useState("");
  const own = ownPlugins(plugins);
  const others = (plugins ?? []).filter((p) => !own.includes(p));
  const words = query.toLowerCase().split(/\s+/).filter(Boolean);
  const matches = (c: InstrumentChoice) => {
    const text = c.kind === "sampler" ? "sampler samples drums" : `${c.plugin.name} ${c.plugin.manufacturer} ${c.plugin.category ?? ""} ${OWN_PLUGINS[c.plugin.name] ?? ""}`;
    return words.every((w) => text.toLowerCase().includes(w));
  };
  const first = [SAMPLER, ...own.map((plugin) => ({ kind: "plugin" as const, plugin }))].filter(matches);
  const byMaker = new Map<string, InstrumentChoice[]>();
  for (const plugin of others) {
    const choice: InstrumentChoice = { kind: "plugin", plugin };
    if (matches(choice)) byMaker.set(plugin.manufacturer || "Other", [...(byMaker.get(plugin.manufacturer || "Other") ?? []), choice]);
  }
  const pick = (choice: InstrumentChoice) => {
    onOpenChange(false);
    setQuery("");
    onPick(choice);
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange} title={title} className="w-[680px]">
      <input data-autofocus value={query} onChange={(e) => setQuery(e.target.value)} placeholder="Search instruments"
        className="mb-3 h-7 w-full rounded-md border border-line bg-control px-2 text-xs text-fg outline-none placeholder:text-faint focus:border-accent"
        onKeyDown={(e) => {
          const all = [...first, ...[...byMaker.values()].flat()];
          if (e.key === "Enter" && all.length) pick(all[0]);
        }} />
      <Section name="GrooveKit" choices={first} onPick={pick} />
      {[...byMaker.entries()].sort(([a], [b]) => a.localeCompare(b)).map(([maker, list]) => (
        <Section key={maker} name={maker} choices={list} onPick={pick} />
      ))}
      {plugins === null && <div className="py-2 text-2xs text-faint">Loading your plugins…</div>}
      {plugins?.length === 0 && <div className="py-2 text-2xs text-faint">No plugins scanned yet: Settings → Plugins finds the ones installed.</div>}
    </Dialog>
  );
}

function Section({ name, choices, onPick }: { name: string; choices: InstrumentChoice[]; onPick: (c: InstrumentChoice) => void }) {
  if (!choices.length) return null;
  return (
    <div className="mb-3">
      <div className="mb-1.5 text-2xs font-semibold tracking-wide text-faint uppercase">{name}</div>
      <div className="grid grid-cols-[repeat(auto-fill,88px)] gap-x-1 gap-y-2">
        {choices.map((c) => <InstrumentCard key={c.kind === "sampler" ? "sampler" : c.plugin.uid} choice={c} onPick={() => onPick(c)} />)}
      </div>
    </div>
  );
}

/** A 3:4 card: the instrument's panel when it has a picture, else a card drawn from its name. */
export function InstrumentCard({ choice, onPick }: { choice: InstrumentChoice; onPick: () => void }) {
  const picture = pictureOf(choice);
  const name = instrumentName(choice);
  const about = choice.kind === "sampler" ? "samples, one-shots, loops" : OWN_PLUGINS[choice.plugin.name] ?? choice.plugin.manufacturer;
  return (
    <button type="button" onClick={onPick} title={`${name}: ${about}`}
      className="group flex w-[88px] flex-col items-center gap-1 rounded-md p-1 text-left hover:bg-raised focus-visible:bg-raised focus-visible:outline-none">
      <div className="aspect-[3/4] w-20 overflow-hidden rounded-md border border-line shadow-md transition-transform group-hover:-translate-y-0.5 group-hover:border-accent">
        {picture ? <img src={picture} alt="" draggable={false} className="h-full w-full object-cover" /> : <DrawnCard choice={choice} />}
      </div>
      <div className="w-20 truncate text-2xs text-fg">{name}</div>
      <div className="-mt-1 w-20 truncate text-[9px] text-faint">{about}</div>
    </button>
  );
}

/** A card for an instrument without a picture: its colour, what kind it is, its initials. */
function DrawnCard({ choice }: { choice: InstrumentChoice }) {
  const name = instrumentName(choice);
  const category = choice.kind === "sampler" ? "sampler" : `${choice.plugin.category ?? ""} ${choice.plugin.name}`.toLowerCase();
  const Icon = choice.kind === "sampler" || category.includes("sampler") ? AudioWaveform
    : category.includes("drum") || category.includes("perc") ? Drum
    : category.includes("synth") || category.includes("instrument") ? KeyboardMusic : Puzzle;
  const color = choice.kind === "sampler" ? "#e9a23b" : colorFor(choice.plugin.manufacturer + choice.plugin.name);
  const initials = name.replace(/[^A-Za-z0-9 ]/g, " ").split(/\s+/).filter(Boolean).slice(0, 2).map((w) => w[0]).join("").toUpperCase();
  return (
    <div className="relative flex h-full w-full flex-col items-center justify-center gap-2"
      style={{ background: `linear-gradient(160deg, ${color}55, #18181c 70%)` }}>
      <Icon size={22} style={{ color }} />
      <span className={cn("font-semibold tracking-wider text-fg/80", initials.length > 1 ? "text-sm" : "text-base")}>{choice.kind === "sampler" ? "SMP" : initials}</span>
      <span className="absolute inset-x-1 bottom-1 truncate text-center text-[8px] text-faint uppercase">{choice.kind === "sampler" ? "built in" : choice.plugin.format}</span>
    </div>
  );
}
