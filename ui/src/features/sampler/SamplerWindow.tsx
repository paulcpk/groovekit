import type { Channel, Envelope, SamplerFilter, SamplerInstrument, SamplerLayer } from "@groovekit/schema/project";
import { Trash2 } from "lucide-react";
import { useState } from "react";
import { DragNumber, IconButton, Knob, Tabs, ToggleChip } from "@/components/controls";
import { FloatingWindow } from "@/components/window";
import { PresetMenu } from "@/features/effects/PresetMenu";
import { droppedSample, isSampleDrag } from "@/features/playlist/geometry";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { cn } from "@/lib/cn";
import { useLiveControl } from "@/lib/liveControl";
import { colorFor, fileName, formatDb, formatPan, noteName, uniqueId } from "@/lib/model";
import { ChannelBar } from "@/features/rack/ChannelBar";
import { call, execute, useEngine } from "@/state/engine";
import type { OpenWindow } from "@/state/windows";
import { DEFAULT_ENVELOPE, EnvelopeGraph } from "./EnvelopeGraph";
import { FilterGraph } from "./FilterGraph";
import { Waveform } from "./Waveform";

type Tab = "sample" | "volume" | "filter";

/** A channel's window, for the samples in the rack and those dropped onto the playlist alike:
    where it plays and how loud, the sample and the part of it that plays, the volume envelope,
    and the filter with its own envelope. Audio clips in the playlist play the first layer. */
export function SamplerWindow({ window: w, channel, sampler }: { window: OpenWindow; channel: Channel; sampler: SamplerInstrument }) {
  const doc = useEngine((s) => s.doc)!;
  const [tab, setTab] = useState<Tab>("sample");
  const [layerId, setLayerId] = useState<string>();
  const [over, setOver] = useState(false);
  const layer = sampler.layers.find((l) => l.id === layerId) ?? sampler.layers[0];
  const color = colorFor(channel.id, channel.color);
  const clips = doc.playlist.clips.filter((c) => c.type === "audio" && c.channel === channel.id).length;

  const addLayers = (paths: string[]) => {
    const ids = sampler.layers.map((l) => ({ id: l.id }));
    const layers = paths.map((path) => {
      const id = uniqueId("l1", ids);
      ids.push({ id });
      return { id, sample: path, pitchTracking: sampler.layers[0]?.pitchTracking ?? false };
    });
    void execute(layers.map((l) => ({ type: "addSamplerLayer" as const, channel: channel.id, layer: l })),
      { label: paths.length === 1 ? `Add layer ${fileName(paths[0])}` : `Add ${paths.length} layers` }).then(() => setLayerId(layers[0]?.id));
  };

  return (
    <FloatingWindow window={w} width={520} accent={color}
      extra={
        <PresetMenu kind="sampler"
          current={() => ({ filter: sampler.filter ?? { type: "off" }, envelope: layer?.envelope ?? DEFAULT_ENVELOPE })}
          apply={(preset) => void execute([
            ...(preset.filter ? [{ type: "updateInstrument" as const, channel: channel.id, changes: { filter: preset.filter } }] : []),
            ...(preset.envelope ? sampler.layers.map((l) => ({ type: "updateSamplerLayer" as const, channel: channel.id, layer: l.id, changes: { envelope: preset.envelope } })) : []),
          ], { label: `Sampler preset: ${preset.name}` })} />
      }
      title={<><span className="h-3 w-1 shrink-0 rounded-sm" style={{ background: color }} /><span className="truncate">{channel.name || channel.id}</span><span className="text-2xs font-normal text-faint">Sampler</span></>}>
      <div
        className={cn("flex flex-col gap-3 p-3", over && "bg-accent/5")}
        onDragOver={(e) => {
          if (!(isSampleDrag(e) || isFileDrag(e))) return;
          e.preventDefault();
          setOver(true);
        }}
        onDragLeave={() => setOver(false)}
        onDrop={(e) => {
          setOver(false);
          if (isFileDrag(e)) {
            e.preventDefault();
            void importDroppedFiles(e).then((samples) => samples.length && addLayers(samples.map((s) => s.path)));
            return;
          }
          const sample = droppedSample(e);
          if (!sample) return;
          e.preventDefault();
          addLayers([sample.path]);
        }}
      >
        <ChannelBar doc={doc} channel={channel} />
        <div className="flex items-center gap-2">
          <Tabs<Tab> value={tab} onChange={setTab} items={[{ value: "sample", label: "Sample" }, { value: "volume", label: "Volume" }, { value: "filter", label: "Filter" }]} />
          <div className="flex-1" />
          {clips > 0 && <span className="text-2xs text-faint">{clips} clip{clips === 1 ? "" : "s"} in the playlist</span>}
          {sampler.layers.length > 1 && tab !== "filter" && (
            <div className="flex items-center gap-1" title="Layers">
              {sampler.layers.map((l, i) => (
                <button key={l.id} type="button" title={fileName(l.sample)}
                  className={cn("h-5 min-w-5 rounded-[3px] px-1 text-2xs tabular", l.id === layer?.id ? "bg-fg text-bg" : "bg-control text-dim hover:text-fg")}
                  onClick={() => setLayerId(l.id)}>{i + 1}</button>
              ))}
            </div>
          )}
        </div>

        {!layer ? (
          <div className="rounded-md border border-dashed border-line p-8 text-center text-xs text-faint">Drop a sample here</div>
        ) : tab === "sample" ? (
          <SampleTab channel={channel} sampler={sampler} layer={layer} color={color} first={layer === sampler.layers[0]} clips={clips} tempo={doc.tempo} />
        ) : tab === "volume" ? (
          <VolumeTab channel={channel} layer={layer} color={color} clips={clips} />
        ) : (
          <FilterTab channel={channel} filter={sampler.filter} color={color} />
        )}
        {layer && tab === "sample" && <div className="text-center text-2xs text-faint">Drop more samples to stack layers</div>}
      </div>
    </FloatingWindow>
  );
}

