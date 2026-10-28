import type { Channel, Envelope, ModulationTarget, SamplerFilter, SamplerInstrument, SamplerLayer, SamplerModulation } from "@groovekit/schema/project";
import { Trash2 } from "lucide-react";
import { useState } from "react";
import { DragNumber, IconButton, Knob, Tabs, ToggleChip } from "@/components/controls";
import { FloatingWindow } from "@/components/window";
import { PresetMenu } from "@/features/effects/PresetMenu";
import { droppedSample, isSampleDrag } from "@/features/playlist/geometry";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { notePlayed } from "@/features/shell/liveInput";
import { cn } from "@/lib/cn";
import { useHeld } from "@/lib/held";
import { useLiveControl } from "@/lib/liveControl";
import { barRange, barsAt, describeFit, fitCommands, fitMode, formatBars, suggestedBars, tempoFor } from "@/lib/fit";
import { colorFor, fileName, formatDb, formatPan, noteName, uniqueId } from "@/lib/model";
import { usePeaks } from "@/lib/peaks";
import { ChannelBar } from "@/features/rack/ChannelBar";
import { call, execute, useEngine } from "@/state/engine";
import { setUi } from "@/state/ui";
import type { OpenWindow } from "@/state/windows";
import { DEFAULT_ENVELOPE, EnvelopeGraph } from "./EnvelopeGraph";
import { FilterGraph } from "./FilterGraph";
import { GrainsPanel } from "./GrainsPanel";
import { ProcessPanel } from "./ProcessPanel";
import { DEFAULT_LFO, LfoPanel, type LfoValues } from "./LfoPanel";
import { Waveform } from "./Waveform";

type Tab = "sample" | "grains" | "envelopes" | "filter";
type Target = "volume" | "cutoff" | "resonance" | "pan" | "pitch";

const TARGETS: { value: Target; label: string; color: string }[] = [
  { value: "volume", label: "Volume", color: "#7ee08a" },
  { value: "cutoff", label: "Cutoff", color: "#f2a33a" },
  { value: "resonance", label: "Resonance", color: "#e0d257" },
  { value: "pan", label: "Pan", color: "#58b7d6" },
  { value: "pitch", label: "Pitch", color: "#c47ef0" },
];

/** A channel's window, for the samples in the rack and those dropped onto the playlist alike:
    the sample and what's done to it, the envelopes and LFOs that move its volume, cutoff,
    resonance, pan and pitch over each note, and the filter. Audio clips in the playlist play the
    first layer. */
