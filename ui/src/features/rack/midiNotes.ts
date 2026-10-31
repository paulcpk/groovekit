import type { Channel, Pattern, Project } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { keyNamesOf } from "@/features/piano/keyNames";
import { copiedNotes, copyNotes } from "@/lib/noteClipboard";
import { noteName, ticksPerBar } from "@/lib/model";
import { execute, toast } from "@/state/engine";

/** A channel's MIDI notes in a pattern, onto the clipboard the piano roll's ⌘C and ⌘V share. */
export function copyMidiNotes(pattern: Pattern, channel: Channel) {
  const notes = pattern.notes.filter((n) => n.channel === channel.id);
  copyNotes(notes);
  toast(notes.length ? `Copied ${notes.length} MIDI note${notes.length > 1 ? "s" : ""} from ${channel.name ?? channel.id}` : `${channel.name ?? channel.id} has no notes in this pattern`);
}

/** The copied MIDI notes onto a channel in a pattern, at their times and keys: in place of its
    notes there, or added to them. A pattern with a set length grows in whole bars to hold them.
    When the channel names its keys and none of the notes lands on one, it says so: they'd
    otherwise be silent with no hint why. */
export async function pasteMidiNotes(doc: Project, pattern: Pattern, channel: Channel, add: boolean) {
  const pasted = copiedNotes().map((n) => ({ ...n, channel: channel.id }));
  if (!pasted.length) {
    toast("No MIDI notes copied yet: right-click a channel and Copy MIDI notes, or ⌘C in the piano roll");
    return;
  }
  const notes = add ? [...pattern.notes.filter((n) => n.channel === channel.id), ...pasted] : pasted;
  const end = Math.max(...notes.map((n) => n.start + n.length));
  const bar = ticksPerBar(doc);
  const commands: Command[] = [];
  if (pattern.length && end > pattern.length) commands.push({ type: "updatePattern", pattern: pattern.id, changes: { length: Math.ceil(end / bar) * bar } });
  commands.push({ type: "setNotes", pattern: pattern.id, channel: channel.id, notes });
  const name = channel.name ?? channel.id;
  await execute(commands, { label: `Paste ${pasted.length} MIDI note${pasted.length > 1 ? "s" : ""} onto ${name}` });

  const names = await keyNamesOf(channel);
  if (names.size && !pasted.some((n) => names.has(n.key ?? 60))) {
    const keys = [...new Set(pasted.map((n) => noteName(n.key ?? 60)))].slice(0, 3).join(", ");
    toast(`None of these notes (${keys}) lands on a key ${name} plays: move them in the piano roll's drum view`);
  }
}
