import type { BuiltinEffect, Insert } from "@groovekit/schema/project";
import { Power } from "lucide-react";
import { Knob } from "@/components/controls";
import { FloatingWindow } from "@/components/window";
import { EqEditor } from "@/features/effects/EqEditor";
import { BitcrusherEditor } from "@/features/effects/BitcrusherEditor";
import { CompressorEditor } from "@/features/effects/CompressorEditor";
import { ChorusEditor, DelayEditor, DistortionEditor, ReverbEditor } from "@/features/effects/editors";
import { ExciterEditor } from "@/features/effects/ExciterEditor";
import { MultibandEditor } from "@/features/effects/MultibandEditor";
import { StereoEditor } from "@/features/effects/StereoEditor";
import { ParamKnob } from "@/features/effects/params";
import { PresetMenu } from "@/features/effects/PresetMenu";
import { useCatalogue } from "@/lib/catalogue";
import { useLiveControl } from "@/lib/liveControl";
import { insertName } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import type { OpenWindow } from "@/state/windows";
import { isSendTarget } from "./InsertPanel";
import { effectName } from "./Strip";

/** Effects with an editor of their own; the rest get a knob per parameter. */
const EDITORS: Partial<Record<string, (props: { insert: Insert; effect: BuiltinEffect }) => React.ReactNode>> = {
  eq: (props) => <EqEditor {...props} />,
  delay: (props) => <DelayEditor {...props} />,
  reverb: (props) => <ReverbEditor {...props} />,
  chorus: (props) => <ChorusEditor {...props} />,
  distortion: (props) => <DistortionEditor {...props} />,
  compressor: (props) => <CompressorEditor {...props} />,
  multiband: (props) => <MultibandEditor {...props} />,
  exciter: (props) => <ExciterEditor {...props} />,
  bitcrusher: (props) => <BitcrusherEditor {...props} />,
  stereo: (props) => <StereoEditor {...props} />,
};

/** Wider windows for the editors that need the room. */
const WIDTHS: Partial<Record<string, number>> = { multiband: 900, exciter: 720, stereo: 720 };

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
    <FloatingWindow window={w} width={WIDTHS[effect.kind] ?? (EDITORS[effect.kind] ? 680 : 460)} accent="var(--color-accent)"
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
          <PresetMenu kind={effect.kind}
            current={() => ({ params: effect.params ?? {}, mix: effect.mix ?? 1 })}
            apply={(preset) => {
              // Every parameter: the preset's, or its default, so nothing of the old setting stays.
              const params = Object.fromEntries(Object.entries(info?.params ?? {}).map(([id, p]) => [id, (preset.params as Record<string, number> | undefined)?.[id] ?? p.default]));
              // On a send bus the effect stays fully wet; elsewhere it takes the preset's mix.
              const mix = typeof preset.mix === "number" && !isSendTarget(doc, insert.id) ? { mix: preset.mix } : {};
              void execute({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { params, ...mix } },
                { label: `${name} preset: ${preset.name}` });
            }} />
          <span className="ml-1.5">Mix</span>
          <Knob size={22} value={effect.mix ?? 1} min={0} max={1} defaultValue={1} format={(v) => `${Math.round(v * 100)}%`}
            onChange={mix.onChange} onCommit={mix.onCommit} />
          <span className="w-7 tabular">{Math.round((effect.mix ?? 1) * 100)}%</span>
        </div>
      }>
      <div className="p-3">
        {EDITORS[effect.kind] ? EDITORS[effect.kind]!({ insert, effect }) : !info ? <div className="text-xs text-faint">Loading…</div> : (
          <>
            <div className="mb-3 text-xs text-dim">{info.description}</div>
            <div className="grid grid-cols-4 gap-x-2 gap-y-3">
              {Object.entries(info.params).map(([id, p]) => (
                <ParamKnob key={id} insert={insert} effect={effect} id={id} param={p} />
              ))}
            </div>
          </>
        )}
      </div>
    </FloatingWindow>
  );
}