export function SamplerWindow({ window: w, channel, sampler }: { window: OpenWindow; channel: Channel; sampler: SamplerInstrument }) {
  const doc = useEngine((s) => s.doc)!;
  const [tab, setTab] = useState<Tab>("sample");
  const [target, setTarget] = useState<Target>("volume");
  const [layerId, setLayerId] = useState<string>();
  const [over, setOver] = useState(false);
  const saved = sampler.layers.find((l) => l.id === layerId) ?? sampler.layers[0];
  const color = colorFor(channel.id, channel.color);
  const clips = doc.playlist.clips.filter((c) => c.type === "audio" && c.channel === channel.id).length;

  const addLayers = (paths: string[]) => {
    const ids = sampler.layers.map((l) => ({ id: l.id }));
    const layers = paths.map((path) => {
      const id = uniqueId("l1", ids);
      ids.push({ id });
      return { id, sample: path, pitchTracking: sampler.layers[0]?.pitchTracking ?? true };
    });
    void execute(layers.map((l) => ({ type: "addSamplerLayer" as const, channel: channel.id, layer: l })),
      { label: paths.length === 1 ? `Add layer ${fileName(paths[0])}` : `Add ${paths.length} layers` }).then(() => setLayerId(layers[0]?.id));
  };

  return (
    <FloatingWindow window={w} width={680} accent={color}
      extra={
        <PresetMenu kind="sampler"
          current={() => ({ filter: sampler.filter ?? { type: "off" }, envelope: saved?.envelope ?? DEFAULT_ENVELOPE, modulation: sampler.modulation ?? {} })}
          apply={(preset) => void execute([
            ...(preset.filter || preset.modulation ? [{
              type: "updateInstrument" as const, channel: channel.id,
              changes: { ...(preset.filter ? { filter: preset.filter } : {}), ...(preset.modulation ? { modulation: preset.modulation } : {}) },
            }] : []),
            ...(preset.envelope ? sampler.layers.map((l) => ({ type: "updateSamplerLayer" as const, channel: channel.id, layer: l.id, changes: { envelope: preset.envelope } })) : []),
          ], { label: `Sampler preset: ${preset.name}` })} />
      }
      title={<><span className="h-3 w-1 shrink-0 rounded-sm" style={{ background: color }} /><span className="truncate">{channel.name || channel.id}</span><span className="text-2xs font-normal text-faint">Sampler</span></>}>
      <div
        className={cn("flex flex-col gap-3 p-3", over && "bg-accent/5")}
        // The keyboards play the channel whose window was touched last.
        onPointerDownCapture={() => setUi({ selectedChannel: channel.id })}
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
          <Tabs<Tab> value={tab} onChange={setTab} items={[
            { value: "sample", label: "Sample" },
            { value: "grains", label: <span className="flex items-center gap-1">Grains{saved?.grains?.on && <span className="h-1.5 w-1.5 rounded-full" style={{ background: color }} />}</span>,
              title: "Granular: pitch and speed apart" },
            { value: "envelopes", label: "Envelopes" },
            { value: "filter", label: "Filter" },
          ]} />
          <div className="flex-1" />
          {clips > 0 && <span className="text-2xs text-faint">{clips} clip{clips === 1 ? "" : "s"} in the playlist</span>}
          {sampler.layers.length > 1 && (tab === "sample" || tab === "grains" || (tab === "envelopes" && target === "volume")) && (
            <div className="flex items-center gap-1" title="Layers">
              {sampler.layers.map((l, i) => (
                <button key={l.id} type="button" title={fileName(l.sample)}
                  className={cn("h-5 min-w-5 rounded-[3px] px-1 text-2xs tabular", l.id === saved?.id ? "bg-fg text-bg" : "bg-control text-dim hover:text-fg")}
                  onClick={() => setLayerId(l.id)}>{i + 1}</button>
              ))}
            </div>
          )}
        </div>

        {!saved ? (
          <div className="rounded-md border border-dashed border-line p-8 text-center text-xs text-faint">Drop a sample here</div>
        ) : tab === "sample" ? (
          <SampleTab channel={channel} sampler={sampler} saved={saved} color={color} first={saved === sampler.layers[0]} clips={clips} />
        ) : tab === "grains" ? (
          <GrainsTab channel={channel} saved={saved} color={color} />
        ) : tab === "envelopes" ? (
          <EnvelopesTab channel={channel} sampler={sampler} layer={saved} target={target} setTarget={setTarget} clips={clips} tempo={doc.tempo}
            showFilter={() => setTab("filter")} />
        ) : (
          <FilterTab channel={channel} filter={sampler.filter} color={color} showEnvelope={() => { setTarget("cutoff"); setTab("envelopes"); }} />
        )}
        {saved && tab === "sample" && <div className="text-center text-2xs text-faint">Drop more samples to stack layers</div>}
      </div>
    </FloatingWindow>
  );
}

/** The layer as last set here, and a way to change it: while a control moves (one undo step),
    or at once with a label. */
function useLayer(channel: Channel, saved: SamplerLayer) {
  const [layer, hold] = useHeld(saved, saved.id);
  const live = useLiveControl<Partial<SamplerLayer>>((changes) => ({ type: "updateSamplerLayer", channel: channel.id, layer: saved.id, changes }), "Sampler layer");
  return {
    layer,
    change(changes: Partial<SamplerLayer>, commit = true, label?: string) {
      hold({ ...layer, ...changes });
      if (label) void execute({ type: "updateSamplerLayer", channel: channel.id, layer: saved.id, changes }, { label });
      else (commit ? live.onCommit : live.onChange)(changes);
    },
  };
}

function GrainsTab({ channel, saved, color }: { channel: Channel; saved: SamplerLayer; color: string }) {
  const { layer, change } = useLayer(channel, saved);
  const key = layer.rootKey ?? 60;
  return (
    <GrainsPanel layer={layer} color={color} change={change}
      onPress={() => {
        notePlayed(true);
        void call("live.noteOn", { channel: channel.id, key, velocity: 110 });
      }}
      onRelease={() => {
        notePlayed(false);
        void call("live.noteOff", { channel: channel.id, key });
      }} />
  );
}

