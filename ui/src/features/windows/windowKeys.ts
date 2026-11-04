// The keys for the windows, heard before anything else (registered first, at the capture), so an
// arrow doesn't also move notes in the piano roll:
// - Cmd+Up (or F3, the Mac's own key for it): the overview; while it's up, the arrows choose a
//   window, Enter brings it forward, Escape (or Cmd+Up again) leaves everything as it was;
// - Cmd+Right / Cmd+Left: the switcher, Cmd let go of picks; Escape calls it off. In a text field
//   they keep moving to the line's end or start, as on the Mac.

import { typing } from "@/lib/clickedLast";
import { closeOverview, moveSelection, toggleOverview, useOverview } from "./overviewState";
import { cancelSwitcher, commitSwitcher, stepSwitcher, useSwitcher } from "./switcherState";

const arrows: Record<string, [number, number]> = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };

export function windowKeyDown(e: KeyboardEvent) {
  const plainCmd = e.metaKey && !e.shiftKey && !e.altKey && !e.ctrlKey;
  const switching = useSwitcher.getState().open;
  const overview = useOverview.getState().phase !== "closed";

  if ((plainCmd && e.key === "ArrowUp") || e.key === "F3") {
    if (switching) cancelSwitcher();
    if (!e.repeat) toggleOverview();
  } else if (plainCmd && (e.key === "ArrowLeft" || e.key === "ArrowRight") && (switching || (!overview && !typing(e)))) {
    stepSwitcher(e.key === "ArrowRight" ? 1 : -1);
  } else if (switching) {
    if (e.key === "Escape") cancelSwitcher();
    else if (e.key === "Enter") commitSwitcher();
    else return;
  } else if (overview) {
    if (e.key === "Escape") closeOverview();
    else if (e.key === "Enter") closeOverview(useOverview.getState().selected);
    else if (arrows[e.key]) moveSelection(...arrows[e.key]);
    else return;
  } else {
    return;
  }

  e.preventDefault();
  e.stopImmediatePropagation();
}

/** Letting go of Cmd picks the window chosen in the switcher. */
export function windowKeyUp(e: KeyboardEvent) {
  if (e.key === "Meta" && useSwitcher.getState().open) commitSwitcher();
}

/** If Cmd was let go of where the app couldn't see it (the window had just taken the keyboard),
    the next move of the mouse tells. */
export function windowPointerMove(e: PointerEvent) {
  if (!e.metaKey && useSwitcher.getState().open) commitSwitcher();
}
