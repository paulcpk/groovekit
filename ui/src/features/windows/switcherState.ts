// The window switcher (Cmd+Left/Right), as Cmd+Tab switches apps: hold Cmd, and a bar shows every
// open window (the app's own first among them, by when each was last in front); Right moves on as
// Tab does, Left back as Shift+Tab does, and letting go of Cmd brings the one chosen to the front,
// on its own. So one quick Cmd+Right goes back to the window used before, and the next one back again.
// Plugins' windows are over the app's whole window, the bar too, so they step aside while choosing.

import { create } from "zustand";
import { arrangePlugins, bringToFront, focusApp, openTargets } from "./arrange";
import { useWindows, type WindowTarget } from "@/state/windows";

interface Switcher {
  open: boolean;
  targets: WindowTarget[];
  index: number;
  /** The plugins' windows that stepped aside, to come back if it's called off. */
  aside: string[];
}

export const useSwitcher = create<Switcher>(() => ({ open: false, targets: [], index: 0, aside: [] }));

/** Cmd+Right (1) or Cmd+Left (-1): opens the switcher on the window used before (or the one used
    longest ago), or moves along it. */
export function stepSwitcher(direction: 1 | -1) {
  const { open, targets, index } = useSwitcher.getState();
  if (!open) {
    const all = openTargets();
    const aside = useWindows.getState().plugins.filter((p) => p.visible).map((p) => p.key);
    useSwitcher.setState({ open: true, targets: all, index: all.length < 2 ? 0 : direction > 0 ? 1 : all.length - 1, aside });
    if (aside.length) arrangePlugins();
    // Asked for from a plugin's window: the app's window takes the keyboard, to see Cmd let go of.
    void focusApp();
    return;
  }
  useSwitcher.setState({ index: (index + direction + targets.length) % targets.length });
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