/** How the sample fits the song in the arrangement: as recorded, stretched (pitch kept) or
    resampled (pitch follows) to fill a number of bars, which it keeps filling at any tempo. */
function FitToSong({ channel, layer }: { channel: Channel; layer: SamplerLayer }) {
  const doc = useEngine((s) => s.doc)!;
  const peaks = usePeaks(layer.sample);
  if (!peaks) return null;

  const mode = fitMode(layer);
  const seconds = Math.max(0.001, (layer.end ?? peaks.durationSeconds) - (layer.start ?? 0));
  const range = barRange(doc, seconds);
  const bars = layer.sourceTempo ? barsAt(doc, seconds, layer.sourceTempo) : suggestedBars(doc, seconds);
  const fit = (to: number, resample: boolean, label: string) => {
    const b = Math.min(range.max, Math.max(range.min, to));
    void execute(fitCommands(doc, channel, seconds, { sourceTempo: Math.round(tempoFor(doc, seconds, b) * 1000) / 1000, resample }), { label });
  };

  return (
    <div className="flex flex-wrap items-center gap-1.5 text-2xs text-dim">
      <span>Fit to song</span>
      <div className="flex items-center gap-px">
        {(["off", "stretch", "resample"] as const).map((m) => (
          <ToggleChip key={m} on={mode === m} tone={m === "off" ? "dim" : "accent"} label={{ off: "Off", stretch: "Stretch", resample: "Resample" }[m]}
            title={{ off: "Plays at its own speed, as recorded", stretch: "Sped up or slowed down to fill its bars, pitch kept",
              resample: "Sped up or slowed down to fill its bars, the pitch moving with it like a record" }[m]}
            onClick={() => m === "off"
              ? void execute(fitCommands(doc, channel, seconds, { sourceTempo: null, resample: false }), { label: "Don't fit to the song" })
              : fit(bars, m === "resample", m === "resample" ? "Resample to the song" : "Stretch to the song")} />
        ))}
      </div>
      {mode !== "off" && layer.sourceTempo != null && (
        <>
          <button type="button" className="rounded-[3px] bg-control px-1 text-fg disabled:opacity-40" title="Half as many bars"
            disabled={bars / 2 < range.min - 1e-6} onClick={() => fit(bars / 2, !!layer.resample, `Fit to ${formatBars(bars / 2)}`)}>÷2</button>
          <DragNumber value={Math.round(bars * 1000) / 1000} min={range.min} max={range.max} step={0.125} pixelsPerStep={6} format={formatBars} className="text-fg"
            parse={(t) => { const [a, b] = t.split("/").map(Number); return b ? a / b : a; }}
            onCommit={(v) => fit(v, !!layer.resample, `Fit to ${formatBars(v)}`)} />
          <button type="button" className="rounded-[3px] bg-control px-1 text-fg disabled:opacity-40" title="Twice as many bars"
            disabled={bars * 2 > range.max + 1e-6} onClick={() => fit(bars * 2, !!layer.resample, `Fit to ${formatBars(bars * 2)}`)}>×2</button>
          <span className="text-faint">{describeFit(doc, layer.sourceTempo, !!layer.resample)}</span>
        </>
      )}
    </div>
  );
}

