import { useEffect } from "react";
import { typing, useClickedLast } from "@/lib/clickedLast";
import { useEngine } from "@/state/engine";
import { setUi } from "@/state/ui";
import { copySelectedClips, deleteSelectedClips, duplicateClips, pasteClips } from "./clipOps";

/** The playlist's editing keys, while it's what was clicked last: ⌘A selects every clip, ⌘C
    copies the selected ones, ⌘V pastes and ⌘D duplicates them (by the placement rule), Delete
    removes them and Escape lets go of them. */
export function usePlaylistKeys(root: React.RefObject<HTMLElement | null>) {
  const active = useClickedLast(root);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!active.current || typing(e)) return;
      const mod = e.metaKey || e.ctrlKey;
      const k = e.key.toLowerCase();
      let done = false;

      if (mod && k === "a") {
        setUi({ selectedClips: useEngine.getState().doc?.playlist.clips.map((c) => c.id) ?? [] });
        done = true;
      } else if (mod && k === "c") {
        done = copySelectedClips();
      } else if (mod && k === "v") {
        done = pasteClips();
      } else if (mod && k === "d") {
        done = duplicateClips();
      } else if ((e.key === "Delete" || e.key === "Backspace") && !mod) {
        done = deleteSelectedClips();
      } else if (e.key === "Escape") {
        // Not used up: an open menu or dialog closes on it too.
        setUi({ selectedClips: [] });
      }

      if (done) {
        e.preventDefault();
        e.stopImmediatePropagation();
      }
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [active]);
}
