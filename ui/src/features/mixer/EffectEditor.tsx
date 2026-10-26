import { Power, X } from "lucide-react";
import { Button, IconButton, Knob } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuTrigger } from "@/components/menu";
import { createAutomation } from "@/features/playlist/automation";
import { formatParam, useCatalogue } from "@/lib/catalogue";
import { call, execute, toast, useEngine } from "@/state/engine";
import { effectName } from "./Strip";

/** The parameters of one effect, as knobs from the engine's effect catalogue. */
export function EffectEditor({ insertId, effectId, onClose }: { insertId: string; effectId: string; onClose: () => void }) {
  const doc = useEngine((s) => s.doc)!;
  const catalogue = useCatalogue();
  const insert = doc.mixer.inserts.find((i) => i.id === insertId);
  const effect = insert?.effects?.find((e) => e.id === effectId);
  if (!insert || !effect) return null;

  const info = effect.type === "builtin" ? catalogue?.[effect.kind] : undefined;

  return (
    <div className="flex h-full flex-col">
      <div className="flex h-8 shrink-0 items-center gap-2 border-b border-line px-2">
        <button type="button" title={effect.bypass ? "Turn on" : "Bypass"} className={effect.bypass ? "text-faint" : "text-accent"}
          onClick={() => void execute({ type: "updateEffect", insert: insertId, effect: effectId, changes: { bypass: !effect.bypass } }, { label: "Bypass effect" })}>
          <Power size={13} />
        </button>
        <div className="flex-1 truncate text-sm font-medium">{effectName(effect)}</div>
        <IconButton size="sm" title="Close" onClick={onClose}><X size={12} /></IconButton>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto p-3">
        {effect.type === "builtin" ? (
          !info ? <div className="text-xs text-faint">Loading…</div> : (
            <>
              <div className="mb-3 text-xs text-dim">{info.description}</div>
              <div className="grid grid-cols-3 gap-x-2 gap-y-3">
                {Object.entries(info.params).map(([id, p]) => {
                  const value = effect.params?.[id] ?? p.default;
                  const log = p.unit === "Hz" && p.min > 0;
                  // Frequencies turn on a log scale, so the knob spends its travel evenly across octaves.
                  const toKnob = (v: number) => (log ? Math.log(v / p.min) / Math.log(p.max / p.min) : v);
                  const fromKnob = (k: number) => (log ? p.min * Math.pow(p.max / p.min, k) : k);
                  return (
                    <ContextMenu key={id}>
                      <ContextMenuTrigger asChild>
                        <div className="flex flex-col items-center" title={p.description}>
                          <Knob size={32} value={toKnob(value)} min={toKnob(p.min)} max={toKnob(p.max)} defaultValue={toKnob(p.default)}
                            format={(k) => formatParam(fromKnob(k), p.unit)}
                            onCommit={(k) => void execute({ type: "updateEffect", insert: insertId, effect: effectId, changes: { params: { [id]: round(fromKnob(k)) } } }, { label: `${effectName(effect)} ${id}` })} />
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
                })}
              </div>
            </>
          )
        ) : (
          <div className="flex flex-col gap-2 text-xs">
            <div className="text-dim">{effect.plugin.manufacturer} · {effect.plugin.format}</div>
            <div className="flex gap-1.5">
              <Button size="sm" className="bg-control text-fg" onClick={() => void call("plugins.openEditor", { insert: insertId, effect: effectId })}>Open plugin window</Button>
              <Button size="sm" onClick={() =>
                void call("plugins.captureState", { insert: insertId, effect: effectId }).then((r) => toast(r.changed ? `Saved the plugin's state (${Math.round(r.bytes / 1024)} KB)` : "Nothing changed since the last save"))}>
                Save state
              </Button>
            </div>
            <div className="text-2xs text-faint">Closing the plugin's window saves what you changed there into the project.</div>
          </div>
        )}
      </div>
    </div>
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
