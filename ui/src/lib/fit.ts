import type { Command } from "@groovekit/schema/protocol";
import type { Channel, Project, SamplerLayer } from "@groovekit/schema/project";
import { ticksPerBar } from "./model";

/** The furthest a sample is sped up or slowed down to fit the song (the engine's limit). */
export const MAX_SPEED = 8;

/** How a sample in the arrangement fits the song: played as recorded, time-stretched (pitch
    kept), or resampled (pitch follows speed). */
export type FitMode = "off" | "stretch" | "resample";

export function fitMode(layer: SamplerLayer | undefined): FitMode {
  return layer?.sourceTempo == null ? "off" : layer.resample ? "resample" : "stretch";
}

/** How much faster than recorded a layer plays in the song. */
export function fitSpeed(doc: Pick<Project, "tempo">, sourceTempo: number | undefined) {
  return sourceTempo ? Math.min(MAX_SPEED, Math.max(1 / MAX_SPEED, doc.tempo / sourceTempo)) : 1;
}

function beatsPerBar(doc: Project) {
  return ticksPerBar(doc) / 960;
}

/** The bars a part of `seconds` fills when it plays at `sourceTempo`. */
export function barsAt(doc: Project, seconds: number, sourceTempo: number) {
  return (seconds * sourceTempo) / 60 / beatsPerBar(doc);
}

/** The tempo that makes a part of `seconds` fill `bars`. */
export function tempoFor(doc: Project, seconds: number, bars: number) {
  return (bars * beatsPerBar(doc) * 60) / seconds;
}

/** The bars a part fills at the song's own speed. */
export function naturalBars(doc: Project, seconds: number) {
  return barsAt(doc, seconds, doc.tempo);
}

/** The bar count closest to the part's own length: a power of two (a half bar, 1, 2, 4...). */
export function suggestedBars(doc: Project, seconds: number) {
  return 2 ** Math.round(Math.log2(Math.max(1 / 16, Math.min(64, naturalBars(doc, seconds)))));
}

/** The bars it can fill without going past the speed limit either way. */
export function barRange(doc: Project, seconds: number) {
  const bars = naturalBars(doc, seconds);
  return { min: bars / MAX_SPEED, max: bars * MAX_SPEED };
}

export function formatBars(bars: number) {
  const fractions: Record<string, string> = { "0.125": "1/8", "0.25": "1/4", "0.5": "1/2", "0.75": "3/4" };
  const rounded = Math.round(bars * 1000) / 1000;
  const text = fractions[String(rounded)] ?? String(Math.round(bars * 100) / 100);
  return `${text} bar${rounded === 1 || rounded < 1 ? "" : "s"}`;
}

/** What fitting does to the sound: how much faster or slower, and by how much the pitch moves
    when resampled. */
export function describeFit(doc: Project, sourceTempo: number, resample: boolean) {
  const speed = fitSpeed(doc, sourceTempo);
  if (Math.abs(speed - 1) < 0.005) return "as recorded";
  const pace = `${speed > 1 ? speed.toFixed(2) : (1 / speed).toFixed(2)}× ${speed > 1 ? "faster" : "slower"}`;
  if (!resample) return `${pace}, pitch kept`;
  const semitones = 12 * Math.log2(speed);
  return `${pace}, ${semitones > 0 ? "+" : "−"}${Math.abs(semitones).toFixed(1)} st`;
}

/** The commands that fit a channel's first layer to the song (or stop fitting it), resizing its
    clips that played the whole part to the part's new length. */
export function fitCommands(doc: Project, channel: Channel, seconds: number, fit: { sourceTempo: number | null; resample: boolean }): Command[] {
  const layer = channel.instrument.type === "sampler" ? channel.instrument.layers[0] : undefined;
  if (!layer) return [];
  const ticks = (tempo: number | undefined) => (seconds * (doc.tempo / fitSpeed(doc, tempo)) * 960) / 60;
  const before = ticks(layer.sourceTempo), after = Math.max(1, Math.round(ticks(fit.sourceTempo ?? undefined)));
  const resized = doc.playlist.clips.filter((c) => c.type === "audio" && c.channel === channel.id && !c.offset && Math.abs(c.length - before) <= Math.max(2, before * 0.01));
  return [
    { type: "updateSamplerLayer", channel: channel.id, layer: layer.id,
      changes: { sourceTempo: fit.sourceTempo as number, resample: fit.sourceTempo != null && fit.resample ? true : (null as never) } },
    ...resized.map((c) => ({ type: "updateClip" as const, clip: c.id, changes: { length: after } })),
  ];
}
