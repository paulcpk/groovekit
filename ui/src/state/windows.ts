// Floating windows over the app: a channel's sampler, an effect's editor, a style guide, a song map, mastering. Several can be open;
// each remembers where it was put. An open window is in front or in the background (hidden, still
// open: clicking the app's own window sends them all there, the overview and the switcher bring
// one back). Hosted plugins' windows are the engine's; what's known of them is kept here too.

import { create } from "zustand";

export type WindowSpec =
  | { kind: "channel"; channel: string }
  | { kind: "effect"; insert: string; effect: string }
  /** A style guide by id; "" for a new one. */
  | { kind: "guide"; guide: string }
  /** A reference song's map, by its id (style/title). */
  | { kind: "map"; id: string }
  /** A choice the agent put to the user, by its offer id. */
  | { kind: "pick"; id: number }
  /** Mastering the song like a reference. */
  | { kind: "master" };

export interface OpenWindow {
  key: string;
  spec: WindowSpec;
  x: number;
  y: number;
  z: number;
  /** Set by dragging its top or bottom edge; else as tall as it needs, up to the screen. */
  height?: number;
  /** In the background: still open, not shown. */
  hidden?: boolean;
}

/** A hosted plugin's open window (the engine's), by its key ("channel:<id>", "effect:<insert>/<effect>"). */
export interface PluginWindow { key: string; visible: boolean }

/** Who a window is for the switcher and the overview: "main" (the app's own window), a floating
    window's key, or "plugin:" and a plugin window's key. */
export type WindowTarget = string;

export const pluginTarget = (key: string) => `plugin:${key}`;

const POSITIONS = "groovekit.windowPositions";

function loadPositions(): Record<string, { x: number; y: number; height?: number }> {
  try {
    return JSON.parse(localStorage.getItem(POSITIONS) ?? "{}");
  } catch {
    return {};
  }
}

const positions = loadPositions();

export const useWindows = create<{
  windows: OpenWindow[];
  top: number;
  plugins: PluginWindow[];
  /** The windows by when they were last in front, latest first (the switcher's order). */
  recent: WindowTarget[];
  /** What each floating window is called, as its title says, for the switcher. */
  names: Record<string, string>;
}>(() => ({ windows: [], top: 1, plugins: [], recent: ["main"], names: {} }));

/** Marks a window as the one used last. */
export function touchWindow(target: WindowTarget) {
  useWindows.setState((s) => (s.recent[0] === target ? s : { recent: [target, ...s.recent.filter((t) => t !== target)] }));
}

export function windowKey(spec: WindowSpec) {
  if (spec.kind === "guide") return `guide:${spec.guide}`;
  if (spec.kind === "map") return `map:${spec.id}`;
  if (spec.kind === "pick") return `pick:${spec.id}`;
  if (spec.kind === "master") return "master";
  return spec.kind === "channel" ? `channel:${spec.channel}` : `effect:${spec.insert}/${spec.effect}`;
}

export function openWindow(spec: WindowSpec) {
  const key = windowKey(spec);
  useWindows.setState((s) => {
    const top = s.top + 1;
    if (s.windows.some((w) => w.key === key)) return { top, windows: s.windows.map((w) => (w.key === key ? { ...w, z: top, hidden: false } : w)) };
    const n = s.windows.length;
    const at = positions[key] ?? { x: 260 + n * 28, y: 110 + n * 28 };
    return { top, windows: [...s.windows, { key, spec, ...at, z: top }] };
  });
  touchWindow(key);
}

/** Sends a floating window to the background. */
export function hideWindow(key: string) {
  useWindows.setState((s) => ({ windows: s.windows.map((w) => (w.key === key ? { ...w, hidden: true } : w)) }));
}

export function closeWindow(key: string) {
  useWindows.setState((s) => ({ windows: s.windows.filter((w) => w.key !== key), recent: s.recent.filter((t) => t !== key) }));
}

/** Opens the window, or closes it when it's already open. */
export function toggleWindow(spec: WindowSpec) {
  const key = windowKey(spec);
  if (useWindows.getState().windows.some((w) => w.key === key && !w.hidden)) closeWindow(key);
  else openWindow(spec);
}

export function focusWindow(key: string) {
  useWindows.setState((s) => {
    const window = s.windows.find((w) => w.key === key);
    if (window?.z === s.top && !window.hidden) return s;
    const top = s.top + 1;
    return { top, windows: s.windows.map((w) => (w.key === key ? { ...w, z: top, hidden: false } : w)) };
  });
  touchWindow(key);
}

function savePositions() {
  try {
    localStorage.setItem(POSITIONS, JSON.stringify(positions));
  } catch {
    // Positions last until the app closes.
  }
}

export function moveWindow(key: string, x: number, y: number) {
  useWindows.setState((s) => ({ windows: s.windows.map((w) => (w.key === key ? { ...w, x, y } : w)) }));
  positions[key] = { ...positions[key], x, y };
  savePositions();
}

/** A window's height and top, from dragging one of its edges. */
export function resizeWindow(key: string, y: number, height: number) {
  useWindows.setState((s) => ({ windows: s.windows.map((w) => (w.key === key ? { ...w, y, height } : w)) }));
  const x = useWindows.getState().windows.find((w) => w.key === key)?.x ?? 0;
  positions[key] = { ...positions[key], x: positions[key]?.x ?? x, y, height };
  savePositions();
}
