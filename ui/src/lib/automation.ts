import type { AutomationTarget, Project } from "@groovekit/schema/project";
import type { Catalogue } from "./catalogue";
import { effectNames } from "./catalogue";
import { useMemo } from "react";
import { useEngine } from "@/state/engine";
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
  if (target.scope === "channel" && channelParams[target.param]) return channelParams[target.param].range;
  if (target.scope === "channel" && target.param !== "volume") return { min: 0, max: 1, unit: "", default: 0.5 };
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

/** A sampler channel's parameters beyond volume and pan that automation can move. */
export const channelParams: Record<string, { label: string; range: Range }> = {
  filterCutoff: { label: "Filter cutoff", range: { min: 20, max: 20000, unit: "Hz", default: 20000, log: true } },
  filterResonance: { label: "Filter resonance", range: { min: 0, max: 1, unit: "", default: 0 } },
  filterEnvelopeAmount: { label: "Filter env amount", range: { min: -10, max: 10, unit: "oct", default: 0 } },
};

export function targetLabel(target: AutomationTarget, doc: Project | undefined) {
  const param = target.param === "volume" ? "Volume" : target.param === "pan" ? "Pan" : target.param === "level" ? "Level" : target.param === "mix" ? "Mix"
    : target.scope === "channel" && channelParams[target.param] ? channelParams[target.param].label : target.param;

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

//==============================================================================
// What automation sets a parameter to at a moment, so controls turn with it as the song plays.

function sameTarget(a: AutomationTarget, b: AutomationTarget) {
  return a.scope === b.scope && a.param === b.param && a.channel === b.channel && a.insert === b.insert && a.effect === b.effect && a.send === b.send;
}

/** A segment shaped like the engine's: straight, or a quadratic bezier through a control point
    pulled by the curve (twice it, up to ±1), as Tracktion draws it. */
function segment(x1: number, y1: number, x2: number, y2: number, curve: number, x: number) {
  if (x2 <= x1) return y2;
  const c = Math.max(-1, Math.min(1, curve * 2));
  if (c === 0) return y1 + ((x - x1) / (x2 - x1)) * (y2 - y1);

  const run = x2 - x1;
  const rise = Math.abs(y2 - y1);
  const xc = x1 + run / 2 + (run / 2) * c;
  const yc = y2 > y1 ? y1 + rise / 2 - (rise / 2) * c : y2 + rise / 2 + (rise / 2) * c;

  // x(t) = (1-t)²·x1 + 2(1-t)t·xc + t²·x2: solve for t, then y(t).
  const a = x1 - 2 * xc + x2, b = 2 * (xc - x1), k = x1 - x;
  const t = Math.abs(a) < 1e-9 ? -k / b : (-b + Math.sqrt(Math.max(0, b * b - 4 * a * k))) / (2 * a);
  const u = Math.max(0, Math.min(1, t));
  return (1 - u) * (1 - u) * y1 + 2 * (1 - u) * u * yc + u * u * y2;
}

/** The value automation gives a target at a tick, or undefined when no automation clip that's
    heard drives it. As in the engine: the points of all its clips (muted clips and rows aside, a
    solo leaving only soloed rows), the first value before the first point, a clip's last value
    held until the next clip's first point, the last value after the end. */
export function automationValueAt(doc: Project | undefined, target: AutomationTarget, tick: number): number | undefined {
  if (!doc) return undefined;
  const rows = doc.playlist.rows ?? [];
  const anySolo = rows.some((r) => r?.solo);
  const heard = (row: number) => !(rows[row]?.mute || (anySolo && !rows[row]?.solo));

  const clips = doc.playlist.clips
    .flatMap((c) => (c.type === "automation" && !c.mute && heard(c.row) && sameTarget(c.target, target) ? [c] : []))
    .map((c) => ({ c, points: [...c.points].filter((p) => p.time < c.length).sort((a, b) => a.time - b.time) }))
    .filter((x) => x.points.length > 0)
    .sort((a, b) => a.c.start + a.points[0].time - (b.c.start + b.points[0].time));

  const points = clips.flatMap(({ c, points }, i) => points.map((p) => ({ at: c.start + p.time, value: p.value, curve: p.curve ?? 0, clip: i })));
  if (points.length === 0) return undefined;
  if (tick <= points[0].at) return points[0].value;

  for (let i = 0; i + 1 < points.length; i++) {
    const p = points[i], q = points[i + 1];
    if (tick >= q.at) continue;
    // Between clips the last value holds; within one the segment runs to the next point.
    return p.clip !== q.clip ? p.value : segment(p.at, p.value, q.at, q.value, p.curve, tick);
  }

  return points[points.length - 1].value;
}

/** A control's value: what automation sets it to at the playhead, else the value as set. */
export function useAutomated(target: AutomationTarget, value: number) {
  const automated = useEngine((s) => automationValueAt(s.doc, target, s.transport.position));
  return automated ?? value;
}

/** An effect as automation has it at the playhead: its mix and parameters with automated values
    in place of the ones set, so its window turns with the song. */
export function useAutomatedEffect<E extends { id: string; mix?: number; params?: Record<string, number> }>(insert: string, effect: E): E {
  const values = useEngine((s) => {
    const doc = s.doc;
    if (!doc) return "";
    const params = new Set<string>();
    for (const c of doc.playlist.clips)
      if (c.type === "automation" && c.target.scope === "effect" && c.target.insert === insert && c.target.effect === effect.id) params.add(c.target.param);
    const out: Record<string, number> = {};
    for (const param of params) {
      const v = automationValueAt(doc, { scope: "effect", insert, effect: effect.id, param }, s.transport.position);
      if (v !== undefined) out[param] = v;
    }
    return JSON.stringify(out);
  });

  return useMemo(() => {
    const automated = values ? (JSON.parse(values) as Record<string, number>) : {};
    if (Object.keys(automated).length === 0) return effect;
    const { mix, ...params } = automated;
    return { ...effect, ...(mix !== undefined ? { mix } : {}), params: { ...(effect.params ?? {}), ...params } };
  }, [effect, values]);
}