function SampleTab({ channel, sampler, saved, color, first, clips }: {
  channel: Channel; sampler: SamplerInstrument; saved: SamplerLayer; color: string; first: boolean; clips: number;
}) {
  const { layer, change } = useLayer(channel, saved);
  const key = layer.rootKey ?? 60;
  const mode = layer.mode ?? "oneShot";
  const loop = layer.loop ?? "off";
  // Touching the window makes the channel the one the keyboards play, which loads a sample from
  // the playlist that plays no notes yet, so the wave plays the channel with all its settings.
  const plays = {
    press: () => {
      notePlayed(true);
      void call("live.noteOn", { channel: channel.id, key, velocity: 110 });
    },
    release: () => {
      notePlayed(false);
      void call("live.noteOff", { channel: channel.id, key });
    },
  };

  const knob = (field: keyof SamplerLayer, props: Omit<React.ComponentProps<typeof Knob>, "onCommit" | "onChange"> & { snap?: (v: number) => number }) => {
    const { snap = (v: number) => Math.round(v * 100) / 100, ...rest } = props;
    return <Knob size={30} color={color} {...rest}
      onChange={(v) => change({ [field]: snap(v) }, false)} onCommit={(v) => change({ [field]: snap(v) }, true)} />;
  };

  return (
    <div className="flex flex-col gap-3">
      <div className="flex items-center gap-1.5">
        <div className="min-w-0 flex-1 truncate text-xs text-fg" title={layer.sample}>{fileName(layer.sample)}</div>
        <IconButton size="sm" title="Remove layer" onClick={() => void execute({ type: "removeSamplerLayer", channel: channel.id, layer: layer.id }, { label: "Remove layer" })}>
          <Trash2 size={11} />
        </IconButton>
      </div>
      <Waveform
        path={layer.sample} start={layer.start ?? 0} end={layer.end ?? undefined} color={color}
        processing={layer.process || layer.punch || layer.tail ? { process: layer.process, punch: layer.punch, tail: layer.tail } : undefined}
        shaping={{ reverse: !!layer.reverse, invert: layer.invert, normalize: layer.normalize, fadeIn: layer.fadeIn, fadeOut: layer.fadeOut, loop, length: layer.length }}
        onRegion={(start, end, commit) => change({ start, end: end ?? (null as never) }, commit)}
        onPress={plays.press}
        onRelease={plays.release}
      />
      <div className="flex flex-wrap items-center gap-1.5">
        <div className="flex items-center gap-px" title="Loop the part that plays while the note is held">
          {(["off", "forward", "pingPong"] as const).map((l) => (
            <ToggleChip key={l} on={loop === l} tone={l === "off" ? "dim" : "accent"} label={{ off: "No loop", forward: "Loop", pingPong: "Ping-pong" }[l]}
              onClick={() => change({ loop: l }, true, l === "off" ? "No loop" : l === "forward" ? "Loop" : "Ping-pong loop")} />
          ))}
        </div>
        <div className="flex-1" />
        <ToggleChip on={layer.pitchTracking !== false} label="Follow keys" title="Pitch follows the key played; off, every key plays it at its own pitch"
          onClick={() => change({ pitchTracking: layer.pitchTracking === false }, true, "Pitch tracking")} />
        <ToggleChip on={mode === "gate"} tone="dim" label={mode === "gate" ? "Gate" : "One-shot"} title="One-shot plays to the end; gate stops on note-off"
          onClick={() => change({ mode: mode === "gate" ? "oneShot" : "gate" }, true, "Layer mode")} />
      </div>
      {(first && (clips > 0 || channel.group === "arrangement")) || (layer.start ?? 0) > 0 || layer.end != null ? (
        <div className="flex flex-wrap items-center gap-1.5">
          {first && (clips > 0 || channel.group === "arrangement") && <FitToSong channel={channel} layer={layer} />}
          {((layer.start ?? 0) > 0 || layer.end != null) && (
            <button type="button" className="text-2xs text-faint hover:text-fg" onClick={() => change({ start: 0, end: null as never }, true, "Play the whole sample")}>Whole sample</button>
          )}
        </div>
      ) : null}
      <div className="flex flex-wrap gap-2">
        <Section title="Level and pitch">
          {knob("gainDb", { label: "Gain", value: layer.gainDb ?? 0, min: -48, max: 24, defaultValue: 0, format: (v) => `${formatDb(v)} dB`, snap: (v) => Math.round(v * 10) / 10 })}
          {knob("pan", { label: "Pan", value: layer.pan ?? 0, min: -1, max: 1, defaultValue: 0, bipolar: true, format: formatPan })}
          {knob("transpose", { label: "Pitch", value: layer.transpose ?? 0, min: -24, max: 24, defaultValue: 0, bipolar: true, format: (v) => `${Math.round(v)} st`, snap: Math.round })}
          {knob("fineTune", { label: "Fine", value: layer.fineTune ?? 0, min: -100, max: 100, defaultValue: 0, bipolar: true, format: (v) => `${Math.round(v)} ct`, snap: Math.round })}
        </Section>
      </div>
      <ProcessPanel layer={layer} color={color} change={change} />
      <div className="flex flex-wrap items-center gap-x-4 gap-y-1.5 text-2xs text-dim">
        <label className="flex items-center gap-1">Root
          <DragNumber value={key} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg" onCommit={(v) => change({ rootKey: v }, true, "Root key")} />
        </label>
        <label className="flex items-center gap-1">Keys
          <DragNumber value={layer.keyRange?.low ?? 0} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg"
            onCommit={(v) => change({ keyRange: { ...layer.keyRange, low: v } }, true, "Key range")} />
          –
          <DragNumber value={layer.keyRange?.high ?? 127} min={0} max={127} pixelsPerStep={5} format={noteName} className="text-fg"
            onCommit={(v) => change({ keyRange: { ...layer.keyRange, high: v } }, true, "Key range")} />
        </label>
        <label className="flex items-center gap-1">Velocity
          <DragNumber value={layer.velocityRange?.low ?? 1} min={1} max={127} className="text-fg" onCommit={(v) => change({ velocityRange: { ...layer.velocityRange, low: v } }, true, "Velocity range")} />
          –
          <DragNumber value={layer.velocityRange?.high ?? 127} min={1} max={127} className="text-fg" onCommit={(v) => change({ velocityRange: { ...layer.velocityRange, high: v } }, true, "Velocity range")} />
        </label>
        <label className="flex items-center gap-1">Voices
          <DragNumber value={sampler.polyphony ?? 32} min={1} max={256} className="text-fg"
            onCommit={(v) => void execute({ type: "updateInstrument", channel: channel.id, changes: { polyphony: v } }, { label: "Sampler voices" })} />
        </label>
      </div>
    </div>
  );
}

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-1.5 rounded-md bg-raised px-2.5 py-2">
      <div className="text-2xs uppercase tracking-wide text-faint">{title}</div>
      <div className="flex items-start gap-3">{children}</div>
    </div>
  );
}

