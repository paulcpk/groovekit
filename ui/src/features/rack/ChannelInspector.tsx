import type { Channel, SamplerInstrument, SamplerLayer } from "@groovekit/schema/project";
import { Play, Trash2, X } from "lucide-react";
import { useState } from "react";
import { Button, DragNumber, IconButton, Knob, ToggleChip } from "@/components/controls";
import { droppedSample, isSampleDrag } from "@/features/playlist/geometry";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { cn } from "@/lib/cn";
import { colorFor, fileName, formatDb, formatPan, noteName, uniqueId } from "@/lib/model";
import { call, execute, toast, useEngine } from "@/state/engine";
import { setUi } from "@/state/ui";

export function ChannelInspector({ channelId }: { channelId: string }) {
  const channel = useEngine((s) => s.doc?.channels.find((c) => c.id === channelId));
  const [over, setOver] = useState(false);
  if (!channel) return null;

  const color = colorFor(channel.id, channel.color);

  return (
    <div
      className={cn("flex h-full flex-col", over && "bg-accent/5")}
      onDragOver={(e) => {
        if (channel.instrument.type !== "sampler" || !(isSampleDrag(e) || isFileDrag(e))) return;
        e.preventDefault();
        e.stopPropagation();
        setOver(true);
      }}
      onDragLeave={() => setOver(false)}
      onDrop={(e) => {
        setOver(false);
        if (channel.instrument.type !== "sampler") return;
        const instrument = channel.instrument;
        const addLayers = (paths: string[]) => {
          const ids = instrument.layers.map((l) => ({ id: l.id }));
          void execute(paths.map((path) => {
            const id = uniqueId("l1", ids);
            ids.push({ id });
            return { type: "addSamplerLayer" as const, channel: channel.id, layer: { id, sample: path, pitchTracking: instrument.layers[0]?.pitchTracking ?? false } };
          }), { label: paths.length === 1 ? `Add layer ${fileName(paths[0])}` : `Add ${paths.length} layers` });
        };
        if (isFileDrag(e)) {
          e.preventDefault();
          e.stopPropagation();
          void importDroppedFiles(e).then((samples) => samples.length && addLayers(samples.map((s) => s.path)));
          return;
        }
        const sample = droppedSample(e);
        if (!sample) return;
        e.preventDefault();
        e.stopPropagation();
        addLayers([sample.path]);
      }}
    >
      <div className="flex h-8 shrink-0 items-center gap-2 border-b border-line px-2">
        <span className="h-3 w-1 rounded-sm" style={{ background: color }} />
        <ChannelName channel={channel} />
        <IconButton size="sm" title="Close" onClick={() => setUi({ selectedChannel: undefined })}><X size={12} /></IconButton>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto p-2">
        {channel.instrument.type === "sampler" ? (
          <SamplerEditor channel={channel} sampler={channel.instrument} />
        ) : (
          <PluginInstrument channel={channel} />
        )}
      </div>
    </div>
  );
}

function ChannelName({ channel }: { channel: Channel }) {
  const [editing, setEditing] = useState(false);
  if (editing) {
    return (
      <input
        autoFocus defaultValue={channel.name ?? channel.id}
        className="h-6 flex-1 rounded-[3px] bg-bg px-1.5 text-sm outline-none ring-1 ring-accent"
        onBlur={(e) => {
          setEditing(false);
          const v = e.target.value.trim();
          if (v && v !== channel.name) void execute({ type: "updateChannel", channel: channel.id, changes: { name: v } }, { label: "Rename channel" });
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") (e.target as HTMLInputElement).blur();
          if (e.key === "Escape") setEditing(false);
          e.stopPropagation();
        }}
      />
    );
  }
  return <div className="flex-1 truncate text-sm font-medium" onDoubleClick={() => setEditing(true)} title="Double-click to rename">{channel.name ?? channel.id}</div>;
}

function SamplerEditor({ channel, sampler }: { channel: Channel; sampler: SamplerInstrument }) {
  return (
    <div className="flex flex-col gap-2">
      <div className="flex items-center gap-2 text-xs text-dim">
        <span>Sampler</span>
        <span className="text-faint">·</span>
        <span>Voices</span>
        <DragNumber value={sampler.polyphony ?? 16} min={1} max={128} className="text-fg"
          onCommit={(v) => void execute({ type: "patch", ops: [{ op: "add", path: `/channels/${indexOf(channel)}/instrument/polyphony`, value: v }] }, { label: "Sampler voices" })} />
      </div>
      {sampler.layers.length === 0 && (
        <div className="rounded-md border border-dashed border-line p-4 text-center text-xs text-faint">Drop a sample here to add a layer</div>
      )}
      {sampler.layers.map((layer) => <LayerEditor key={layer.id} channel={channel} layer={layer} />)}
      {sampler.layers.length > 0 && <div className="py-1 text-center text-2xs text-faint">Drop more samples to stack layers</div>}
    </div>
  );
}

function indexOf(channel: Channel) {
  return useEngine.getState().doc?.channels.findIndex((c) => c.id === channel.id) ?? -1;
}