function useLayerCommand(channel: Channel, layer: SamplerLayer, label: string) {
  return useLiveControl<Partial<SamplerLayer>>((changes) => ({ type: "updateSamplerLayer", channel: channel.id, layer: layer.id, changes }), label);
}

function SampleTab({ channel, sampler, layer, color, first, clips, tempo }: {
  channel: Channel; sampler: SamplerInstrument; layer: SamplerLayer; color: string; first: boolean; clips: number; tempo: number;
}) {
  const set = (changes: Partial<SamplerLayer>, label: string) =>
    void execute({ type: "updateSamplerLayer", channel: channel.id, layer: layer.id, changes }, { label });
  const region = useLayerCommand(channel, layer, "Sample start and end");
  const gain = useLayerCommand(channel, layer, "Layer gain");
  const pan = useLayerCommand(channel, layer, "Layer pan");
  const key = layer.rootKey ?? 60;
  const mode = layer.mode ?? "oneShot";
  // A sample from the playlist isn't loaded to play notes until it has some, so pressing the
  // waveform plays the file instead.
  const plays = channel.group === "arrangement"
    ? { press: () => void call("preview.play", { path: layer.sample }), release: () => void call("preview.stop") }
    : { press: () => void call("live.noteOn", { channel: channel.id, key, velocity: 110 }), release: () => void call("live.noteOff", { channel: channel.id, key }) };

  return (
    <div className="flex flex-col gap-3">
      <div className="flex items-center gap-1.5">
        <div className="min-w-0 flex-1 truncate text-xs text-fg" title={layer.sample}>{fileName(layer.sample)}</div>
        <IconButton size="sm" title="Remove layer" onClick={() => void execute({ type: "removeSamplerLayer", channel: channel.id, layer: layer.id }, { label: "Remove layer" })}>
          <Trash2 size={11} />
        </IconButton>
      </div>
      <Waveform
        path={layer.sample} start={layer.start ?? 0} end={layer.end} reverse={!!layer.reverse} color={color}
        onRegion={(start, end, commit) => (commit ? region.onCommit : region.onChange)({ start, end: end ?? (null as never) })}
        onPress={plays.press}
        onRelease={plays.release}
      />
      <div className="flex flex-wrap items-center gap-1.5">
        <ToggleChip on={!!layer.reverse} label="Reverse" onClick={() => set({ reverse: !layer.reverse }, layer.reverse ? "Play forwards" : "Reverse")} />
        <ToggleChip on={layer.pitchTracking !== false} label="Follow keys" title="Pitch follows the played key (off for drums)"
          onClick={() => set({ pitchTracking: layer.pitchTracking === false }, "Pitch tracking")} />
        <ToggleChip on={mode === "gate"} tone="dim" label={mode === "gate" ? "Gate" : "One-shot"} title="One-shot plays to the end; gate stops on note-off"
          onClick={() => set({ mode: mode === "gate" ? "oneShot" : "gate" }, "Layer mode")} />
        {first && (clips > 0 || channel.group === "arrangement") && (
          <>
            <ToggleChip on={layer.sourceTempo !== undefined} label="Stretch to tempo" title="Audio clips are time-stretched to the song's tempo, keeping their pitch"
              onClick={() => set({ sourceTempo: layer.sourceTempo !== undefined ? (null as never) : tempo }, layer.sourceTempo !== undefined ? "Don't stretch" : "Stretch to tempo")} />
            {layer.sourceTempo !== undefined && (
              <span className="flex items-center gap-1 text-2xs text-dim">from
                <DragNumber value={layer.sourceTempo} min={20} max={999} step={0.1} format={(v) => `${v.toFixed(1)} BPM`} className="text-fg"
                  onCommit={(v) => set({ sourceTempo: Math.round(v * 10) / 10 }, "Source tempo")} />
              </span>
            )}
          </>
        )}
        {((layer.start ?? 0) > 0 || layer.end !== undefined) && (
          <button type="button" className="text-2xs text-faint hover:text-fg" onClick={() => set({ start: 0, end: null as never }, "Play the whole sample")}>Whole sample</button>
        )}
      </div>
      <div className="flex items-start justify-between rounded-md bg-raised px-2 py-2">
        <Knob size={30} label="Gain" value={layer.gainDb ?? 0} min={-48} max={24} defaultValue={0} format={(v) => `${formatDb(v)} dB`}
          onChange={(v) => gain.onChange({ gainDb: Math.round(v * 10) / 10 })} onCommit={(v) => gain.onCommit({ gainDb: Math.round(v * 10) / 10 })} />
        <Knob size={30} label="Pan" value={layer.pan ?? 0} min={-1} max={1} defaultValue={0} bipolar format={formatPan}
          onChange={(v) => pan.onChange({ pan: Math.round(v * 100) / 100 })} onCommit={(v) => pan.onCommit({ pan: Math.round(v * 100) / 100 })} />
        <Knob size={30} label="Pitch" value={layer.transpose ?? 0} min={-24} max={24} defaultValue={0} bipolar format={(v) => `${Math.round(v)} st`}
          onCommit={(v) => set({ transpose: Math.round(v) }, "Transpose")} />
        <Knob size={30} label="Fine" value={layer.fineTune ?? 0} min={-100} max={100} defaultValue={0} bipolar format={(v) => `${Math.round(v)} ct`}
          onCommit={(v) => set({ fineTune: Math.round(v) }, "Fine tune")} />
      </div>
      <div className="flex flex-wrap items-center gap-x-4 gap-y-1.5 text-2xs text-dim">
        <label className="flex items-center gap-1">Root
          <DragNumber value={key} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg" onCommit={(v) => set({ rootKey: v }, "Root key")} />
        </label>
        <label className="flex items-center gap-1">Keys
          <DragNumber value={layer.keyRange?.low ?? 0} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg"
            onCommit={(v) => set({ keyRange: { ...layer.keyRange, low: v } }, "Key range")} />
          –
          <DragNumber value={layer.keyRange?.high ?? 127} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg"
            onCommit={(v) => set({ keyRange: { ...layer.keyRange, high: v } }, "Key range")} />
        </label>
        <label className="flex items-center gap-1">Velocity
          <DragNumber value={layer.velocityRange?.low ?? 1} min={1} max={127} className="text-fg" onCommit={(v) => set({ velocityRange: { ...layer.velocityRange, low: v } }, "Velocity range")} />
          –
          <DragNumber value={layer.velocityRange?.high ?? 127} min={1} max={127} className="text-fg" onCommit={(v) => set({ velocityRange: { ...layer.velocityRange, high: v } }, "Velocity range")} />
        </label>
        <label className="flex items-center gap-1">Voices
          <DragNumber value={sampler.polyphony ?? 32} min={1} max={256} className="text-fg"
            onCommit={(v) => void execute({ type: "updateInstrument", channel: channel.id, changes: { polyphony: v } }, { label: "Sampler voices" })} />
        </label>
      </div>
    </div>
  );
}

