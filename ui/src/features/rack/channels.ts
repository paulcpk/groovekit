import type { Channel, Project, SamplerInstrument } from "@groovekit/schema/project";
import { baseName, uniqueId } from "@/lib/model";
import { execute } from "@/state/engine";
import { setUi } from "@/state/ui";
import type { DraggedSample } from "@/features/playlist/geometry";

const drumCategories = ["kick", "snare", "clap", "hat", "hihat", "perc", "percussion", "cymbal", "tom", "rim", "shaker", "drum", "fx"];

/** The key a channel's steps use: its sampler's root key, or C4. */
export function stepKey(channel: Channel | undefined) {
  if (channel?.instrument.type === "sampler") return channel.instrument.layers[0]?.rootKey ?? 60;
  return 60;
}

/** A new sampler channel playing a sample; drums ignore pitch. */
export async function addSamplerChannel(doc: Project, sample: DraggedSample, index?: number) {
  const name = baseName(sample.path);
  const id = uniqueId(name, doc.channels);
  const drum = !sample.category || drumCategories.some((c) => sample.category!.toLowerCase().includes(c));
  const instrument: SamplerInstrument = {
    type: "sampler",
    layers: [{ id: "l1", sample: sample.path, pitchTracking: !drum, mode: drum ? "oneShot" : "gate" }],
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

/** Gives a channel its own mixer insert, as FL's "route to free insert". */
export async function routeToNewInsert(doc: Project, channel: Channel) {
  const id = uniqueId(channel.id, doc.mixer.inserts);
  await execute([
    { type: "addInsert", insert: { id, name: channel.name ?? channel.id } },
    { type: "updateChannel", channel: channel.id, changes: { insert: id } },
  ], { label: `Route ${channel.name ?? channel.id} to a new insert` });
}
