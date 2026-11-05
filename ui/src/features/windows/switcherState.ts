// The window switcher (Cmd+Left/Right), as Cmd+Tab switches apps: hold Cmd, and a bar shows every
// open window (the app's own first among them, by when each was last in front); Right moves on as
// Tab does, Left back as Shift+Tab does, and letting go of Cmd brings the one chosen to the front,
// on its own. So one quick Cmd+Right goes back to the window used before, and the next one back again.
//
// Each window shows as a small picture of itself: GrooveKit's own windows are moved into the bar,
// live and scaled down (as in the overview); a plugin's window is the engine's, so it shows the
// picture the engine takes. Plugins' windows are over the app's whole window, the bar too, so
// they step aside while choosing.

import { create } from "zustand";
import { engine, useEngine } from "@/state/engine";
import { pluginTarget, useWindows, type WindowTarget } from "@/state/windows";
import { arrangePlugins, bringToFront, focusApp, openTargets } from "./arrange";
import type { Rect } from "./overviewState";

interface Switcher {
  open: boolean;
  targets: WindowTarget[];
  index: number;
  /** The plugins' windows that stepped aside, to come back if it's called off. */
  aside: string[];
  /** Each window's tile in the bar, and where its picture goes in it. */
  tiles: Record<WindowTarget, Rect>;
  /** Our floating windows: where each is, and where it's shown in its tile. */
  fits: Record<WindowTarget, { from: Rect; to: Rect }>;
  /** The plugins' windows' pictures, as the engine took them. */
  pictures: Record<WindowTarget, string>;
}

export const useSwitcher = create<Switcher>(() => ({ open: false, targets: [], index: 0, aside: [], tiles: {}, fits: {}, pictures: {} }));

/** The bar's measures: tiles as wide as fit the screen (at most 176 pixels), a picture in each
    with the name under it. */
export const switcherLayout = { gap: 10, pad: 14, label: 22, inset: 6, maxTile: 176, minTile: 88, ratio: 0.68 };

function layoutTiles(targets: WindowTarget[]) {
  const { gap, pad, label, inset, maxTile, minTile, ratio } = switcherLayout;
  const n = targets.length;
  const width = Math.max(minTile, Math.min(maxTile, (innerWidth - 64 - pad * 2 - gap * (n - 1)) / n));
  const height = width * ratio + label;
  const total = width * n + gap * (n - 1);
  const left = Math.max(32 + pad, (innerWidth - total) / 2);
  const top = (innerHeight - height) / 2;
  const tiles: Record<WindowTarget, Rect> = {};
  targets.forEach((t, i) => (tiles[t] = { x: left + i * (width + gap), y: top, width, height }));

  // Our windows' pictures: each fitted into its tile above the name, keeping its shape.
  const fits: Switcher["fits"] = {};
  for (const t of targets) {
    const box = document.querySelector<HTMLElement>(`[data-window-key="${CSS.escape(t)}"]`)?.getBoundingClientRect();
    if (!box?.width || !box.height) continue;
    const tile = tiles[t];
    const room = { width: tile.width - inset * 2, height: tile.height - label - inset * 2 };
    const scale = Math.min(room.width / box.width, room.height / box.height);
    const w = box.width * scale, h = box.height * scale;
    fits[t] = {
      from: { x: box.left, y: box.top, width: box.width, height: box.height },
      to: { x: tile.x + inset + (room.width - w) / 2, y: tile.y + inset + (room.height - h) / 2, width: w, height: h },
    };
  }
  return { tiles, fits };
}

interface PluginWindow { key: string; picture?: string }

/** Cmd+Right (1) or Cmd+Left (-1): opens the switcher on the window used before (or the one used
    longest ago), or moves along it. */
export function stepSwitcher(direction: 1 | -1) {
  const { open, targets, index } = useSwitcher.getState();
  if (open) {
    useSwitcher.setState({ index: (index + direction + targets.length) % targets.length });
    return;
  }

  const all = openTargets();
  const aside = useWindows.getState().plugins.filter((p) => p.visible).map((p) => p.key);
  useSwitcher.setState({
    open: true, targets: all, index: all.length < 2 ? 0 : direction > 0 ? 1 : all.length - 1, aside, ...layoutTiles(all), pictures: {},
  });

  // The plugins' pictures (taken as they step aside), as soon as the engine has them.
  if (all.some((t) => t.startsWith("plugin:")) && useEngine.getState().status === "connected") {
    useWindows.setState((s) => ({ plugins: s.plugins.map((p) => ({ ...p, visible: false })) }));
    void engine.call<{ windows: PluginWindow[] }>("plugins.windows", { pictures: true, hide: true }).then(({ windows }) => {
      if (!useSwitcher.getState().open) return;
      const pictures: Record<WindowTarget, string> = {};
      for (const w of windows) if (w.picture) pictures[pluginTarget(w.key)] = w.picture;
      useSwitcher.setState({ pictures });
    }, () => {});
  }

  // Asked for from a plugin's window: the app's window takes the keyboard, to see Cmd let go of.
  void focusApp();
}

/** Brings the window chosen (or one clicked) to the front. */
export function commitSwitcher(choice?: WindowTarget) {
  const { open, targets, index } = useSwitcher.getState();
  if (!open) return;
  useSwitcher.setState({ open: false });
  const target = choice ?? targets[index];
  if (target) bringToFront(target);
}

export function cancelSwitcher() {
  const { open, aside } = useSwitcher.getState();
  if (!open) return;
  useSwitcher.setState({ open: false });
  if (aside.length) arrangePlugins(undefined, aside);
}
