import type { Clip, Project } from "@groovekit/schema/project";
import { useEffect } from "react";
import { uniqueId } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { redo, setPosition, stop, togglePlay, toggleLoop, undo } from "./actions";
import { browseForProject, saveProject } from "./ProjectDialog";
import { splitAtPlayhead } from "@/features/playlist/clipOps";
import { useVoiceRequest } from "@/features/agent/voice";

let clipboard: Clip[] = [];
let lastSpace = 0;

/** Copies of clips moved by an amount, with fresh ids. */
function copiesOf(doc: Project, clips: Clip[], shift: number) {
  const taken = new Set(doc.playlist.clips.map((c) => c.id));
  return clips.map((c) => {
    const id = uniqueId(c.id.replace(/-\d+$/, ""), taken);
    taken.add(id);
    return { ...c, id, start: c.start + shift };
  });
}

function addClips(clips: Clip[], label: string) {
  if (clips.length === 0) return;
  void execute(clips.map((clip) => ({ type: "addClip" as const, clip })), { label });
  setUi({ selectedClips: clips.map((c) => c.id) });
}

function typing(e: KeyboardEvent) {
  const t = e.target as HTMLElement | null;
  return !!t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
}

/** Global keys: F5-F9 for the views, Space to play, the usual editing keys. */
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
      } else if (mod && ["c", "v", "d", "a"].includes(e.key.toLowerCase())) {
        // Selected text (in the agent's conversation, say) copies as text.
        const selection = window.getSelection();
        if (e.key.toLowerCase() === "c" && selection && !selection.isCollapsed && selection.toString().trim()) return;

        const doc = useEngine.getState().doc;
        if (!doc) return;
        const selected = doc.playlist.clips.filter((c) => useUi.getState().selectedClips.includes(c.id));
        const key = e.key.toLowerCase();
        e.preventDefault();

        if (key === "a") {
          setUi({ selectedClips: doc.playlist.clips.map((c) => c.id) });
        } else if (key === "c") {
          clipboard = selected;
        } else if (key === "d" && selected.length) {
          const span = Math.max(...selected.map((c) => c.start + c.length)) - Math.min(...selected.map((c) => c.start));
          addClips(copiesOf(doc, selected, span), selected.length === 1 ? "Duplicate clip" : `Duplicate ${selected.length} clips`);
        } else if (key === "v" && clipboard.length) {
          // Pasted at the playhead, keeping their spacing and tracks.
          const first = Math.min(...clipboard.map((c) => c.start));
          const at = useEngine.getState().transport.position;
          const clips = copiesOf(doc, clipboard, at - first);
          addClips(clips, clips.length === 1 ? "Paste clip" : `Paste ${clips.length} clips`);
        }
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
        else saveProject();
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
