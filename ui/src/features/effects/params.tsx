import type { BuiltinEffect, Insert } from "@groovekit/schema/project";
import { Knob } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuTrigger } from "@/components/menu";
import { createAutomation } from "@/features/playlist/automation";
import { effectNames, formatParam, useCatalogue, type EffectParam } from "@/lib/catalogue";
import { cn } from "@/lib/cn";
import { useLiveControl } from "@/lib/liveControl";
import { execute, useEngine } from "@/state/engine";

/** A parameter's value: as set, or its default. */
export function paramValue(effect: BuiltinEffect, id: string, param: EffectParam | undefined) {
  return effect.params?.[id] ?? param?.default ?? 0;
}

/** One parameter of one of our effects: a knob, or buttons for a choice. Right-click to reset
    or automate it. */
export function ParamKnob({ insert, effect, id, param: given, name, size = 32 }: {
  insert: Insert; effect: BuiltinEffect; id: string; param?: EffectParam; name?: string; size?: number;
}) {
  const doc = useEngine((s) => s.doc)!;
  const catalogue = useCatalogue();
  const p = given ?? catalogue?.[effect.kind]?.params[id];
  const value = paramValue(effect, id, p);
  const log = p?.unit === "Hz" && p.min > 0;
  // Frequencies turn on a log scale, so the knob spends its travel evenly across octaves.
  const toKnob = (v: number) => (p && log ? Math.log(v / p.min) / Math.log(p.max / p.min) : v);
  const fromKnob = (k: number) => (p && log ? p.min * Math.pow(p.max / p.min, k) : k);
  const live = useLiveControl((k) => ({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { params: { [id]: round(fromKnob(k)) } } }),
    `${effectNames[effect.kind] ?? effect.kind} ${name ?? label(id)}`);
  if (!p) return null;

  const set = (v: number) => void execute({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { params: { [id]: v } } }, { label: `${name ?? label(id)}` });

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        {p.choices && p.choices.length > 5 ? (
          <div className="flex flex-col items-center gap-1" title={p.description}>
            <select value={Math.round(value)} onChange={(e) => set(Number(e.target.value))}
              className="h-6 rounded-[3px] bg-control px-1 text-xs text-fg outline-none">
              {p.choices.map((c, i) => <option key={c} value={i}>{c === "off" ? "Off" : c}</option>)}
            </select>
            <div className="text-2xs text-dim">{name ?? label(id)}</div>
          </div>
        ) : p.choices ? (
          <div className="flex flex-col items-center gap-1" title={p.description}>
            <div className="flex flex-wrap justify-center gap-0.5">
              {p.choices.map((c, i) => (
                <button key={c} type="button" onClick={() => set(i)}
                  className={cn("h-5 rounded-[3px] px-1.5 text-2xs", Math.round(value) === i ? "bg-accent text-black" : "bg-control text-dim hover:text-fg")}>
                  {c === "off" ? "Off" : c === "on" ? "On" : c}
                </button>
              ))}
            </div>
            <div className="text-2xs text-dim">{name ?? label(id)}</div>
          </div>
        ) : (
          <div className="flex flex-col items-center" title={p.description}>
            <Knob size={size} value={toKnob(value)} min={toKnob(p.min)} max={toKnob(p.max)} defaultValue={toKnob(p.default)}
              format={(k) => formatParam(fromKnob(k), p.unit)} onChange={live.onChange} onCommit={live.onCommit} />
            <div className="mt-0.5 max-w-[84px] truncate text-2xs text-dim">{name ?? label(id)}</div>
            <div className="text-[9px] text-faint tabular">{formatParam(value, p.unit)}</div>
          </div>
        )}
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem onSelect={() => set(p.default)}>Reset</ContextMenuItem>
        {p.automatable && (
          <ContextMenuItem onSelect={() => void createAutomation(doc, { scope: "effect", insert: insert.id, effect: effect.id, param: id }, value)}>Create automation clip</ContextMenuItem>
        )}
      </ContextMenuContent>
    </ContextMenu>
  );
}

function round(v: number) {
  return Math.abs(v) >= 100 ? Math.round(v) : Math.round(v * 1000) / 1000;
}

/** "lowGainDb" → "Low gain". */
export function label(id: string) {
  const words = id.replace(/Db$/, "").replace(/Ms$/, "").replace(/([A-Z0-9]+)/g, " $1").trim().toLowerCase();
  return words.charAt(0).toUpperCase() + words.slice(1);
}