/** Each target's envelope and LFO. The volume envelope is the layer's own, the cutoff envelope
    the filter's; the rest live in the sampler's modulation. */
function EnvelopesTab({ channel, sampler, layer: savedLayer, target, setTarget, clips, tempo, showFilter }: {
  channel: Channel; sampler: SamplerInstrument; layer: SamplerLayer; target: Target; setTarget: (t: Target) => void;
  clips: number; tempo: number; showFilter: () => void;
}) {
  const { layer, change: changeLayer } = useLayer(channel, savedLayer);
  const [modulation, holdModulation] = useHeld<SamplerModulation>(sampler.modulation ?? {}, channel.id);
  const [filter, holdFilter] = useHeld<SamplerFilter>(sampler.filter ?? {}, channel.id);
  const liveModulation = useLiveControl<SamplerModulation>((m) => ({ type: "updateInstrument", channel: channel.id, changes: { modulation: m } }), "Modulation");
  const liveFilter = useLiveControl<SamplerFilter>((f) => ({ type: "updateInstrument", channel: channel.id, changes: { filter: f } }), "Filter");
  const { label, color } = TARGETS.find((t) => t.value === target)!;
  const filterOff = (filter.type ?? "off") === "off";

  const setModulation = (next: SamplerModulation, commit: boolean) => {
    holdModulation(next);
    (commit ? liveModulation.onCommit : liveModulation.onChange)(next);
  };
  const setFilter = (next: SamplerFilter, commit: boolean) => {
    holdFilter(next);
    (commit ? liveFilter.onCommit : liveFilter.onChange)(next);
  };

  // The envelope this target's tab edits, and its amount.
  let envelope: Envelope | undefined;
  let onEnvelope: (env: Envelope, commit: boolean) => void;
  let amount: React.ReactNode = null, dim = false;

  if (target === "volume") {
    envelope = layer.envelope;
    onEnvelope = (env, commit) => changeLayer({ envelope: env }, commit);
  } else if (target === "cutoff") {
    // Switched on here, a filter's envelope starts as a pluck: a quick fall back to the cutoff.
    const env = filter.envelope ?? (filterOff ? { attack: 0, decay: 0.3, sustain: 0, release: 0.2 } : DEFAULT_ENVELOPE);
    envelope = env;
    onEnvelope = (e, commit) => setFilter({ ...filter, envelope: e }, commit);
    dim = filterOff || !filter.envelopeAmount;
    amount = (
      <Knob size={30} label="Amount" title="How far the envelope moves the cutoff at its peak" value={filter.envelopeAmount ?? 0} min={-10} max={10} defaultValue={0} bipolar color={color}
        format={(v) => `${v > 0 ? "+" : ""}${v.toFixed(1)} oct`}
        onChange={(v) => setFilter({ ...filter, envelope: env, envelopeAmount: Math.round(v * 10) / 10 }, false)}
        onCommit={(v) => setFilter({ ...filter, envelope: env, envelopeAmount: Math.round(v * 10) / 10 }, true)} />
    );
  } else {
    const t: ModulationTarget = modulation[target] ?? {};
    const env = t.envelope ?? { attack: 0, decay: 0.3, sustain: 0, release: 0.1 };
    const setTargetValue = (change: Partial<ModulationTarget>, commit: boolean) => setModulation({ ...modulation, [target]: { ...t, envelope: env, ...change } }, commit);
    envelope = env;
    onEnvelope = (e, commit) => setTargetValue({ envelope: e }, commit);
    dim = !t.amount || (target === "resonance" && filterOff);
    amount = (
      <Knob size={30} label="Amount" title="How far the envelope moves it at its peak; 0 is off" value={t.amount ?? 0} min={-1} max={1} defaultValue={0} bipolar color={color}
        format={(v) => formatAmount(target, v)}
        onChange={(v) => setTargetValue({ amount: Math.round(v * 1000) / 1000 }, false)}
        onCommit={(v) => setTargetValue({ amount: Math.round(v * 1000) / 1000 }, true)} />
    );
  }

  const lfo: LfoValues = { ...DEFAULT_LFO, ...modulation[target]?.lfo };
  const setLfo = (next: LfoValues, commit: boolean) => {
    const { sync, ...rest } = next;
    setModulation({ ...modulation, [target]: { ...modulation[target], lfo: sync ? { ...rest, sync } : rest } }, commit);
  };

  const note = target === "volume"
    ? clips > 0 ? "Over each audio clip, the volume envelope's attack starts where the clip starts and its release ends where it ends; the LFO plays on notes only." : undefined
    : (target === "cutoff" || target === "resonance") && filterOff ? "The filter is off: switch it on to hear this."
    : clips > 0 && target !== "cutoff" ? "Notes only: audio clips don't follow this." : undefined;

  return (
    <div className="flex flex-col gap-3">
      <div className="flex items-center gap-1">
        {TARGETS.map((t) => (
          <button key={t.value} type="button" onClick={() => setTarget(t.value)}
            className={cn("relative h-7 rounded-[4px] px-3 text-xs font-medium transition-colors", target === t.value ? "bg-control" : "text-dim hover:text-fg")}
            style={target === t.value ? { color: t.color } : undefined}>
            {t.label}
            {isActive(t.value, layer, filter, modulation) && <span className="absolute top-1 right-1 h-1 w-1 rounded-full" style={{ background: t.color }} />}
          </button>
        ))}
        <div className="flex-1" />
        {(target === "cutoff" || target === "resonance") && (
          <button type="button" className="text-2xs text-faint hover:text-fg" onClick={showFilter}>Filter: {filterLabel(filter.type)} →</button>
        )}
      </div>
      <div className="grid grid-cols-[1fr_216px] gap-3">
        <div className="flex flex-col gap-1.5 rounded-md bg-raised p-2.5">
          <div className="text-2xs uppercase tracking-wide text-faint">Envelope</div>
          <EnvelopeGraph key={`${target}:${savedLayer.id}`} envelope={envelope} color={color} label={label} dim={dim} extra={amount}
            onChange={(env) => onEnvelope(env, false)} onCommit={(env) => onEnvelope(env, true)} />
        </div>
        <div className="rounded-md bg-raised p-2.5">
          <LfoPanel lfo={lfo} color={color} tempo={tempo} formatAmount={(v) => formatLfoAmount(target, v)}
            onChange={(l) => setLfo(l, false)} onCommit={(l) => setLfo(l, true)} />
        </div>
      </div>
      {note && <div className="text-2xs text-faint">{note}</div>}
      {target === "volume" && (layer.mode ?? "oneShot") === "oneShot" && (layer.loop ?? "off") === "off" && channel.group !== "arrangement" && (
        <div className="text-2xs text-faint">One-shot layers play to their end and ignore note-off, so the release only applies in gate mode or while looping.</div>
      )}
    </div>
  );
}

