// UI-only state: views, selection, zoom. The engine never sees it.

import { create } from "zustand";
import type { FitMode } from "@/lib/fit";

export type BottomPanel = "rack" | "piano" | "mixer";
export type SidebarTab = "browser" | "history" | "activity";
/** Which channels the rack lists: those played by patterns, the samples dropped on the playlist, or all. */
export type RackView = "rack" | "arrangement" | "all";

export interface UiState {
  sidebarTab: SidebarTab;
  sidebarOpen: boolean;
  bottomPanel: BottomPanel;
  bottomOpen: boolean;
  bottomHeight: number;
  sidebarWidth: number;

  selectedPattern?: string;
  selectedChannel?: string;
  selectedInsert?: string;
  selectedClips: string[];
  rackView: RackView;

  /** Playlist pixels per quarter note. */
  playlistZoom: number;
  rowHeight: number;
  /** Snap in ticks; 0 snaps to bars, 1 is effectively off. */
  snap: number;
  pianoZoom: number;
  pianoKeyHeight: number;
  /** The piano roll's grid in ticks (its own, apart from the playlist's snap); 1 is off. */
  pianoSnap: number;
  followPlayhead: boolean;
  settingsOpen: boolean;
  recordOpen: boolean;
  projectDialog?: "new" | "open" | "saveAs";
  exportOpen: boolean;
  /** The computer keyboard plays the selected channel (Z row from C of this octave, Q row an octave up; 3 by default, so Z is C3). */
  typingKeyboard: boolean;
  typingOctave: number;
  /** Recorded notes snap to this grid (ticks); 0 keeps them as played. */
  recordQuantize: number;
  /** How a loop dropped onto the playlist fits the song (one-shots play as recorded). */
  loopDrop: FitMode;
  /** The loop region in ticks, and whether the transport loops it. */
  loop?: { start: number; end: number };
  looping: boolean;
}

const saved = (() => {
  try {
    return JSON.parse(localStorage.getItem("groovekit.ui") ?? "{}") as Partial<UiState>;
  } catch {
    return {};
  }
})();

export const useUi = create<UiState>(() => ({
  sidebarTab: "browser",
  sidebarOpen: true,
  bottomPanel: "rack",
  bottomOpen: true,
  bottomHeight: 300,
  sidebarWidth: 260,
  selectedClips: [],
  rackView: "rack",
  playlistZoom: 24,
  rowHeight: 44,
  snap: 960,
  pianoZoom: 80,
  pianoKeyHeight: 12,
  pianoSnap: 240,
  followPlayhead: true,
  settingsOpen: false,
  recordOpen: false,
  exportOpen: false,
  typingKeyboard: false,
  typingOctave: 3,
  recordQuantize: 0,
  looping: false,
  loopDrop: "stretch",
  ...saved,
}));

const persisted: (keyof UiState)[] = [
  "sidebarTab", "sidebarOpen", "bottomPanel", "bottomOpen", "bottomHeight", "sidebarWidth",
  "rackView", "playlistZoom", "rowHeight", "snap", "pianoZoom", "pianoKeyHeight", "pianoSnap", "followPlayhead", "typingKeyboard", "typingOctave", "recordQuantize", "loopDrop",
];

function save(state: UiState) {
  try {
    localStorage.setItem("groovekit.ui", JSON.stringify(Object.fromEntries(persisted.map((k) => [k, state[k]]))));
  } catch {
    // Storage can be unavailable; the UI works without it.
  }
}

// Saved a moment after the last change to a persisted setting (a drag changes one many times
// a second), and on leaving; selection and other passing state never touch storage.
let saveTimer: ReturnType<typeof setTimeout> | undefined;

useUi.subscribe((state, previous) => {
  if (persisted.every((k) => state[k] === previous[k])) return;
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => save(state), 250);
});

if (typeof window !== "undefined") {
  window.addEventListener("pagehide", () => {
    if (saveTimer === undefined) return;
    clearTimeout(saveTimer);
    saveTimer = undefined;
    save(useUi.getState());
  });
}

export const setUi = useUi.setState;
