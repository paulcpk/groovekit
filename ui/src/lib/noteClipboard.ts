import type { Note } from "@groovekit/schema/project";

/** MIDI notes copied in the piano roll (⌘C) or from a channel's menu ("Copy MIDI notes"),
    pasted into any pattern or channel. Times are as copied, keys as played. */
let notes: Note[] = [];

export function copyNotes(copied: Note[]) {
  notes = copied.map((n) => ({ ...n }));
}

export function copiedNotes(): Note[] {
  return notes.map((n) => ({ ...n }));
}
