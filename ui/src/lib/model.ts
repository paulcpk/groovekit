import type { AudioClip, Clip, Insert, Pattern, Project } from "@groovekit/schema/project";
import { fitSpeed } from "./fit";

export const PPQ = 960;
export const STEP = PPQ / 4; // a 16th

export function ticksPerBeat(doc: Project | undefined) {
  return (PPQ * 4) / (doc?.timeSignature?.denominator ?? 4);
}

export function ticksPerBar(doc: Project | undefined) {
  return ticksPerBeat(doc) * (doc?.timeSignature?.numerator ?? 4);
}

/** "bar.beat.16th", all from 1, as DAWs show positions. */
export function formatPosition(ticks: number, doc: Project | undefined) {
  const bar = ticksPerBar(doc);
  const beat = ticksPerBeat(doc);
  const t = Math.max(0, Math.round(ticks));
  const b = Math.floor(t / bar);
  const be = Math.floor((t % bar) / beat);
  const s = Math.floor((t % beat) / STEP);
  return `${b + 1}.${be + 1}.${s + 1}`;
}

export function formatTime(seconds: number) {
  const s = Math.max(0, seconds);
  const m = Math.floor(s / 60);
  return `${m}:${(s % 60).toFixed(2).padStart(5, "0")}`;
}

/** The loop length the engine uses: the pattern's own, or its last note's end rounded up to a bar. */
export function patternLength(pattern: Pattern, doc: Project | undefined) {
  if (pattern.length) return pattern.length;
  const bar = ticksPerBar(doc);
  const end = pattern.notes.reduce((m, n) => Math.max(m, n.start + n.length), 0);
  return Math.max(bar, Math.ceil(end / bar) * bar);
}

/** The first of base, base-2, base-3 ... that no item uses. */
export function uniqueId(base: string, taken: Iterable<{ id: string }> | Set<string>) {
  const ids = taken instanceof Set ? taken : new Set([...taken].map((t) => t.id));
  const clean = base.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "item";
  if (!ids.has(clean)) return clean;
  for (let n = 2; ; n++) if (!ids.has(`${clean}-${n}`)) return `${clean}-${n}`;
}

export function allClipIds(doc: Project) {
  return new Set(doc.playlist.clips.map((c) => c.id));
}

export function fileName(path: string) {
  return path.split("/").pop() ?? path;
}

export function baseName(path: string) {
  return fileName(path).replace(/\.[^.]+$/, "");
}

export function insertName(insert: Insert | undefined, index?: number) {
  if (!insert) return "—";
  if (insert.id === "master") return "Master";
  return insert.name ?? (index !== undefined ? `Insert ${index}` : insert.id);
}

export function clipEnd(clip: Clip) {
  return clip.start + clip.length;
}

export function songEnd(doc: Project | undefined) {
  return doc ? doc.playlist.clips.reduce((m, c) => Math.max(m, clipEnd(c)), 0) : 0;
}

//==============================================================================
// Muted clip colours: readable text on top, calm in large areas.
export const palette = [
  "#e8915a", "#e3b448", "#b8c35c", "#7cc47c", "#5ec4a9", "#58b7d6",
  "#6f9de8", "#8f86e3", "#b57ad8", "#d673b3", "#df6f7f", "#a6a39b",
];

function hash(s: string) {
  let h = 0;
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0;
  return Math.abs(h);
}

export function colorFor(id: string, explicit?: string) {
  return explicit ?? palette[hash(id) % palette.length];
}

export function dbToGain(db: number) {
  return Math.pow(10, db / 20);
}

export function gainToDb(gain: number) {
  return gain <= 0 ? -Infinity : 20 * Math.log10(gain);
}

export function formatDb(db: number | undefined) {
  if (db === undefined) return "0.0";
  if (!Number.isFinite(db) || db <= -100) return "-∞";
  return (db > 0 ? "+" : "") + db.toFixed(1);
}

export function formatPan(pan: number | undefined) {
  const p = pan ?? 0;
  if (Math.abs(p) < 0.005) return "C";
  return `${Math.round(Math.abs(p) * 100)}${p < 0 ? "L" : "R"}`;
}

export function noteName(key: number) {
  const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  return `${names[key % 12]}${Math.floor(key / 12) - 1}`;
}

export function newGroup() {
  return `ui-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`;
}

/** The snap step in ticks: 0 stands for a bar. */
export function snapTicks(snap: number, doc: Project | undefined) {
  return snap === 0 ? ticksPerBar(doc) : snap;
}

/** What an audio clip plays: its channel's first sampler layer, the part of the file it cuts out,
    and the tempo the file plays at in the song when it's fitted to it (within the speed limit). */
export function audioSource(doc: Project, clip: AudioClip) {
  const channel = doc.channels.find((c) => c.id === clip.channel);
  const layer = channel?.instrument.type === "sampler" ? channel.instrument.layers[0] : undefined;
  return {
    channel, layer, sample: layer?.sample, reverse: !!layer?.reverse,
    sourceTempo: layer?.sourceTempo ? doc.tempo / fitSpeed(doc, layer.sourceTempo) : undefined,
    start: layer?.start ?? 0, end: layer?.end,
  };
}

/** The number of playlist tracks to show: every one in use or set up, a screenful at least,
    and a few spare below. */
export function laneCount(doc: Project) {
  const used = Math.max(doc.playlist.tracks.length, ...doc.playlist.clips.map((c) => c.track + 1));
  return Math.max(16, used + 4);
}

/** The first track from `from` with no clips. */
export function firstEmptyLane(doc: Project, from = 0) {
  const used = new Set(doc.playlist.clips.map((c) => c.track));
  let lane = from;
  while (used.has(lane)) lane++;
  return lane;
}
