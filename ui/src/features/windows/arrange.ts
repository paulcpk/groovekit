// What's in front: the app's own window (the arrangement, with the rack, mixer and piano roll)
// and the windows over it. Clicking the app's window sends every window to the background; the
// overview (Cmd+Up) and the switcher (Cmd+Left/Right) bring one back, on its own.

import { inDesktopApp } from "@/engine/client";
import { engine, listen, useEngine } from "@/state/engine";
import { pluginTarget, touchWindow, useWindows, type PluginWindow, type WindowTarget } from "@/state/windows";

/** Brings one window to the front, and sends every other one to the background; "main" sends
    them all there. `plugins: false` leaves the plugins' windows to the caller (the overview puts
    them back once its pictures have landed). */
export function bringToFront(target: WindowTarget, { plugins = true } = {}) {
  const { windows, plugins: open } = useWindows.getState();
  const top = useWindows.getState().top + 1;

  if (windows.some((w) => !w.hidden !== (w.key === target)))
    useWindows.setState({ top, windows: windows.map((w) => ({ ...w, hidden: w.key !== target, z: w.key === target ? top : w.z })) });

  const plugin = target.startsWith("plugin:") ? target.slice(7) : undefined;
  if (plugins && (plugin || open.some((p) => p.visible))) arrangePlugins(plugin);
  touchWindow(target);
}

/** The plugins' windows: one in front (with the keyboard), those in `keep` behind it, the rest in
    the background. */
export function arrangePlugins(front?: string, keep: string[] = []) {
  // Known at once, so a second click doesn't ask again; the engine confirms.
  useWindows.setState((s) => ({ plugins: s.plugins.map((p) => ({ ...p, visible: p.key === front || keep.includes(p.key) })) }));
  void engine.call("plugins.arrange", { ...(front ? { front } : {}), ...(keep.length ? { keep } : {}) }).catch(() => {});
}

/** Whether anything is in front of the app's window. */
export function anythingInFront() {
  const { windows, plugins } = useWindows.getState();
  return windows.some((w) => !w.hidden) || plugins.some((p) => p.visible);
}

/** Every open window, for the switcher: the app's own, the floating ones and the plugins', by when
    each was last in front. */
export function openTargets(): WindowTarget[] {
  const { windows, plugins, recent } = useWindows.getState();
  const all = ["main", ...[...windows].sort((a, b) => b.z - a.z).map((w) => w.key), ...plugins.map((p) => pluginTarget(p.key))];
  return [...recent.filter((t) => all.includes(t)), ...all.filter((t) => !recent.includes(t))];
}

/** Brings the app's own window forward, from a plugin's window that had the keyboard. */
export async function focusApp() {
  if (!inDesktopApp || document.hasFocus()) return;
  const { getCurrentWindow } = await import("@tauri-apps/api/window");
  await getCurrentWindow().setFocus().catch(() => {});
}

/** Keeps the plugins' windows known: the engine says when they open, close, go or come back. */
export function followPluginWindows() {
  const update = (windows: PluginWindow[]) => {
    const before = useWindows.getState().plugins;
    useWindows.setState({ plugins: windows.map(({ key, visible }) => ({ key, visible })) });
    // One that came to the front (opened, say) is the one used last.
    for (const w of windows) if (w.visible && !before.some((b) => b.key === w.key && b.visible)) touchWindow(pluginTarget(w.key));
  };

  const stopList = listen("pluginWindows", (params: { windows: PluginWindow[] }) => update(params.windows));
  const stopActive = listen("pluginWindowActive", ({ key }: { key: string }) => touchWindow(pluginTarget(key)));
  const stopStatus = useEngine.subscribe((s, previous) => {
    if (s.status === "connected" && previous.status !== "connected")
      void engine.call<{ windows: PluginWindow[] }>("plugins.windows").then((r) => update(r.windows), () => update([]));
  });

  return () => {
    stopList();
    stopActive();
    stopStatus();
  };
}
