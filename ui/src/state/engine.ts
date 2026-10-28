// The engine connection and the mirror of the song: everything the engine knows, kept in step
// through snapshot and patch notifications. UI-only state lives in ./ui.ts.

import type { Project } from "@groovekit/schema/project";
import type { Command, PatchOp } from "@groovekit/schema/protocol";
import { create } from "zustand";
import { EngineClient, type Status } from "@/engine/client";
import { applyPatch, parsePointer } from "@/engine/patch";
import { setUi } from "./ui";

export interface Transport {
  playing: boolean;
  recording: boolean;
  /** Ticks; within the pattern in pattern mode. */
  position: number;
  seconds: number;
  /** Pattern mode plays one pattern on a loop instead of the song. */
  mode?: "song" | "pattern";
  pattern?: string;
  metronome?: boolean;
  countInBars?: number;
  /** When the engine read the position (ms since 1970), and how long until it's heard. */
  time?: number;
  outputLatencyMs?: number;
}

export interface Levels {
  inserts: Record<string, [number, number]>;
  channels: Record<string, number>;
}

export interface ChangeEvent {
  entry: number;
  kind: "execute" | "undo" | "redo";
  origin: string;
  label: string;
  time: number;
  /** Ids of the channels, patterns, rows, clips and inserts it touched. */
  touched: string[];
}

export interface LibraryProgress {
  running: boolean;
  filesFound: number;
  analysed: number;
  unchanged: number;
  failed: number;
  samples: number;
}

interface EngineState {
  status: Status;
  error?: string;
  project?: string;
  doc?: Project;
  head: number | null;
  /** The history entry the song was last saved at; changes since are unsaved. */
  saved?: number | null;
  transport: Transport;
  levels: Levels;
  changes: ChangeEvent[];
  /** When each id was last touched by someone other than this UI (the agent), for highlighting. */
  touchedAt: Record<string, number>;
  libraryProgress?: LibraryProgress;
  /** The sample being auditioned from the browser. */
  previewing?: string;
}

export const useEngine = create<EngineState>(() => ({
  status: "connecting",
  head: null,
  transport: { playing: false, recording: false, position: 0, seconds: 0 },
  levels: { inserts: {}, channels: {} },
  changes: [],
  touchedAt: {},
}));

const set = useEngine.setState;
const get = useEngine.getState;

/** The ids a change touched: the element each op's path leads into, looked up in the newer
    document. Arrangement rows have numbers, not ids: they show up as "row:<number>". */
function touchedIds(before: Project | undefined, after: Project, ops: PatchOp[]): string[] {
  const ids = new Set<string>();
  const collections: Record<string, (doc: Project) => { id: string }[] | undefined> = {
    channels: (d) => d.channels,
    patterns: (d) => d.patterns,
    "playlist/clips": (d) => d.playlist.clips,
    "mixer/inserts": (d) => d.mixer.inserts,
  };

  for (const op of ops) {
    const keys = parsePointer(op.path);
    const name = keys[0] === "playlist" || keys[0] === "mixer" ? `${keys[0]}/${keys[1]}` : keys[0];
    const index = Number(keys[name.includes("/") ? 2 : 1]);
    if (name === "playlist/rows" && !Number.isNaN(index)) ids.add(`row:${index}`);
    const list = collections[name];
    if (!list || Number.isNaN(index)) continue;

    const item = op.op === "remove" && keys.length === (name.includes("/") ? 3 : 2) ? list(before ?? after)?.[index] : list(after)?.[index];
    if (item?.id) ids.add(item.id);
  }

  return [...ids];
}

const listeners = new Map<string, Set<(params: any) => void>>();

/** Calls back for every notification of a kind until the returned function is called. */
export function listen(method: string, callback: (params: any) => void) {
  const set = listeners.get(method) ?? new Set();
  listeners.set(method, set);
  set.add(callback);
  return () => void set.delete(callback);
}

