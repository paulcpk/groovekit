// Floating windows over the app: a channel's sampler, an effect's editor, a style guide. Several can be open;
// each remembers where it was put.

import { create } from "zustand";

export type WindowSpec =
  | { kind: "channel"; channel: string }
  | { kind: "effect"; insert: string; effect: string }
  /** A style guide by id; "" for a new one. */
  | { kind: "guide"; guide: string };

export interface OpenWindow {
  key: string;
  spec: WindowSpec;
  x: number;
  y: number;
  z: number;
}

const POSITIONS = "groovekit.windowPositions";

function loadPositions(): Record<string, { x: number; y: number }> {
  try {
    return JSON.parse(localStorage.getItem(POSITIONS) ?? "{}");
  } catch {
    return {};
  }
}

const positions = loadPositions();

export const useWindows = create<{ windows: OpenWindow[]; top: number }>(() => ({ windows: [], top: 1 }));

export function windowKey(spec: WindowSpec) {
  if (spec.kind === "guide") return `guide:${spec.guide}`;
  return spec.kind === "channel" ? `channel:${spec.channel}` : `effect:${spec.insert}/${spec.effect}`;
}

export function openWindow(spec: WindowSpec) {
  const key = windowKey(spec);
  useWindows.setState((s) => {
    const top = s.top + 1;
    if (s.windows.some((w) => w.key === key)) return { top, windows: s.windows.map((w) => (w.key === key ? { ...w, z: top } : w)) };
    const n = s.windows.length;
    const at = positions[key] ?? { x: 260 + n * 28, y: 110 + n * 28 };
    return { top, windows: [...s.windows, { key, spec, ...at, z: top }] };
  });
}

export function closeWindow(key: string) {
  useWindows.setState((s) => ({ windows: s.windows.filter((w) => w.key !== key) }));
}

/** Opens the window, or closes it when it's already open. */
export function toggleWindow(spec: WindowSpec) {
  const key = windowKey(spec);
  if (useWindows.getState().windows.some((w) => w.key === key)) closeWindow(key);
  else openWindow(spec);
}

export function focusWindow(key: string) {
  useWindows.setState((s) => {
    if (s.windows.find((w) => w.key === key)?.z === s.top) return s;
    const top = s.top + 1;
    return { top, windows: s.windows.map((w) => (w.key === key ? { ...w, z: top } : w)) };
  });
}

export function moveWindow(key: string, x: number, y: number) {
  useWindows.setState((s) => ({ windows: s.windows.map((w) => (w.key === key ? { ...w, x, y } : w)) }));
  positions[key] = { x, y };
  try {
    localStorage.setItem(POSITIONS, JSON.stringify(positions));
  } catch {
    // Positions last until the app closes.
  }
}
