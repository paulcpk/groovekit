import { useEffect } from "react";
import { engine, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

/** MIDI keyboards and the typing keyboard play the channel selected in the channel rack. */
export function useLiveChannel() {
  const connected = useEngine((s) => s.status === "connected");
  const project = useEngine((s) => s.project);
  const selected = useUi((s) => s.selectedChannel);
  const firstChannel = useEngine((s) => s.doc?.channels[0]?.id);
  const channel = selected ?? firstChannel;

  useEffect(() => {
    if (connected) void engine.call("live.setChannel", { channel: channel ?? null }).catch(() => {});
  }, [connected, project, channel]);
}

// Two rows like a piano: Z S X D C V G B H N J M from C, and Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P an octave up.
const lower = ["z", "s", "x", "d", "c", "v", "g", "b", "h", "n", "j", "m", ",", "l", ".", ";", "/"];
const upper = ["q", "2", "w", "3", "e", "r", "5", "t", "6", "y", "7", "u", "i", "9", "o", "0", "p"];

function keyFor(key: string, octave: number) {
  const c = 12 * (octave + 1);
  const lo = lower.indexOf(key);
  if (lo >= 0) return c + lo;
  const up = upper.indexOf(key);
  if (up >= 0) return c + 12 + up;
  return undefined;
}

/** With the typing keyboard on, letter keys play notes instead of shortcuts; - and = shift the octave. */
export function useTypingKeyboard() {
  const on = useUi((s) => s.typingKeyboard);

  useEffect(() => {
    if (!on) return;
    const held = new Map<string, number>();

    const typing = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      return !!t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
    };

    const down = (e: KeyboardEvent) => {
      if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
      const k = e.key.toLowerCase();
      const octave = useUi.getState().keyboardOctave;

      if (k === "-" || k === "=") {
        e.preventDefault();
        e.stopImmediatePropagation();
        setUi({ keyboardOctave: Math.max(0, Math.min(8, octave + (k === "=" ? 1 : -1))) });
        return;
      }

      const note = keyFor(k, octave);
      if (note === undefined) return;
      // Ahead of the shortcuts (registered for the bubbling phase), which never see these keys.
      e.preventDefault();
      e.stopImmediatePropagation();
      if (e.repeat || held.has(k)) return;
      held.set(k, note);
      void engine.call("live.noteOn", { key: note, velocity: 100 }).catch(() => {});
    };

    const up = (e: KeyboardEvent) => {
      const k = e.key.toLowerCase();
      const note = held.get(k);
      if (note === undefined) return;
      held.delete(k);
      void engine.call("live.noteOff", { key: note }).catch(() => {});
    };

    const release = () => {
      for (const note of held.values()) void engine.call("live.noteOff", { key: note }).catch(() => {});
      held.clear();
    };

    window.addEventListener("keydown", down, true);
    window.addEventListener("keyup", up, true);
    window.addEventListener("blur", release);
    return () => {
      release();
      window.removeEventListener("keydown", down, true);
      window.removeEventListener("keyup", up, true);
      window.removeEventListener("blur", release);
    };
  }, [on]);
}
