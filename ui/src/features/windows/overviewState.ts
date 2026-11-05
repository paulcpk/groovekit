// The overview of every open window (Cmd+Up): GrooveKit's own floating windows and the hosted
// plugins' windows, spread side by side over the app, so one click brings any of them to the front.
//
// It shows the windows in the background too. GrooveKit's windows move there themselves, live and
// scaled down. A plugin's window is the engine's (another process, drawn by the plugin), so the
// engine pictures it and takes it off the screen; the picture flies from where the window was to
// its place. Picking one brings it back alone, the rest fading as they fly back to the background;
// clicking beside them sends them all there, the app's own window in front.

import { create } from "zustand";
import { engine, useEngine } from "@/state/engine";
import { pluginTarget, useWindows } from "@/state/windows";
import { arrangePlugins, bringToFront, focusApp } from "./arrange";

export interface Rect { x: number; y: number; width: number; height: number }

export interface OverviewItem {
  /** A floating window's key, or "plugin:" and a plugin window's. */
  key: string;
  plugin: boolean;
  /** It was in the background: it fades in on its way. */
  hidden: boolean;
  /** Where it is, in the app's coordinates. */
  from: Rect;
  /** Where the overview puts it. */
  to: Rect;
  /** A plugin window's picture. */
  picture?: string;
}

export interface Overview {
  /** "opening": everything still where it was, about to move; "open"; "closing": on the way back. */
  phase: "closed" | "opening" | "open" | "closing";
  items: OverviewItem[];
  /** The window the keys point at (arrows, Enter). */
  selected?: string;
  /** While closing: the one coming to the front, if any. */
  picked?: string;
  /** The plugins' windows that were on screen, back to front, to put back with nothing picked. */
  restore: string[];
}

export const useOverview = create<Overview>(() => ({ phase: "closed", items: [], restore: [] }));

/** The transform that puts a box drawn at `from` over `to` (with its origin at the top left). */
export function flyTo(from: Rect, to: Rect) {
  return `translate(${to.x - from.x}px, ${to.y - from.y}px) scale(${to.width / from.width})`;
}

/** How long windows take to move in or out, in milliseconds. */
export const overviewMs = 260;

/** The room the overview leaves around the windows: the views bar on top. */
const margin = { top: 92, side: 40, bottom: 36 };
const gap = 28;

/** Places windows in rows, in the order they're given (top to bottom, then left to right, as they
    were), trying every number of rows and keeping the one that shows them biggest. Never bigger
    than they are. */
export function arrange(sizes: { width: number; height: number }[], area: Rect): Rect[] {
  if (!sizes.length) return [];
  let best: { scale: number; rows: number[][] } = { scale: 0, rows: [] };

  for (let count = 1; count <= sizes.length; count++) {
    // Rows of about equal width, in order.
    const total = sizes.reduce((sum, s) => sum + s.width, 0);
    const rows: number[][] = [[]];
    let width = 0;
    sizes.forEach((s, i) => {
      if (rows[rows.length - 1].length && width + s.width / 2 > (total / count) * rows.length && rows.length < count) rows.push([]);
      rows[rows.length - 1].push(i);
      width += s.width;
    });

    // The gaps between them stay as they are; only the windows shrink.
    const across = rows.map((r) => (area.width - gap * (r.length - 1)) / r.reduce((sum, i) => sum + sizes[i].width, 0));
    const tallest = rows.reduce((sum, r) => sum + Math.max(...r.map((i) => sizes[i].height)), 0);
    const scale = Math.min(1, ...across, (area.height - gap * (rows.length - 1)) / tallest);
    if (scale > best.scale + 0.001) best = { scale, rows };
  }

  const { scale, rows } = best;
  const heights = rows.map((r) => Math.max(...r.map((i) => sizes[i].height)) * scale);
  const tallest = heights.reduce((a, b) => a + b, 0) + gap * (rows.length - 1);
  const out: Rect[] = new Array(sizes.length);
  let y = area.y + (area.height - tallest) / 2;

  rows.forEach((r, ri) => {
    const width = r.reduce((sum, i) => sum + sizes[i].width * scale, 0) + gap * (r.length - 1);
    let x = area.x + (area.width - width) / 2;
    for (const i of r) {
      const w = sizes[i].width * scale, h = sizes[i].height * scale;
      out[i] = { x, y: y + (heights[ri] - h) / 2, width: w, height: h };
      x += w + gap;
    }
    y += heights[ri] + gap;
  });

  return out;
}

interface PluginWindow { key: string; visible: boolean; x: number; y: number; width: number; height: number; picture?: string }

let busy = false;

