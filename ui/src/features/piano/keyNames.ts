import type { Channel } from "@groovekit/schema/project";
import { useEffect, useState } from "react";
import { create } from "zustand";
import { baseName } from "@/lib/model";
import { engine, listen } from "@/state/engine";

/** The General MIDI drum map: the keys drum instruments play by convention (36 the bass drum,
    38 the snare, 42 the closed hat), for drum plugins that don't name their keys. */
export const GENERAL_MIDI_DRUMS: Record<number, string> = {
  35: "Acoustic Bass Drum", 36: "Bass Drum", 37: "Side Stick", 38: "Snare", 39: "Hand Clap", 40: "Electric Snare",
  41: "Low Floor Tom", 42: "Closed Hat", 43: "High Floor Tom", 44: "Pedal Hat", 45: "Low Tom", 46: "Open Hat",
  47: "Low-Mid Tom", 48: "Hi-Mid Tom", 49: "Crash", 50: "High Tom", 51: "Ride", 52: "China", 53: "Ride Bell",
  54: "Tambourine", 55: "Splash", 56: "Cowbell", 57: "Crash 2", 58: "Vibraslap", 59: "Ride 2", 60: "High Bongo",
  61: "Low Bongo", 62: "Mute High Conga", 63: "Open High Conga", 64: "Low Conga", 65: "High Timbale",
  66: "Low Timbale", 67: "High Agogo", 68: "Low Agogo", 69: "Cabasa", 70: "Maracas", 71: "Short Whistle",
  72: "Long Whistle", 73: "Short Guiro", 74: "Long Guiro", 75: "Claves", 76: "High Wood Block",
  77: "Low Wood Block", 78: "Mute Cuica", 79: "Open Cuica", 80: "Mute Triangle", 81: "Open Triangle",
};

/** The General MIDI keys a kit mostly uses (kick to ride); drum rows show these and any other
    key a pattern plays, so all 47 don't crowd the view. */
const GENERAL_MIDI_CORE = [36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51];

/** Plugins' key names by channel, plugin and preset, asked of the engine once; and those
    already answered, for code that can't wait. */
const pluginNames = new Map<string, Promise<Record<string, string>>>();
const answered = new Map<string, Map<number, string>>();

const pluginKey = (channel: Channel) => channel.instrument.type === "plugin"
  ? `${channel.id}|${channel.instrument.plugin.uid}|${channel.instrument.plugin.preset ?? ""}` : "";

function askPlugin(channel: Channel) {
  const key = pluginKey(channel);
  if (!pluginNames.has(key)) {
    pluginNames.set(key, engine.call<{ keyNames: Record<string, string> }>("plugins.keyNames", { channel: channel.id })
      .then((r) => r.keyNames ?? {}, () => ({}))
      .then((names) => {
        answered.set(key, new Map(Object.entries(names).map(([k, v]) => [Number(k), v])));
        return names;
      }));
  }
  return pluginNames.get(key)!;
}

/** The names of a channel's keys as far as they're known now (a plugin's once the engine has
    answered), without waiting. */
export function knownKeyNames(channel: Channel): Map<number, string> {
  if (channel.keyNames === "generalMidi") return new Map(Object.entries(GENERAL_MIDI_DRUMS).map(([k, v]) => [Number(k), v]));
  if (channel.instrument.type === "sampler") return layerNames(channel);
  return answered.get(pluginKey(channel)) ?? new Map();
}

/** The names of a channel's keys, asking the engine if need be. */
export async function keyNamesOf(channel: Channel): Promise<Map<number, string>> {
  if (channel.instrument.type === "plugin" && channel.keyNames !== "generalMidi") await askPlugin(channel);
  return knownKeyNames(channel);
}

/** The key a channel's steps play: a kit's bass drum (or its lowest named key), else none. */
export function namedStepKey(channel: Channel): number | undefined {
  const names = knownKeyNames(channel);
  if (names.size === 0) return undefined;
  for (const [key, name] of names) if (/bass drum|kick/i.test(name)) return key;
  return Math.min(...names.keys());
}

/** The names of a channel's keys: General MIDI's when the channel says so, else what its
    instrument declares (a plugin's pitch names, or a sampler's layers on keys of their own,
    named after their samples). Empty when nothing names them. */
export function useKeyNames(channel: Channel): Map<number, string> {
  const fromLayers = channel.instrument.type === "sampler" ? layerNames(channel) : null;
  const plugin = channel.instrument.type === "plugin" ? channel.instrument.plugin : null;
  const cacheKey = pluginKey(channel);
  const [fromPlugin, setFromPlugin] = useState<Map<number, string>>(() => answered.get(cacheKey) ?? new Map());

  useEffect(() => {
    if (!plugin || channel.keyNames === "generalMidi") return;
    let current = true;
    void askPlugin(channel).then((names) => {
      if (current) setFromPlugin(new Map(Object.entries(names).map(([k, v]) => [Number(k), v])));
    });
    return () => {
      current = false;
    };
  }, [cacheKey, channel.keyNames]);

  if (channel.keyNames === "generalMidi") return new Map(Object.entries(GENERAL_MIDI_DRUMS).map(([k, v]) => [Number(k), v]));
  return fromLayers ?? (plugin ? fromPlugin : new Map());
}

/** A sampler's layers that sit on a key of their own (a kit: kick on one key, snare on
    another), named after their samples. */
function layerNames(channel: Channel): Map<number, string> {
  const names = new Map<number, string>();
  if (channel.instrument.type !== "sampler") return names;
  for (const layer of channel.instrument.layers) {
    const low = layer.keyRange?.low, high = layer.keyRange?.high;
    if (low !== undefined && low === high) names.set(low, baseName(layer.sample));
  }
  return names;
}

/** The rows the drum view shows, top to bottom from the lowest key: the named keys (for
    General MIDI, the kit's usual ones), and every other key the pattern plays so no note hides. */
export function drumRows(channel: Channel, names: Map<number, string>, played: number[], fallback: number): number[] {
  const named = channel.keyNames === "generalMidi" ? GENERAL_MIDI_CORE : [...names.keys()];
  const keys = new Set([...named, ...played]);
  if (keys.size === 0) keys.add(fallback);
  return [...keys].sort((a, b) => a - b);
}

/** Keys held on a MIDI keyboard or the app's keyboard right now, from the engine. */
export const useHeldKeys = create<{ held: Set<number> }>(() => ({ held: new Set() }));

listen("inputNotes", (params: { notes: [number, number][] }) => {
  const held = new Set(useHeldKeys.getState().held);
  for (const [key, velocity] of params.notes) velocity > 0 ? held.add(key) : held.delete(key);
  useHeldKeys.setState({ held });
});
