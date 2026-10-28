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
  followPlayhead: boolean;
  settingsOpen: boolean;
  recordOpen: boolean;
  projectDialog?: "new" | "open" | "saveAs";
  exportOpen: boolean;
  /** The computer keyboard plays the selected channel (Z row from C of this octave, Q row an octave up). */
  typingKeyboard: boolean;
  keyboardOctave: number;
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
  followPlayhead: true,
  settingsOpen: false,
  recordOpen: false,
  exportOpen: false,
  typingKeyboard: false,
  keyboardOctave: 4,
  recordQuantize: 0,
  looping: false,
  loopDrop: "stretch",
  ...saved,
}));

const persisted: (keyof UiState)[] = [
  "sidebarTab", "sidebarOpen", "bottomPanel", "bottomOpen", "bottomHeight", "sidebarWidth",
  "rackView", "playlistZoom", "rowHeight", "snap", "pianoZoom", "pianoKeyHeight", "followPlayhead", "typingKeyboard", "keyboardOctave", "recordQuantize", "loopDrop",
];

useUi.subscribe((state) => {
  try {
    localStorage.setItem("groovekit.ui", JSON.stringify(Object.fromEntries(persisted.map((k) => [k, state[k]]))));
  } catch {
    // Storage can be unavailable; the UI works without it.
  }
});

export const setUi = useUi.setState;