function VolumeTab({ channel, layer, color, clips }: { channel: Channel; layer: SamplerLayer; color: string; clips: number }) {
  const envelope = useLayerCommand(channel, layer, "Volume envelope");
  return (
    <div className="flex flex-col gap-2">
      <EnvelopeGraph label="Volume" envelope={layer.envelope} color={color}
        onChange={(env) => envelope.onChange({ envelope: env })} onCommit={(env) => envelope.onCommit({ envelope: env })} />
      {clips > 0 && <div className="text-2xs text-faint">Over each audio clip, the attack starts where the clip starts and the release ends where it ends.</div>}
      {channel.group !== "arrangement" && (layer.mode ?? "oneShot") === "oneShot" && (
        <div className="text-2xs text-faint">One-shot layers play to their end and ignore note-off, so a note's release only applies in gate mode.</div>
      )}
    </div>
  );
}

/** A sampler's filter and its envelope. */
export function FilterTab({ channel, filter: saved, color }: { channel: Channel; filter: SamplerFilter | undefined; color: string }) {
  const filter = useLiveControl<SamplerFilter>((f) => ({ type: "updateInstrument", channel: channel.id, changes: { filter: f } }), "Filter");
  // A filter switched on here starts with a pluck: a quick fall back to the cutoff. One set up
  // elsewhere without an envelope has the engine's default.
  const envelope: Envelope = saved?.envelope
    ?? ((saved?.type ?? "off") === "off" ? { attack: 0, decay: 0.3, sustain: 0, release: 0.2 } : DEFAULT_ENVELOPE);
  const current = (): SamplerFilter => ({ ...saved, envelope });

  return (
    <div className="flex flex-col gap-3">
      <FilterGraph filter={saved} color={color} channel={channel.id}
        onChange={(change) => filter.onChange({ ...current(), ...change })}
        onCommit={(change) => filter.onCommit({ ...current(), ...change })} />
      <EnvelopeGraph label="Cutoff envelope" envelope={envelope} color={color}
        onChange={(env) => filter.onChange({ ...current(), envelope: env })} onCommit={(env) => filter.onCommit({ ...current(), envelope: env })} />
      <div className="text-2xs text-faint">
        The envelope moves the cutoff by up to its amount (Env amount), held at the sustain level while a note is held.
      </div>
    </div>
  );
}
