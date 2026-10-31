// The song maps in the UI: every reference track, how it stands, and the hearing queue, kept up
// to date by "listening" notifications.

import { create } from "zustand";
import { call, engine, listen } from "@/state/engine";
import { setUi } from "@/state/ui";

export type SongStatus = "new" | "changed" | "older" | "heard" | "queued" | "hearing" | "failed" | "skipped" | "imported" | "gone";

export interface Song {
  id: string;
  style: string;
  title: string;
  /** The audio, when it's on this Mac. */
  file?: string;
  status: SongStatus;
  inUse: boolean;
  error?: string;
  map?: { source: "heard" | "imported"; heardAt: number; analysis: number; tempo: number; lufs: number };
}

export interface QueueItem { id: string; style: string; title: string }

export interface Hearing {
  running: boolean;
  paused: boolean;
  /** Whether the listening models are in: without them the queue waits. */
  ready: boolean;
  current?: QueueItem & { progress: number; started: number };
  queued: QueueItem[];
  recent: (QueueItem & { ok: boolean; seconds: number; at: number; error?: string })[];
  secondsPerSong: number;
}

/** A song's map as the engine keeps it (sound fingerprints only counted). */
export interface SongMap {
  id: string;
  style: string;
  title: string;
  source: "heard" | "imported";
  heardAt: number;
  inUse: boolean;
  /** Heard by the current hearing. */
  current: boolean;
  track: {
    path: string; analysis: number; tempo: number; swing?: number; density: number; lufs: number;
    balance: number[]; levels: Record<string, number>; sounds: Record<string, number>;
    arrangement?: Arrangement;
    /** The kick and rumble, measured where the kick plays with least around it. */
    kick?: { sound: KickSound; bars: [number, number]; clean: boolean };
    /** What the music and the bass play where they're loudest: two bars each on the 16th grid. */
    figures?: Figure[];
    /** Every melodic part through the track and how they go together (hearing 9 on). */
    voices?: { voices: Voice[]; pairs: Pairing[]; key?: { key: string; confidence: number } };
  };
  /** The arrangement as the agent reads it. */
  score?: string;
}

export interface Figure {
  part: string;
  bar: number;
  bars: number;
  section?: string;
  key?: { key: string; confidence: number };
  notes: { start: number; length: number; key: number; velocity: number }[];
}

/** A melodic part heard through a track: runs of a figure (steps of 16ths from the figure's
    start; notes as [step, steps, key, velocity]) and the bars each repeats over. */
export interface Voice {
  role: string;
  part: string;
  sound: string;
  range: [number, number];
  level: number;
  chord: number;
  notesPerBar: number;
  runs: { from: number; to: number; bars: number; notes: [number, number, number, number][]; change?: string; variations?: number }[];
}

/** How two voices go together: b answers a when answerAfter is there. */
export interface Pairing {
  a: number;
  b: number;
  bars: number;
  together: number;
  fills: number;
  answerAfter?: number;
  intervals: [number, number][];
}

export interface KickSound {
  bodyHz: number; attackMs: number; decayMs: number; clickDb: number; gritDb: number; width: number;
  rumbleDb?: number; rumbleGritDb?: number; spectrum: number[]; pattern?: string; hits: number;
}

/** A library sample that sounds like a reference's kick: score 0-100 (measured 75%, fingerprint
    25%), and what differs most. */
export interface KickMatch {
  path: string; name: string; category: string; loop: boolean; tempo?: number;
  score: number; measured: number; fingerprint?: number; differences: string[]; kick: string;
}

export const matchKicks = (id: string, loops?: boolean) =>
  call<{ results: KickMatch[]; searched: number; measuring: boolean }>("match.kicks", { id, limit: 12, ...(loops !== undefined ? { loops } : {}) });

/** How a whole track unfolds, bar by bar (bars count from 0; a section's end is excluded). */
export interface Arrangement {
  tempo: number;
  downbeat: number;
  seconds: number;
  bars: number;
  parts: Record<string, { level: number[]; on: string }>;
  loudness: number[];
  low: number[];
  brightness: number[];
  density: number[];
  sections: { start: number; end: number; role: string }[];
  changes: { bar: number; part: string; what: string }[];
}

