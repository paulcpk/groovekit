import { useEffect } from "react";
import { create } from "zustand";
import { engine, listen, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

/** When a note played from here (the typing keyboard, or pressing a sampler's wave) last started
    and ended, so envelopes can show it running through them. */
export const useNotePlayed = create<{ on?: number; off?: number }>(() => ({}));

export function notePlayed(on: boolean) {
  useNotePlayed.setState(on ? { on: performance.now(), off: undefined } : { off: performance.now() });
}

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

/** With the typing keyboard on, letter keys play notes instead of shortcuts; - and = shift the octave.
    Keys typed in a plugin's window come from the engine, whose window that is. */
export function useTypingKeyboard() {
  const on = useUi((s) => s.typingKeyboard);

  useEffect(() => {
    if (!on) return;
    const held = new Map<string, number>();

    const typing = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      return !!t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
    };

    /** Whether the key is the typing keyboard's (then it's used up). */
    const press = (k: string, repeat: boolean) => {
      const octave = useUi.getState().typingOctave;

      if (k === "-" || k === "=") {
        setUi({ typingOctave: Math.max(0, Math.min(8, octave + (k === "=" ? 1 : -1))) });
        return true;
      }

      const note = keyFor(k, octave);
      if (note === undefined) return false;
      if (repeat || held.has(k)) return true;
      held.set(k, note);
      notePlayed(true);
      void engine.call("live.noteOn", { key: note, velocity: 100 }).catch(() => {});
      return true;
    };

    const lift = (k: string) => {
      const note = held.get(k);
      if (note === undefined) return;
      held.delete(k);
      if (!held.size) notePlayed(false);
      void engine.call("live.noteOff", { key: note }).catch(() => {});
    };

    const down = (e: KeyboardEvent) => {
      if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
      // Ahead of the shortcuts (registered for the bubbling phase), which never see these keys.
      if (press(e.key.toLowerCase(), e.repeat)) {
        e.preventDefault();
        e.stopImmediatePropagation();
      }
    };

    const up = (e: KeyboardEvent) => lift(e.key.toLowerCase());

    const release = () => {
      if (!held.size) return;
      for (const note of held.values()) void engine.call("live.noteOff", { key: note }).catch(() => {});
      held.clear();
      notePlayed(false);
    };

    const stopListening = listen("typingKey", ({ key, down }: { key: string; down: boolean }) => {
      if (down) press(key, false);
      else lift(key);
    });

    window.addEventListener("keydown", down, true);
    window.addEventListener("keyup", up, true);
    window.addEventListener("blur", release);
    return () => {
      release();
      stopListening();
      window.removeEventListener("keydown", down, true);
      window.removeEventListener("keyup", up, true);
      window.removeEventListener("blur", release);
    };
  }, [on]);
}
