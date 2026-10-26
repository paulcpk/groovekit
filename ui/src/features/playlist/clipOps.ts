import type { AudioClip, AutomationPoint, Clip, Project } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { audioSource, uniqueId } from "@/lib/model";
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
