import type { Channel, Project, SamplerInstrument } from "@groovekit/schema/project";
import { baseName, uniqueId } from "@/lib/model";
import { setUi } from "@/state/ui";
import { call, execute } from "@/state/engine";
import { openWindow, toggleWindow } from "@/state/windows";
import type { DraggedSample } from "@/features/playlist/geometry";

const drumCategories = ["kick", "snare", "clap", "hat", "hihat", "perc", "percussion", "cymbal", "tom", "rim", "shaker", "drum", "fx"];

/** The key a channel's steps use: its sampler's root key, or C4. */
export function stepKey(channel: Channel | undefined) {
  if (channel?.instrument.type === "sampler") return channel.instrument.layers[0]?.rootKey ?? 60;
  return 60;
}

/** A new sampler channel playing a sample: its pitch follows the keys played, drums included
    (they still play at their own pitch on the root key, which steps use). */
export async function addSamplerChannel(doc: Project, sample: DraggedSample, index?: number) {
  const name = baseName(sample.path);
  const id = uniqueId(name, doc.channels);
  const drum = !sample.category || drumCategories.some((c) => sample.category!.toLowerCase().includes(c));
  const instrument: SamplerInstrument = {
    type: "sampler",
    layers: [{ id: "l1", sample: sample.path, mode: drum ? "oneShot" : "gate" }],
  };
  await execute({ type: "addChannel", channel: { id, name, instrument }, ...(index !== undefined ? { index } : {}) }, { label: `Add channel ${name}` });
  setUi({ selectedChannel: id });
  return id;
}

export async function addEmptySampler(doc: Project) {
  const id = uniqueId("sampler", doc.channels);
  await execute({ type: "addChannel", channel: { id, name: `Sampler ${doc.channels.length + 1}`, instrument: { type: "sampler", layers: [] } } }, { label: "Add sampler channel" });
  setUi({ selectedChannel: id });
}

/** Gives a channel a mixer insert of its own. */
export async function routeToNewInsert(doc: Project, channel: Channel) {
  const id = uniqueId(channel.id, doc.mixer.inserts);
  await execute([
    { type: "addInsert", insert: { id, name: channel.name ?? channel.id } },
    { type: "updateChannel", channel: channel.id, changes: { insert: id } },
  ], { label: `Route ${channel.name ?? channel.id} to a new insert` });
}

/** Selects a channel and opens its sampler window, or the plugin's own window. */
export function openChannel(channel: Channel, toggle = false) {
  setUi({ selectedChannel: channel.id });
  if (channel.instrument.type === "plugin") void call("plugins.openEditor", { channel: channel.id });
  else (toggle ? toggleWindow : openWindow)({ kind: "channel", channel: channel.id });
}

/** A sample dropped on a sampler channel replaces its first layer's sample (the one its audio
    clips play), keeping its settings. */
export async function replaceSample(channel: Channel, path: string) {
  if (channel.instrument.type !== "sampler") return;
  const layer = channel.instrument.layers[0];
  await execute(layer
    ? { type: "updateSamplerLayer", channel: channel.id, layer: layer.id, changes: { sample: path, start: 0, end: null } }
    : { type: "addSamplerLayer", channel: channel.id, layer: { id: "l1", sample: path } },
    { label: `Replace sample with ${baseName(path)}` });
}