function isActive(target: Target, layer: SamplerLayer, filter: SamplerFilter, m: SamplerModulation) {
  const lfoOn = !!m[target]?.lfo?.amount;
  if (target === "volume") return lfoOn || JSON.stringify({ ...DEFAULT_ENVELOPE, ...layer.envelope }) !== JSON.stringify(DEFAULT_ENVELOPE);
  if (target === "cutoff") return lfoOn || ((filter.type ?? "off") !== "off" && !!filter.envelopeAmount);
  return lfoOn || !!m[target]?.amount;
}

function filterLabel(type: SamplerFilter["type"]) {
  return { off: "off", lowpass: "low-pass", lowpass24: "low-pass 24", highpass: "high-pass", highpass24: "high-pass 24", bandpass: "band-pass", notch: "notch" }[type ?? "off"];
}

/** An amount in the target's units: shares of 5 octaves of cutoff, all of the resonance, centre
    to one side of pan, 24 semitones of pitch (as the engine scales them). */
function formatAmount(target: Target, v: number) {
  if (Math.abs(v) < 0.0005) return "Off";
  const sign = v > 0 ? "+" : "−", a = Math.abs(v);
  switch (target) {
    case "pitch": return `${sign}${(a * 24).toFixed(a * 24 < 10 ? 2 : 1)} st`;
    case "pan": return `${Math.round(a * 100)}% ${v > 0 ? "R" : "L"}`;
    case "cutoff": return `${sign}${(a * 5).toFixed(1)} oct`;
    default: return `${sign}${Math.round(a * 100)}%`;
  }
}

