import type { AutomationTarget, Project } from "@groovekit/schema/project";
import type { Catalogue } from "./catalogue";
import { effectNames } from "./catalogue";
import { insertName } from "./model";

export interface Range {
  min: number;
  max: number;
  unit: string;
  default: number;
  /** Frequencies are shown on a log scale. */
  log?: boolean;
}

/** Where a value sits in its range, 0..1. */
export function rangePosition(r: Range, v: number) {
  const x = Math.min(r.max, Math.max(r.min, v));
  return r.log ? Math.log(x / r.min) / Math.log(r.max / r.min) : (x - r.min) / (r.max - r.min || 1);
}

export function rangeValue(r: Range, t: number) {
  const c = Math.min(1, Math.max(0, t));
  return r.log ? r.min * Math.pow(r.max / r.min, c) : r.min + c * (r.max - r.min);
}

/** The value range of what an automation clip drives, in the parameter's own units. */
export function targetRange(target: AutomationTarget, doc: Project | undefined, catalogue: Catalogue | undefined): Range {
  if (target.param === "pan") return { min: -1, max: 1, unit: "", default: 0 };
  if (target.scope === "channel" || target.scope === "insert") return { min: -60, max: 12, unit: "dB", default: 0 };
  if (target.scope === "send") return { min: -60, max: 12, unit: "dB", default: 0 };

  if (target.param === "mix") return { min: 0, max: 1, unit: "", default: 1 };

  const insert = doc?.mixer.inserts.find((i) => i.id === target.insert);
  const effect = insert?.effects?.find((e) => e.id === target.effect);
  if (effect?.type === "builtin") {
    const p = catalogue?.[effect.kind]?.params[target.param];
    if (p) return { min: p.min, max: p.max, unit: p.unit, default: p.default, log: p.unit === "Hz" && p.min > 0 };
  }
  return { min: 0, max: 1, unit: "", default: 0.5 };
}

export function targetLabel(target: AutomationTarget, doc: Project | undefined) {
  const param = target.param === "volume" ? "Volume" : target.param === "pan" ? "Pan" : target.param === "level" ? "Level" : target.param === "mix" ? "Mix" : target.param;

  if (target.scope === "channel") {
    const ch = doc?.channels.find((c) => c.id === target.channel);
    return `${ch?.name ?? target.channel} · ${param}`;
  }

  const index = doc?.mixer.inserts.findIndex((i) => i.id === target.insert) ?? -1;
  const insert = insertName(doc?.mixer.inserts[index], index);

  if (target.scope === "effect") {
    const effect = doc?.mixer.inserts[index]?.effects?.find((e) => e.id === target.effect);
    const name = effect?.type === "builtin" ? effectNames[effect.kind] : effect?.type === "plugin" ? effect.plugin.name : target.effect;
    return `${insert} · ${name} · ${param}`;
  }

  if (target.scope === "send") return `${insert} · Send ${target.send} · ${param}`;
  return `${insert} · ${param}`;
}
