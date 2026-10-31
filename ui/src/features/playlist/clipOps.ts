import type { AudioClip, AutomationPoint, Clip, Project } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { copiedClips, copyClips } from "@/lib/clipClipboard";
import { audioSource, ticksPerBar, uniqueId } from "@/lib/model";
import { PasteTrail, placement, type Span } from "@/lib/placement";
import { execute, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

/** The value an automation clip holds at a time (linear between points). */
function valueAt(points: AutomationPoint[], time: number) {
  const sorted = [...points].sort((a, b) => a.time - b.time);
  if (time <= sorted[0].time) return sorted[0].value;
  for (let i = 1; i < sorted.length; i++) {
    const a = sorted[i - 1], b = sorted[i];
    if (time <= b.time) return a.value + ((b.value - a.value) * (time - a.time)) / Math.max(1, b.time - a.time);
  }
  return sorted[sorted.length - 1].value;
}

/** Commands that cut a clip in two at a song position (ticks), and the new clip's id. */
export function splitCommands(clip: Clip, at: number, doc: Project, taken: Set<string>): { commands: Command[]; id: string } | null {
  const cut = Math.round(at - clip.start);
  if (cut <= 0 || cut >= clip.length) return null;

  const id = uniqueId(clip.id.replace(/-\d+$/, ""), taken);
  taken.add(id);
  const second = { ...clip, id, start: clip.start + cut, length: clip.length - cut } as Clip;
  const firstChanges: Record<string, unknown> = { length: cut };

  if (second.type === "pattern" && clip.type === "pattern") {
    second.offset = (clip.offset ?? 0) + cut;
  } else if (second.type === "audio" && clip.type === "audio") {
    // offset is in the file's own seconds, which a stretched clip plays at its source tempo.
    const fileSecondsPerTick = 60 / ((audioSource(doc, clip).sourceTempo ?? doc.tempo) * 960);
    second.offset = Math.round(((clip.offset ?? 0) + cut * fileSecondsPerTick) * 1e6) / 1e6;
    second.fadeIn = 0;
    if (clip.fadeOut) firstChanges.fadeOut = 0;
  } else if (second.type === "automation" && clip.type === "automation") {
    const hold = valueAt(clip.points, cut);
    const before = clip.points.filter((p) => p.time < cut);
    const after = clip.points.filter((p) => p.time > cut).map((p) => ({ ...p, time: p.time - cut }));
    firstChanges.points = [...before, { time: cut, value: hold }];
    second.points = [{ time: 0, value: hold }, ...after];
  }

  return {
    id,
    commands: [
      { type: "updateClip", clip: clip.id, changes: firstChanges },
      { type: "addClip", clip: second },
    ],
  };
}

/** Splits clips at a position: the given ones, or those selected, or any under the position. */
export function splitClips(at: number, ids?: string[]) {
  const doc = useEngine.getState().doc;
  if (!doc) return;
  const chosen = ids ?? useUi.getState().selectedClips;
  const clips = doc.playlist.clips.filter((c) => (chosen.length ? chosen.includes(c.id) : true) && c.start < at && c.start + c.length > at);

  if (!clips.length) {
    toast("No clip under the playhead to split");
    return;
  }

  const taken = new Set(doc.playlist.clips.map((c) => c.id));
  const parts = clips.map((c) => splitCommands(c, at, doc, taken)).filter((p) => p !== null);
  void execute(parts.flatMap((p) => p.commands), { label: parts.length === 1 ? "Split clip" : `Split ${parts.length} clips` });
  setUi({ selectedClips: [...clips.map((c) => c.id), ...parts.map((p) => p.id)] });
}

/** Splits at the playhead (song mode). */
export function splitAtPlayhead() {
  const { transport } = useEngine.getState();
  if (transport.mode === "pattern") {
    toast("Switch to song mode to split clips at the playhead");
    return;
  }
  splitClips(transport.position);
}

/** Gives an audio clip a copy of its channel, so its settings can change without the other clips'. */
export async function makeAudioUnique(doc: Project, clip: AudioClip) {
  const channel = doc.channels.find((c) => c.id === clip.channel);
  if (!channel) return;
  const id = uniqueId(channel.id, doc.channels);
  await execute([
    { type: "addChannel", channel: { ...channel, id, name: `${channel.name || channel.id} 2` }, index: doc.channels.indexOf(channel) + 1 },
    { type: "updateClip", clip: clip.id, changes: { channel: id } },
  ], { label: "Make clip unique" });
}

/** Copies of clips with ids of their own (pattern-2, pattern-3...), changed as given. */
export function copiesOf(doc: Project, clips: Clip[], change: (clip: Clip) => Partial<Clip>): Clip[] {
  const taken = new Set(doc.playlist.clips.map((c) => c.id));
  return clips.map((c) => {
    const id = uniqueId(c.id.replace(/-\d+$/, ""), taken);
    taken.add(id);
    return { ...c, ...change(c), id } as Clip;
  });
}

const spans = (clips: Clip[]): Span[] => clips.map((c) => ({ lane: c.row, start: c.start, length: c.length }));

/** Where the clips pasted last went, so pasting them again goes on after them. */
const pastes = new PasteTrail();

/** Adds copies of clips moved by `shift` (from the placement rule: their own times when nothing
    in their rows is in the way, else from the next bar line after them), in their rows. The
    copies become the selection; it all undoes in one step. */
function addCopies(doc: Project, clips: Clip[], shift: number, verb: string) {
  const copies = copiesOf(doc, clips, (c) => ({ start: c.start + shift }));
  void execute(copies.map((clip) => ({ type: "addClip" as const, clip })), { label: `${verb} ${copies.length === 1 ? "clip" : `${copies.length} clips`}` });
  setUi({ selectedClips: copies.map((c) => c.id) });
}

/** The selected clips, in the song's order. */
function selectedClips(doc: Project) {
  const ids = useUi.getState().selectedClips;
  return doc.playlist.clips.filter((c) => ids.includes(c.id));
}

/** ⌘C: the selected clips onto the clipboard. */
export function copySelectedClips() {
  const doc = useEngine.getState().doc;
  const clips = doc ? selectedClips(doc) : [];
  if (clips.length) copyClips(clips);
  return clips.length > 0;
}

/** ⌘V: the copied clips back into the playlist, each paste after the last. */
export function pasteClips() {
  const doc = useEngine.getState().doc;
  const clips = copiedClips();
  if (!doc || !clips.length) return false;
  addCopies(doc, clips, pastes.place("playlist", spans(clips), spans(doc.playlist.clips), ticksPerBar(doc)), "Paste");
  return true;
}

/** ⌘D, or Duplicate in a clip's menu: copies of the clips (the selection unless given) placed
    as a paste would place them, in one step. */
export function duplicateClips(clips?: Clip[]) {
  const doc = useEngine.getState().doc;
  const chosen = clips ?? (doc ? selectedClips(doc) : []);
  if (!doc || !chosen.length) return false;
  addCopies(doc, chosen, placement(spans(chosen), spans(doc.playlist.clips), ticksPerBar(doc)), "Duplicate");
  return true;
}

/** Delete: the selected clips out of the song, in one step. */
export function deleteSelectedClips() {
  const doc = useEngine.getState().doc;
  const clips = doc ? selectedClips(doc) : [];
  if (!clips.length) return false;
  void execute(clips.map((c) => ({ type: "removeClip" as const, clip: c.id })), { label: clips.length === 1 ? "Delete clip" : `Delete ${clips.length} clips` });
  setUi({ selectedClips: [] });
  return true;
}