export async function openOverview() {
  if (busy || useOverview.getState().phase !== "closed") return;
  busy = true;

  try {
    // Brought forward when asked for from a plugin's window, which had the keyboard.
    void focusApp();

    const ours = useWindows.getState().windows
      .map((w) => {
        const box = document.querySelector<HTMLElement>(`[data-window-key="${CSS.escape(w.key)}"]`)?.getBoundingClientRect();
        return box ? { key: w.key, z: w.z, hidden: !!w.hidden, from: { x: box.left, y: box.top, width: box.width, height: box.height } } : undefined;
      })
      .filter((w) => w !== undefined);

    let plugins: PluginWindow[] = [];
    if (useEngine.getState().status === "connected") {
      try {
        plugins = (await engine.call<{ windows: PluginWindow[] }>("plugins.windows", { pictures: true, hide: true })).windows;
      } catch {
        plugins = [];
      }
    }

    // The engine's windows are placed on the screen; ours from the web view's top left.
    const pluginItems = plugins.map((p) => {
      const picture = p.picture ? new Image() : undefined;
      if (picture && p.picture) picture.src = p.picture;
      return { p, picture };
    });
    await Promise.all(pluginItems.map(({ picture }) => picture?.decode().catch(() => {})));

    const items: Omit<OverviewItem, "to">[] = [
      ...pluginItems.map(({ p, picture }) => {
        // The picture has the window's title bar, which its bounds leave out: as wide, taller above.
        const height = picture?.naturalWidth ? p.width * (picture.naturalHeight / picture.naturalWidth) : p.height;
        return {
          key: pluginTarget(p.key), plugin: true, hidden: !p.visible, picture: p.picture,
          from: { x: p.x - window.screenX, y: p.y + p.height - height - window.screenY, width: p.width, height },
        };
      }),
      ...ours.sort((a, b) => b.z - a.z).map((w) => ({ key: w.key, plugin: false, hidden: w.hidden, from: w.from })),
    ];

    // Top to bottom, left to right, as they were: each keeps roughly its side of the screen.
    items.sort((a, b) => Math.round((a.from.y - b.from.y) / 120) || a.from.x - b.from.x);

    const area = { x: margin.side, y: margin.top, width: innerWidth - margin.side * 2, height: innerHeight - margin.top - margin.bottom };
    const slots = arrange(items.map((i) => i.from), area);

    useOverview.setState({ phase: "opening", items: items.map((item, i) => ({ ...item, to: slots[i] })), selected: undefined, picked: undefined,
      restore: plugins.filter((p) => p.visible).map((p) => p.key).reverse() });
    useWindows.setState((s) => ({ plugins: s.plugins.map((p) => ({ ...p, visible: false })) }));
    // Two frames: the first draws them where they were, the second starts them moving.
    requestAnimationFrame(() => requestAnimationFrame(() => {
      if (useOverview.getState().phase === "opening") useOverview.setState({ phase: "open" });
    }));
  } finally {
    busy = false;
  }
}

/** Closes the overview. With a window picked, it flies back and comes to the front on its own, the
    rest fading as they fly back to the background; "main" (a click beside them) sends them all
    there, the app's own window in front; with nothing (Escape), everything goes back as it was. */
export function closeOverview(pick?: string) {
  const { phase, items, restore } = useOverview.getState();
  if (phase === "closed" || phase === "closing") return;
  const picked = items.find((i) => i.key === pick)?.key ?? (pick === "main" ? "main" : undefined);
  useOverview.setState({ phase: "closing", picked });

  // Our windows at once (they're shown while the overview closes); the plugins' once their pictures land.
  if (picked) bringToFront(picked, { plugins: false });

  setTimeout(() => {
    if (items.some((i) => i.plugin)) {
      const front = picked?.startsWith("plugin:") ? picked.slice(7) : undefined;
      const keep = picked ? [] : restore;
      arrangePlugins(front, keep);
    }
    setTimeout(() => useOverview.setState({ phase: "closed", items: [], selected: undefined, picked: undefined }), 60);
  }, overviewMs);
}

/** How opaque a window is in the overview: those in the background fade in on the way there, and
    fade on the way back unless they're coming to the front. */
export function overviewOpacity(phase: Overview["phase"], item: OverviewItem, picked: string | undefined, restoring: boolean) {
  if (phase === "opening") return item.hidden ? 0 : 1;
  if (phase === "closing") return item.key === picked || (restoring && !item.hidden) ? 1 : 0;
  return 1;
}

export function toggleOverview() {
  if (useOverview.getState().phase === "closed") void openOverview();
  else closeOverview();
}

/** Steps the keys' choice to the next window (1) or the one before (-1), in the order they sit in
    (left to right, row by row), round from the last to the first. */
export function stepSelection(direction: 1 | -1) {
  const { items, selected } = useOverview.getState();
  if (!items.length) return;
  // A row's windows share their middle (each is centred in its row's height).
  const middle = (r: Rect) => r.y + r.height / 2;
  const reading = [...items].sort((a, b) => Math.round((middle(a.to) - middle(b.to)) / 8) || a.to.x - b.to.x);
  const at = reading.findIndex((i) => i.key === selected);
  const next = at < 0 ? (direction > 0 ? 0 : reading.length - 1) : (at + direction + reading.length) % reading.length;
  useOverview.setState({ selected: reading[next].key });
}

/** Moves the keys' choice to the next window in a direction, by where the windows sit. */
export function moveSelection(dx: number, dy: number) {
  const { items, selected } = useOverview.getState();
  if (!items.length) return;
  const current = items.find((i) => i.key === selected);
  if (!current) return useOverview.setState({ selected: items[0].key });

  const centre = (r: Rect) => ({ x: r.x + r.width / 2, y: r.y + r.height / 2 });
  const from = centre(current.to);
  let best: { key: string; cost: number } | undefined;

  for (const item of items) {
    if (item === current) continue;
    const c = centre(item.to);
    const along = (c.x - from.x) * dx + (c.y - from.y) * dy;
    if (along <= 1) continue;
    const across = Math.abs((c.x - from.x) * dy) + Math.abs((c.y - from.y) * dx);
    const cost = along + across * 2;
    if (!best || cost < best.cost) best = { key: item.key, cost };
  }

  if (best) useOverview.setState({ selected: best.key });
}
