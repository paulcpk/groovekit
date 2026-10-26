import { useEffect } from "react";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { redo, setPosition, togglePlay, toggleLoop, undo } from "./actions";

function typing(e: KeyboardEvent) {
  const t = e.target as HTMLElement | null;
  return !!t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
}

/** Global keys, FL Studio style where FL has a habit: F5-F9 for views, Space to play. */
export function useShortcuts() {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (typing(e)) return;
      const mod = e.metaKey || e.ctrlKey;

      if (e.key === " ") {
        e.preventDefault();
        void togglePlay();
      } else if (mod && e.key.toLowerCase() === "z") {
        e.preventDefault();
        void (e.shiftKey ? redo() : undo());
      } else if (mod && e.key.toLowerCase() === "y") {
        e.preventDefault();
        void redo();
      } else if (e.key === "F5") {
        e.preventDefault();
        setUi({ bottomOpen: false });
      } else if (e.key === "F6" || e.key === "F7" || e.key === "F9") {
        e.preventDefault();
        setUi({ bottomOpen: true, bottomPanel: e.key === "F6" ? "rack" : e.key === "F7" ? "piano" : "mixer" });
      } else if (e.key === "F8" || (mod && e.key.toLowerCase() === "b")) {
        e.preventDefault();
        setUi({ sidebarOpen: !useUi.getState().sidebarOpen });
      } else if (e.key === "Home" || e.key === "Enter") {
        e.preventDefault();
        void setPosition(0);
      } else if (e.key.toLowerCase() === "l" && !mod) {
        void toggleLoop();
      } else if ((e.key === "Delete" || e.key === "Backspace") && !mod) {
        const ids = useUi.getState().selectedClips;
        const doc = useEngine.getState().doc;
        const existing = ids.filter((id) => doc?.playlist.clips.some((c) => c.id === id));
        if (existing.length) {
          e.preventDefault();
          void execute(existing.map((clip) => ({ type: "removeClip" as const, clip })), { label: existing.length === 1 ? "Delete clip" : `Delete ${existing.length} clips` });
          setUi({ selectedClips: [] });
        }
      }
    };

    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);
}