function formatLfoAmount(target: Target, v: number) {
  if (Math.abs(v) < 0.0005) return "Off";
  const a = Math.abs(v), flipped = v < 0 ? " ⇅" : "";
  switch (target) {
    case "volume": return `${Math.round(a * 100)}% deep${flipped}`;
    case "pitch": return `±${(a * 24).toFixed(a * 24 < 10 ? 2 : 1)} st${flipped}`;
    case "cutoff": return `±${(a * 5).toFixed(1)} oct${flipped}`;
    default: return `±${Math.round(a * 100)}%${flipped}`;
  }
}

/** A sampler's filter: its type, response and knobs. Its envelope and LFO are with the others. */
export function FilterTab({ channel, filter: saved, color, showEnvelope }: { channel: Channel; filter: SamplerFilter | undefined; color: string; showEnvelope: () => void }) {
  const [held, hold] = useHeld<SamplerFilter>(saved ?? {}, channel.id);
  const live = useLiveControl<SamplerFilter>((f) => ({ type: "updateInstrument", channel: channel.id, changes: { filter: f } }), "Filter");
  // A filter switched on here starts with a pluck: a quick fall back to the cutoff. One set up
  // elsewhere without an envelope has the engine's default.
  const envelope: Envelope = held.envelope ?? ((held.type ?? "off") === "off" ? { attack: 0, decay: 0.3, sustain: 0, release: 0.2 } : DEFAULT_ENVELOPE);
  const set = (change: Partial<SamplerFilter>, commit: boolean) => {
    const next = { ...held, envelope, ...change };
    hold(next);
    (commit ? live.onCommit : live.onChange)(next);
  };

  return (
    <div className="flex flex-col gap-3">
      <FilterGraph filter={held} color={color} channel={channel.id} onChange={(c) => set(c, false)} onCommit={(c) => set(c, true)} />
      <div className="flex items-center gap-3 text-2xs text-faint">
        <span>Drag the point: sideways for the cutoff, up and down for the resonance.</span>
        <div className="flex-1" />
        <button type="button" className="hover:text-fg" onClick={showEnvelope}>Cutoff envelope and LFO →</button>
      </div>
    </div>
  );
}