interface MapsState {
  songs: Song[];
  folder?: string;
  references?: string;
  hearing?: Hearing;
  loaded: boolean;
  /** Why the songs couldn't be read (an engine older than the app has no maps). */
  error?: string;
}

export const useMaps = create<MapsState>(() => ({ songs: [], loaded: false }));

export async function refreshSongs() {
  try {
    const r = await engine.call<{ songs: Song[]; folder: string; references: string }>("maps.list");
    useMaps.setState({ songs: r.songs, folder: r.folder, references: r.references, loaded: true, error: undefined });
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    useMaps.setState({ loaded: true, error: /unknown method/.test(message) ? "This engine is older than the app and has no maps: restart it (see the bar at the top)." : message });
    throw e;
  }
}

export async function refreshHearing() {
  useMaps.setState({ hearing: await engine.call<Hearing>("maps.status") });
}

export const refreshMaps = () => Promise.all([refreshSongs(), refreshHearing()]);

// Hearing moves on: the queue every time, the songs when a map or the queue changed (at most
// twice a second, as a song reports each stretch it hears).
let lastShape = "";
let songsTimer: ReturnType<typeof setTimeout> | undefined;

listen("listening", (status: { styles?: { mapsVersion?: number; queued?: number; current?: { id: string }; paused?: boolean; running?: boolean } }) => {
  const s = status.styles;
  if (!s || !useMaps.getState().loaded) return;
  void refreshHearing().catch(() => {});
  const shape = [s.mapsVersion, s.queued, s.current?.id, s.paused, s.running].join("|");
  if (shape === lastShape || songsTimer) return;
  lastShape = shape;
  songsTimer = setTimeout(() => {
    songsTimer = undefined;
    void refreshSongs().catch(() => {});
  }, 500);
});

const then = <T,>(p: Promise<T>) => p.then((r) => (void refreshMaps().catch(() => {}), r));

export const getMap = (id: string) => call<SongMap>("maps.get", { id });
export const setInUse = (ids: string[], inUse: boolean) => then(call("maps.setInUse", { ids, inUse }));
export const hear = (ids: string[]) => then(call<{ queued: number }>("maps.hear", { ids }));
export const hearNew = (style?: string) => then(call<{ queued: number }>("maps.hearNew", style ? { style } : {}));
export const pauseHearing = () => then(call("maps.pause"));
export const resumeHearing = () => then(call("maps.resume"));
export const unqueue = (ids: string[]) => then(call("maps.unqueue", { ids }));
export const clearQueue = () => then(call("maps.clearQueue"));
export const forget = (ids: string[]) => then(call("maps.forget", { ids }));
export const unskip = (ids: string[]) => then(call("maps.unskip", { ids }));
export const removeReferences = (ids: string[]) => then(call<{ moved: number }>("maps.removeReferences", { ids }));
export const exportMaps = (ids: string[], path: string) => call<{ path: string; count: number }>("maps.export", { ids, path });
export const importMaps = (path: string) => then(call<{ added: number; kept: number }>("maps.import", { path }));

/** Statuses that mean the song has a map the styles learn from. */
export const hasMap = (s: Song) => !!s.map;

/** Statuses that hearing would change: not heard yet, changed since, or failed. */
export const needsHearing = (s: Song) => s.status === "new" || s.status === "changed" || s.status === "failed";

// Which part of the agent panel shows, so other places can open the maps.
export type AgentView = "chat" | "notes" | "styles" | "maps" | "changes" | "chats";
export type MapsView = "songs" | "processing";

export const useAgentView = create<{ view: AgentView; maps: MapsView }>(() => ({ view: "chat", maps: "songs" }));

/** Opens the agent panel on the maps, at the songs or the hearing queue. */
export function openMaps(maps: MapsView) {
  setUi({ sidebarTab: "activity", sidebarOpen: true });
  useAgentView.setState({ view: "maps", maps });
}

/** "about 25 min" for a number of seconds. */
export function duration(seconds: number) {
  if (seconds < 90) return `${Math.max(1, Math.round(seconds))} s`;
  const minutes = Math.round(seconds / 60);
  if (minutes < 90) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  return `${hours} h ${minutes % 60} min`;
}