function LayerEditor({ channel, layer }: { channel: Channel; layer: SamplerLayer }) {
  const set = (changes: Record<string, unknown>, label: string) =>
    void execute({ type: "updateSamplerLayer", channel: channel.id, layer: layer.id, changes }, { label });
  const env = layer.envelope ?? {};
  const setEnv = (k: string, v: number, label: string) => set({ envelope: { ...env, [k]: Math.round(v * 1000) / 1000 } }, label);

  return (
    <div className="rounded-md bg-raised p-2">
      <div className="mb-2 flex items-center gap-1.5">
        <IconButton size="sm" title="Preview" onClick={() => void call("preview.play", { path: layer.sample })}><Play size={11} /></IconButton>
        <div className="min-w-0 flex-1 truncate text-xs text-fg" title={layer.sample}>{fileName(layer.sample)}</div>
        <IconButton size="sm" title="Remove layer" onClick={() => void execute({ type: "removeSamplerLayer", channel: channel.id, layer: layer.id }, { label: "Remove layer" })}>
          <Trash2 size={11} />
        </IconButton>
      </div>

      <div className="mb-2 flex flex-wrap items-center gap-x-3 gap-y-1.5 text-2xs text-dim">
        <label className="flex items-center gap-1">Root
          <DragNumber value={layer.rootKey ?? 60} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg" onCommit={(v) => set({ rootKey: v }, "Root key")} />
        </label>
        <label className="flex items-center gap-1">Keys
          <DragNumber value={layer.keyRange?.low ?? 0} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg"
            onCommit={(v) => set({ keyRange: { ...layer.keyRange, low: v } }, "Key range")} />
          –
          <DragNumber value={layer.keyRange?.high ?? 127} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg"
            onCommit={(v) => set({ keyRange: { ...layer.keyRange, high: v } }, "Key range")} />
        </label>
        <label className="flex items-center gap-1">Vel
          <DragNumber value={layer.velocityRange?.low ?? 1} min={1} max={127} className="text-fg" onCommit={(v) => set({ velocityRange: { ...layer.velocityRange, low: v } }, "Velocity range")} />
          –
          <DragNumber value={layer.velocityRange?.high ?? 127} min={1} max={127} className="text-fg" onCommit={(v) => set({ velocityRange: { ...layer.velocityRange, high: v } }, "Velocity range")} />
        </label>
        <ToggleChip on={layer.pitchTracking !== false} label="Pitch" title="Follow the played key (off for drums)"
          onClick={() => set({ pitchTracking: layer.pitchTracking === false }, "Pitch tracking")} />
        <ToggleChip on={(layer.mode ?? "oneShot") === "gate"} tone="dim" label={(layer.mode ?? "oneShot") === "gate" ? "Gate" : "One-shot"} title="One-shot plays to the end; gate stops on note-off"
          onClick={() => set({ mode: (layer.mode ?? "oneShot") === "gate" ? "oneShot" : "gate" }, "Layer mode")} />
      </div>

      <div className="flex items-start justify-between">
        <Knob size={26} label="Gain" value={layer.gainDb ?? 0} min={-48} max={24} defaultValue={0} format={(v) => `${formatDb(v)} dB`} onCommit={(v) => set({ gainDb: Math.round(v * 10) / 10 }, "Layer gain")} />
        <Knob size={26} label="Pan" value={layer.pan ?? 0} min={-1} max={1} defaultValue={0} bipolar format={formatPan} onCommit={(v) => set({ pan: Math.round(v * 100) / 100 }, "Layer pan")} />
        <Knob size={26} label="Tune" value={layer.transpose ?? 0} min={-24} max={24} defaultValue={0} bipolar format={(v) => `${Math.round(v)} st`} onCommit={(v) => set({ transpose: Math.round(v) }, "Transpose")} />
        <Knob size={26} label="Fine" value={layer.fineTune ?? 0} min={-100} max={100} defaultValue={0} bipolar format={(v) => `${Math.round(v)} ct`} onCommit={(v) => set({ fineTune: Math.round(v) }, "Fine tune")} />
      </div>
      <div className="mt-2 flex items-start justify-between border-t border-line pt-2">
        <Knob size={24} label="A" title="Attack" value={env.attack ?? 0} min={0} max={2} defaultValue={0} format={(v) => `${Math.round(v * 1000)} ms`} onCommit={(v) => setEnv("attack", v, "Attack")} />
        <Knob size={24} label="D" title="Decay" value={env.decay ?? 0} min={0} max={4} defaultValue={0} format={(v) => `${Math.round(v * 1000)} ms`} onCommit={(v) => setEnv("decay", v, "Decay")} />
        <Knob size={24} label="S" title="Sustain" value={env.sustain ?? 1} min={0} max={1} defaultValue={1} format={(v) => `${Math.round(v * 100)}%`} onCommit={(v) => setEnv("sustain", v, "Sustain")} />
        <Knob size={24} label="R" title="Release" value={env.release ?? 0.05} min={0} max={4} defaultValue={0.05} format={(v) => `${Math.round(v * 1000)} ms`} onCommit={(v) => setEnv("release", v, "Release")} />
      </div>
    </div>
  );
}

function PluginInstrument({ channel }: { channel: Channel }) {
  if (channel.instrument.type !== "plugin") return null;
  const plugin = channel.instrument.plugin;

  return (
    <div className="flex flex-col gap-2 text-xs">
      <div className="rounded-md bg-raised p-2">
        <div className="text-sm text-fg">{plugin.name}</div>
        <div className="text-dim">{plugin.manufacturer} · {plugin.format}</div>
        {plugin.state && <div className="mt-1 truncate text-2xs text-faint" title={plugin.state}>State: {plugin.state}</div>}
      </div>
      <div className="flex gap-1.5">
        <Button size="sm" className="bg-control text-fg" onClick={() => void call("plugins.openEditor", { channel: channel.id })}>Open plugin window</Button>
        <Button size="sm" onClick={() =>
          void call("plugins.captureState", { channel: channel.id }).then((r) => toast(r.changed ? `Saved the plugin's state (${Math.round(r.bytes / 1024)} KB)` : "Nothing changed since the last save"))}>
          Save state
        </Button>
      </div>
      <div className="text-2xs text-faint">Closing the plugin's window saves what you changed there into the project.</div>
    </div>
  );
}
