import type { Command } from "@groovekit/schema/protocol";
import { useRef } from "react";
import { execute } from "@/state/engine";
import { newGroup } from "./model";

/** For controls the engine applies without rebuilding the song (mixer volume, pan, sends, effect
    parameters): sends the value while it moves, at most every 60 ms, and the final one on release,
    all as one undoable group. */
export function useLiveControl(makeCommand: (value: number) => Command, label: string) {
  const gesture = useRef<{ group: string; last: number; pending?: number; timer?: ReturnType<typeof setTimeout> } | null>(null);
  const command = useRef(makeCommand);
  command.current = makeCommand;

  const flush = () => {
    const g = gesture.current;
    if (!g || g.pending === undefined) return;
    g.timer = undefined;
    g.last = Date.now();
    void execute(command.current(g.pending), { label, group: g.group });
    g.pending = undefined;
  };

  return {
    onChange(value: number) {
      const g = (gesture.current ??= { group: newGroup(), last: 0 });
      g.pending = value;
      if (Date.now() - g.last >= 60) flush();
      else g.timer ??= setTimeout(flush, 60);
    },
    onCommit(value: number) {
      const g = (gesture.current ??= { group: newGroup(), last: 0 });
      clearTimeout(g.timer);
      g.pending = value;
      flush();
      gesture.current = null;
    },
  };
}