function onNotification(method: string, params: any) {
  listeners.get(method)?.forEach((callback) => callback(params));

  switch (method) {
    case "snapshot": {
      // Another project was opened (by this UI or a client sharing the engine).
      const previous = get().project;
      const switched = params.project !== undefined && previous !== undefined && params.project !== previous;

      set({ doc: params.document, head: params.head ?? null, saved: params.saved !== undefined ? params.saved : params.head ?? null,
        ...(params.project ? { project: params.project } : {}) });

      if (switched) {
        set({ changes: [], touchedAt: {}, levels: { inserts: {}, channels: {} }, transport: { playing: false, recording: false, position: 0, seconds: 0, mode: "song" }, previewing: undefined });
        setUi({ selectedPattern: params.document.patterns?.[0]?.id, selectedChannel: undefined, selectedInsert: undefined, selectedClips: [], loop: undefined, looping: false });
      }
      break;
    }

    case "createAutomation": {
      // "Automate" in the bar above a plugin's window.
      const doc = get().doc;
      if (doc) void import("@/features/playlist/automation").then((m) => m.createAutomation(doc, params.target, params.value));
      break;
    }

    case "hello":
      set({ project: params.project });
      break;

    case "patch": {
      const before = get().doc;
      if (!before) return;

      const doc = applyPatch(before, params.ops);
      const touched = touchedIds(before, doc, params.ops);
      const change: ChangeEvent = { entry: params.entry, kind: params.kind, origin: params.origin ?? "?", label: params.label ?? "", time: Date.now(), touched };
      const now = Date.now();
      const touchedAt = change.origin === "ui" ? get().touchedAt : { ...get().touchedAt, ...Object.fromEntries(touched.map((id) => [id, now])) };

      set({ doc, head: params.head ?? null, changes: [change, ...get().changes].slice(0, 200), touchedAt });
      break;
    }

    case "saved":
      set({ saved: params.saved ?? null, head: params.head ?? null });
      break;

    case "transport":
      set({ transport: params });
      break;

    case "levels": {
      // Levels are measured as audio is rendered; show them when it's heard.
      const delay = get().transport.outputLatencyMs ?? 0;
      if (delay > 5) setTimeout(() => set({ levels: params }), delay);
      else set({ levels: params });
      break;
    }

    case "libraryProgress":
      set({ libraryProgress: params });
      break;
  }
}

export const engine = new EngineClient("ui", {
  status: (status, error) => {
    set({ status, error });
    // Transport notifications only come with changes; on connecting, ask where it is (mode, metronome...).
    if (status === "connected") void engine.call<Transport>("transport.state").then((transport) => set({ transport }), () => {});
  },
  notification: onNotification,
});

/** Calls an engine method; failures are shown as a toast as well as rejected. */
export async function call<T = any>(method: string, params: Record<string, unknown> = {}): Promise<T> {
  try {
    return await engine.call<T>(method, params);
  } catch (e) {
    reportError(e);
    throw e;
  }
}

export interface ExecuteOptions {
  label?: string;
  /** Changes sharing a group undo together, e.g. every step of one drag. */
  group?: string;
}

/** Applies commands as one undoable change. */
export function execute(commands: Command | Command[], options: ExecuteOptions = {}) {
  const list = Array.isArray(commands) ? commands : [commands];
  if (list.length === 0) return Promise.resolve();
  return call("execute", { commands: list, ...options });
}

//==============================================================================
export interface Toast {
  id: number;
  message: string;
  tone: "error" | "info";
}

export const useToasts = create<{ toasts: Toast[] }>(() => ({ toasts: [] }));
let nextToast = 1;

/** Whether the song changed since it was last saved. */
export function hasUnsavedChanges() {
  const { status, saved, head } = get();
  return status === "connected" && saved !== undefined && saved !== head;
}

export function toast(message: string, tone: Toast["tone"] = "info") {
  const id = nextToast++;
  useToasts.setState((s) => ({ toasts: [...s.toasts, { id, message, tone }].slice(-4) }));
  setTimeout(() => useToasts.setState((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) })), tone === "error" ? 6000 : 3000);
}

export function reportError(e: unknown) {
  toast(e instanceof Error ? e.message : String(e), "error");
}
