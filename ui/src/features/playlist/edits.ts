// What the playlist does to the song: committing a drag, placing a pattern, dropping a sample,
// making a pattern.

import type { AudioClip, Channel, Clip, PatternClip, Project } from "@groovekit/schema/project";
import { fitSpeed } from "@/lib/fit";
import { audioSource, baseName, patternLength, ticksPerBar, uniqueId } from "@/lib/model";
import { getPeaks } from "@/lib/peaks";
import { execute, reportError, toast } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import type { DraggedSample } from "./geometry";

/** A drag of clips in progress: which, and how far so far. */
export interface DragState {
  mode: "move" | "copy" | "resize" | "trim";
  ids: string[];
  dTicks: number;
  dRows: number;
}

export function secondsToTicks(seconds: number, clip: AudioClip, doc: Project) {
  return (seconds * (audioSource(doc, clip).sourceTempo ?? doc.tempo) * 960) / 60;
}

export async function commitDrag(state: DragState, doc: Project) {
  const clips = doc.playlist.clips.filter((c) => state.ids.includes(c.id));
  const n = clips.length;
  const what = n === 1 ? "clip" : `${n} clips`;
  const rowAfter = (c: Clip) => Math.min(999, Math.max(0, c.row + state.dRows));

  if (state.mode === "move") {
    if (state.dTicks === 0 && state.dRows === 0) return;
    await execute(clips.map((c) => ({
      type: "updateClip" as const, clip: c.id,
      changes: { start: c.start + state.dTicks, ...(state.dRows ? { row: rowAfter(c) } : {}) },
    })), { label: `Move ${what}` });
  } else if (state.mode === "copy") {
    const taken = new Set(doc.playlist.clips.map((c) => c.id));
    const copies = clips.map((c) => {
      const id = uniqueId(c.id.replace(/-\d+$/, ""), taken);
      taken.add(id);
      return { ...c, id, start: c.start + state.dTicks, row: rowAfter(c) };
    });
    await execute(copies.map((clip) => ({ type: "addClip" as const, clip })), { label: `Copy ${what}` });
    setUi({ selectedClips: copies.map((c) => c.id) });
  } else if (state.mode === "resize") {
    await execute(clips.map((c) => ({ type: "updateClip" as const, clip: c.id, changes: { length: c.length + state.dTicks } })), { label: `Resize ${what}` });
  } else {
    await execute(clips.map((c) => {
      const changes: Record<string, unknown> = { start: c.start + state.dTicks, length: c.length - state.dTicks };
      if (c.type === "audio") {
        changes.offset = Math.max(0, (c.offset ?? 0) + (state.dTicks * 60) / ((audioSource(doc, c).sourceTempo ?? doc.tempo) * 960));
      } else if (c.type === "pattern") {
        const pattern = doc.patterns.find((p) => p.id === c.pattern);
        const loop = pattern ? patternLength(pattern, doc) : 1;
        changes.offset = ((((c.offset ?? 0) + state.dTicks) % loop) + loop) % loop;
      }
      return { type: "updateClip" as const, clip: c.id, changes };
    }), { label: `Trim ${what}` });
  }
}

/** Places the selected pattern as a clip. */
export function placePattern(doc: Project, row: number, start: number) {
  const { selectedPattern } = useUi.getState();
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);
  setUi({ selectedClips: [] });

  if (!pattern) {
    toast("Make a pattern in the channel rack first");
    return;
  }

  const clip: PatternClip = { type: "pattern", id: uniqueId(pattern.id, doc.playlist.clips), row, start, length: patternLength(pattern, doc), pattern: pattern.id };
  void execute({ type: "addClip", clip }, { label: `Place ${pattern.name ?? pattern.id}` });
}

/** A sample dropped onto the playlist becomes a clip of a sampler channel listed with the
    arrangement's: one that already plays the whole file as it is, or a new one. */
export async function dropSample(doc: Project, sample: DraggedSample, row: number, start: number) {
  try {
    const duration = sample.durationSeconds ?? (await getPeaks(sample.path)).durationSeconds;
    // Loops fit the song as set (by their own tempo, so they fill as many bars as they're long).
    const fit = useUi.getState().loopDrop;
    const loopTempo = sample.loop && sample.tempo && fit !== "off" ? sample.tempo : undefined;
    const resample = !!loopTempo && fit === "resample";
    const length = Math.max(1, Math.round((duration * (doc.tempo / fitSpeed(doc, loopTempo)) * 960) / 60));
    const existing = doc.channels.find((c) => {
      const layer = c.instrument.type === "sampler" && c.group === "arrangement" ? c.instrument.layers[0] : undefined;
      return layer && layer.sample === sample.path && !layer.reverse && !layer.start && layer.end === undefined
        && !layer.transpose && !layer.fineTune && layer.sourceTempo === loopTempo && !!layer.resample === resample;
    });
    const channel: Channel = existing ?? {
      id: uniqueId(baseName(sample.path).replace(/[^A-Za-z0-9_.-]+/g, "-").slice(0, 56) || "audio", doc.channels),
      name: baseName(sample.path),
      group: "arrangement",
      instrument: { type: "sampler", layers: [{ id: "sample", sample: sample.path, ...(loopTempo ? { sourceTempo: loopTempo } : {}), ...(resample ? { resample } : {}) }] },
    };
    const clip: AudioClip = { type: "audio", id: uniqueId(baseName(sample.path), doc.playlist.clips), row, start, length, channel: channel.id };
    await execute([...(existing ? [] : [{ type: "addChannel" as const, channel }]), { type: "addClip", clip }], { label: `Add ${baseName(sample.path)}` });
    setUi({ selectedClips: [clip.id] });
    return clip;
  } catch (e) {
    reportError(e);
    return undefined;
  }
}

export async function newPattern(doc: Project) {
  const id = uniqueId("pattern", doc.patterns);
  const n = doc.patterns.length + 1;
  await execute({ type: "addPattern", pattern: { id, name: `Pattern ${n}`, length: ticksPerBar(doc), notes: [] } }, { label: "New pattern" });
  setUi({ selectedPattern: id });
}
