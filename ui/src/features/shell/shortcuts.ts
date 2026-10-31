import { useEffect } from "react";
import { typing } from "@/lib/clickedLast";
import { setUi, useUi } from "@/state/ui";
import { redo, setPosition, stop, togglePlay, toggleLoop, undo } from "./actions";
import { browseForProject, saveProject } from "./ProjectDialog";
import { splitAtPlayhead } from "@/features/playlist/clipOps";
import { useVoiceRequest } from "@/features/agent/voice";

let lastSpace = 0;

/** Global keys: F5-F9 for the views, Space to play, undo, save and the like. The playlist and
    the piano roll have editing keys of their own. */
export function useShortcuts() {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      // Talk to the agent, from anywhere (also while typing to it).
      if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === "m") {
        e.preventDefault();
        setUi({ sidebarOpen: true, sidebarTab: "activity" });
        useVoiceRequest.setState({ pending: true });
        return;
      }

      if (typing(e)) return;
      const mod = e.metaKey || e.ctrlKey;

      if (e.key === " ") {
        e.preventDefault();
        if (e.repeat) return;
        // Twice in quick succession: stop and go back to the start.
        const now = performance.now();
        if (now - lastSpace < 350) {
          lastSpace = 0;
          void stop();
        } else {
          lastSpace = now;
          void togglePlay();
        }
      } else if (mod && e.key.toLowerCase() === "z") {
        e.preventDefault();
        void (e.shiftKey ? redo() : undo());
      } else if (mod && e.key.toLowerCase() === "y") {
        e.preventDefault();
        void redo();
      } else if (mod && ["a", "d"].includes(e.key.toLowerCase())) {
        // The playlist and the piano roll select and duplicate with these, when clicked last;
        // elsewhere they'd select the page's text or bookmark it.
        e.preventDefault();
      } else if (e.key === "F5") {
        e.preventDefault();
        setUi({ bottomOpen: false });
      } else if (e.key === "F6" || e.key === "F7" || e.key === "F9") {
        e.preventDefault();
        setUi({ bottomOpen: true, bottomPanel: e.key === "F6" ? "rack" : e.key === "F7" ? "piano" : "mixer" });
      } else if (e.key === "F8" || (mod && e.key.toLowerCase() === "b")) {
        e.preventDefault();
        setUi({ sidebarOpen: !useUi.getState().sidebarOpen });
      } else if (mod && e.key.toLowerCase() === "r") {
        e.preventDefault();
        setUi({ exportOpen: true });
      } else if (mod && e.key.toLowerCase() === "e") {
        e.preventDefault();
        splitAtPlayhead();
      } else if (mod && e.key.toLowerCase() === "s") {
        e.preventDefault();
        if (e.shiftKey) setUi({ projectDialog: "saveAs" });
        else void saveProject();
      } else if (mod && e.key.toLowerCase() === "n") {
        e.preventDefault();
        setUi({ projectDialog: "new" });
      } else if (mod && e.key.toLowerCase() === "o") {
        e.preventDefault();
        void browseForProject();
      } else if (e.key === "Home" || e.key === "Enter") {
        e.preventDefault();
        void setPosition(0);
      } else if (e.key.toLowerCase() === "l" && !mod) {
        void toggleLoop();
      }
    };

    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);
}
