import type { Channel, Instrument, Project } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { uniqueId } from "@/lib/model";
import { execute, toast } from "@/state/engine";
import { setUi } from "@/state/ui";
import type { PluginInfo } from "./PluginPicker";

/** An instrument to put on a channel: the built-in sampler or a plugin. */
export type InstrumentChoice = { kind: "sampler" } | { kind: "plugin"; plugin: PluginInfo };

export const SAMPLER: InstrumentChoice = { kind: "sampler" };

export function instrumentName(choice: InstrumentChoice) {
  return choice.kind === "sampler" ? "Sampler" : choice.plugin.name;
}

/** GrooveKit's own instruments have a picture of their panel; others are drawn as a card. */
const PICTURES = new Set(["Triad", "Quartet", "Sextet", "Kessel", "Groove-8", "Groove-9"]);

export function pictureOf(choice: InstrumentChoice): string | undefined {
  if (choice.kind !== "plugin" || choice.plugin.manufacturer !== "GrooveKit" || !PICTURES.has(choice.plugin.name)) return undefined;
  return `${import.meta.env.BASE_URL}instruments/${choice.plugin.name.toLowerCase()}.jpg`;
}

function instrumentOf(choice: InstrumentChoice): Instrument {
  if (choice.kind === "sampler") return { type: "sampler", layers: [] };
  const { format, uid, name, manufacturer } = choice.plugin;
  return { type: "plugin", plugin: { format, uid, name, manufacturer } };
}

/** A new channel playing the instrument, selected. */
export async function addInstrument(doc: Project, choice: InstrumentChoice) {
  const name = choice.kind === "sampler" ? `Sampler ${doc.channels.length + 1}` : choice.plugin.name;
  const id = uniqueId(choice.kind === "sampler" ? "sampler" : choice.plugin.name, doc.channels);
  await execute({ type: "addChannel", channel: { id, name, instrument: instrumentOf(choice) } }, { label: `Add ${instrumentName(choice)}` });
  setUi({ selectedChannel: id });
}

/** Puts another instrument on a channel. The old one goes, and so does the automation of its
    own parameters (a new instrument has other ones); the channel keeps its notes, insert,
    volume, pan and their automation. One undo step. */
export async function replaceInstrument(doc: Project, channel: Channel, choice: InstrumentChoice) {
  const own = doc.playlist.clips.filter((c) => c.type === "automation" && c.target.scope === "channel" && c.target.channel === channel.id
    && c.target.param !== "volume" && c.target.param !== "pan");
  const old = channel.instrument.type === "plugin" ? channel.instrument.plugin.name : "Sampler";
  const name = instrumentName(choice);
  const commands: Command[] = [
    ...own.map((c) => ({ type: "removeClip" as const, clip: c.id })),
    { type: "updateChannel", channel: channel.id, changes: { instrument: instrumentOf(choice), name, keyNames: null } },
  ];
  await execute(commands, { label: `Replace ${old} with ${name}` });
  setUi({ selectedChannel: channel.id });
  toast(own.length ? `${name} replaces ${old}; its ${own.length} automation clip${own.length > 1 ? "s" : ""} went with it` : `${name} replaces ${old}`);
}

/** Instruments dragged from the browser: the mime type, with the choice kept here too, since
    WebKit doesn't always pass custom types through the drag data. */
const INSTRUMENT_MIME = "application/x-groovekit-instrument";
let dragged: InstrumentChoice | undefined;

export function startInstrumentDrag(e: React.DragEvent, choice: InstrumentChoice) {
  dragged = choice;
  e.dataTransfer.setData(INSTRUMENT_MIME, JSON.stringify(choice));
  e.dataTransfer.setData("text/plain", instrumentName(choice));
  e.dataTransfer.effectAllowed = "copy";
}

export function endInstrumentDrag() {
  dragged = undefined;
}

export function isInstrumentDrag(e: React.DragEvent) {
  return dragged !== undefined || e.dataTransfer.types.includes(INSTRUMENT_MIME);
}

export function droppedInstrument(e: React.DragEvent): InstrumentChoice | undefined {
  const data = e.dataTransfer.getData(INSTRUMENT_MIME);
  const choice = data ? (JSON.parse(data) as InstrumentChoice) : dragged;
  dragged = undefined;
  return choice;
}
