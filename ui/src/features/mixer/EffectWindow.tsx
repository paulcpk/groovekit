import type { BuiltinEffect, Insert } from "@groovekit/schema/project";
import { Power } from "lucide-react";
import { Knob } from "@/components/controls";
import { FloatingWindow } from "@/components/window";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuTrigger } from "@/components/menu";
import { createAutomation } from "@/features/playlist/automation";
import type { Project } from "@groovekit/schema/project";
import { formatParam, useCatalogue, type EffectParam } from "@/lib/catalogue";
import { useLiveControl } from "@/lib/liveControl";
import { insertName } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import type { OpenWindow } from "@/state/windows";
import { effectName } from "./Strip";

/** One of our effects in a window of its own: its controls, with the slot's on switch and mix
    knob in the title bar. */
export function EffectWindow({ window: w, insert, effect }: { window: OpenWindow; insert: Insert; effect: BuiltinEffect }) {
  const doc = useEngine((s) => s.doc)!;
  const catalogue = useCatalogue();
  const info = catalogue?.[effect.kind];
  const name = effectName(effect);
  const index = doc.mixer.inserts.indexOf(insert);
  const mix = useLiveControl((v) => ({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { mix: Math.round(v * 100) / 100 } }), `${name} mix`);

  return (
    <FloatingWindow window={w} width={460} accent="var(--color-accent)"
      title={
        <>
          <button type="button" title={effect.bypass ? "Turn on" : "Turn off"} className={effect.bypass ? "text-faint" : "text-accent"}
            onClick={() => void execute({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { bypass: !effect.bypass } }, { label: effect.bypass ? `Turn on ${name}` : `Turn off ${name}` })}>
            <Power size={13} />
          </button>
          <span className="truncate">{name}</span>
          <span className="truncate text-2xs font-normal text-faint">{insertName(insert, index)}</span>
        </>
      }
      extra={
        <div className="flex items-center gap-1.5 text-2xs text-dim" data-no-drag>
          Mix
          <Knob size={22} value={effect.mix ?? 1} min={0} max={1} defaultValue={1} format={(v) => `${Math.round(v * 100)}%`}
            onChange={mix.onChange} onCommit={mix.onCommit} />
          <span className="w-7 tabular">{Math.round((effect.mix ?? 1) * 100)}%</span>
        </div>
      }>
      <div className="p-3">
        {!info ? <div className="text-xs text-faint">Loading…</div> : (
          <>
            <div className="mb-3 text-xs text-dim">{info.description}</div>
            <div className="grid grid-cols-4 gap-x-2 gap-y-3">
              {Object.entries(info.params).map(([id, p]) => (
                <ParamKnob key={id} doc={doc} insertId={insert.id} effectId={effect.id} effectLabel={name} id={id} param={p} value={effect.params?.[id] ?? p.default} />
              ))}
            </div>
          </>
        )}
      </div>
    </FloatingWindow>
  );
}

function ParamKnob({ doc, insertId, effectId, effectLabel, id, param: p, value }: {
  doc: Project; insertId: string; effectId: string; effectLabel: string; id: string; param: EffectParam; value: number;
}) {
  const log = p.unit === "Hz" && p.min > 0;
  // Frequencies turn on a log scale, so the knob spends its travel evenly across octaves.
  const toKnob = (v: number) => (log ? Math.log(v / p.min) / Math.log(p.max / p.min) : v);
  const fromKnob = (k: number) => (log ? p.min * Math.pow(p.max / p.min, k) : k);
  const live = useLiveControl((k) => ({ type: "updateEffect", insert: insertId, effect: effectId, changes: { params: { [id]: round(fromKnob(k)) } } }), `${effectLabel} ${label(id)}`);

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        <div className="flex flex-col items-center" title={p.description}>
          <Knob size={32} value={toKnob(value)} min={toKnob(p.min)} max={toKnob(p.max)} defaultValue={toKnob(p.default)}
            format={(k) => formatParam(fromKnob(k), p.unit)} onChange={live.onChange} onCommit={live.onCommit} />
          <div className="mt-0.5 max-w-[84px] truncate text-2xs text-dim">{label(id)}</div>
          <div className="text-[9px] text-faint tabular">{formatParam(value, p.unit)}</div>
        </div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem onSelect={() => void execute({ type: "updateEffect", insert: insertId, effect: effectId, changes: { params: { [id]: p.default } } }, { label: "Reset parameter" })}>Reset</ContextMenuItem>
        {p.automatable && (
          <ContextMenuItem onSelect={() => void createAutomation(doc, { scope: "effect", insert: insertId, effect: effectId, param: id }, value)}>Create automation clip</ContextMenuItem>
        )}
      </ContextMenuContent>
    </ContextMenu>
  );
}

function round(v: number) {
  return Math.abs(v) >= 100 ? Math.round(v) : Math.round(v * 1000) / 1000;
}

/** "lowGainDb" → "Low gain". */
function label(id: string) {
  const words = id.replace(/Db$/, "").replace(/Ms$/, "").replace(/([A-Z0-9]+)/g, " $1").trim().toLowerCase();
  return words.charAt(0).toUpperCase() + words.slice(1);
}
