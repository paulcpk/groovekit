import type { Channel, Project, SamplerInstrument, StackInstrument, StackLayer } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { AlignHorizontalJustifyStart, Crown, Plus, SlidersHorizontal, Trash2 } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { DragNumber, IconButton, Knob, ToggleChip } from "@/components/controls";
import { FloatingWindow } from "@/components/window";
import { droppedSample, isSampleDrag, type DraggedSample } from "@/features/playlist/geometry";
import { ChannelBar } from "@/features/rack/ChannelBar";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { cn } from "@/lib/cn";
import { useHeld } from "@/lib/held";
import { useLiveControl } from "@/lib/liveControl";
import { colorFor, fileName, formatDb, palette, stackMaster, ticksPerBar, uniqueId } from "@/lib/model";
import { drawPeaks, usePeaks } from "@/lib/peaks";
import { call, engine, execute, toast, useEngine } from "@/state/engine";
import { setUi } from "@/state/ui";
import { openWindow, type OpenWindow } from "@/state/windows";

/** What the engine says of the layers: each matched to the loudest (dB), a loop's length, and the
    trim that keeps the sum no louder than the loudest alone. */
interface StackInfo {
  trimDb: number;
  layers: Record<string, { loudnessDb?: number; matchDb?: number; gainDb?: number; bars?: number; seconds?: number; rate?: number }>;
}

/** What the library knows of a dropped sample: whether it loops, and at what tempo. */
interface Described { loop?: boolean; sourceTempo?: number; tempo?: number; category?: string }

const SPEEDS = [0.5, 1, 2];

/** The multi-sampler: several samplers as one instrument. Hits play from the channel's notes,
    loops from one audio clip; every layer has its share of the output at equal loudness, and
    opens its own sampler window for everything else. */
export function StackWindow({ window: w, channel, stack }: { window: OpenWindow; channel: Channel; stack: StackInstrument }) {
  const doc = useEngine((s) => s.doc)!;
  const color = colorFor(channel.id, channel.color);
  const [over, setOver] = useState(false);
  const [info, setInfo] = useState<StackInfo>();
  const loops = stack.mode === "loops";
  const anySolo = stack.layers.some((l) => l.solo);
  const signature = JSON.stringify(stack) + doc.tempo;

  useEffect(() => {
    let live = true;
    const timer = setTimeout(() => void engine.call<StackInfo>("stack.info", { channel: channel.id }).then((r) => live && setInfo(r), () => {}), 150);
    return () => {
      live = false;
      clearTimeout(timer);
    };
  }, [channel.id, signature]);

  const [drive, holdDrive] = useHeld(stack.drive ?? 0, channel.id);
  const liveDrive = useLiveControl<number>((v) => ({ type: "updateInstrument", channel: channel.id, changes: { drive: v } }), "Drive");

  const add = (samples: DraggedSample[]) => void addLayers(channel, stack, samples);

  const setMode = (mode: "hits" | "loops") => {
    if (mode === stack.mode) return;
    void execute([
      { type: "updateInstrument", channel: channel.id, changes: { mode } },
      { type: "updateChannel", channel: channel.id, changes: { group: mode === "loops" ? "arrangement" : (null as never) } },
    ], { label: mode === "loops" ? "Multi-sampler of loops" : "Multi-sampler of hits" });
  };

  const master = stackMaster(stack);
  const masterBars = master ? info?.layers[master.id]?.bars : undefined;

  return (
    <FloatingWindow window={w} name={channel.name || channel.id} width={Math.min(980, Math.max(600, 34 + Math.max(4, stack.layers.length + 1) * 142))} accent={color}
      title={<><span className="h-3 w-1 shrink-0 rounded-sm" style={{ background: color }} /><span className="truncate">{channel.name || channel.id}</span><span className="text-2xs font-normal text-faint">Multi-sampler</span></>}>
      <div
        className={cn("flex flex-col gap-3 p-3", over && "bg-accent/5")}
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
            void importDroppedFiles(e).then((samples) => samples.length && add(samples));
            return;
          }
          const sample = droppedSample(e);
          if (!sample) return;
          e.preventDefault();
          add([sample]);
        }}
      >
        <ChannelBar doc={doc} channel={channel} />

        <div className="flex flex-wrap items-center gap-3">
          <div className="flex items-center gap-px" title="Hits play from the channel's notes (layered kicks, claps, one-shots); loops from one clip in the arrangement">
            <ToggleChip on={!loops} label="Hits" onClick={() => setMode("hits")} />
            <ToggleChip on={loops} label="Loops" onClick={() => setMode("loops")} />
          </div>
          {!loops && stack.layers.length > 1 && (
            <button type="button" className="flex items-center gap-1 rounded-[3px] bg-control px-1.5 py-0.5 text-2xs text-fg hover:bg-hover"
              title="Lines every layer's attack up with the latest one's, so the hits land together instead of smearing or cancelling"
              onClick={() => void call("stack.align", { channel: channel.id })}>
              <AlignHorizontalJustifyStart size={11} /> Line up the hits
            </button>
          )}
          {loops && master && (
            <span className="text-2xs text-dim" title="The master sets how long one time round of the multi-sampler lasts: a clip's natural length">
              Master: {master.name || fileName(master.sampler.layers[0]?.sample ?? master.id)}{masterBars ? `, ${formatBars(masterBars)}` : ""}
            </span>
          )}
          <div className="flex-1" />
          {info && info.trimDb < -0.05 && (
            <span className="text-2xs text-faint tabular" title="Every layer at its mix is matched in loudness, then the sum is brought down so it's never louder than the loudest layer alone">
              Guard {formatDb(info.trimDb)} dB
            </span>
          )}
          <Knob size={30} label="Drive" color={color} value={drive} min={0} max={1} defaultValue={0}
            title="Saturates the layers together, denser rather than louder"
            format={(v) => (v < 0.005 ? "Clean" : `${Math.round(v * 100)}%`)}
            onChange={(v) => { holdDrive(v); liveDrive.onChange(Math.round(v * 100) / 100); }}
            onCommit={(v) => { holdDrive(v); liveDrive.onCommit(Math.round(v * 100) / 100); }} />
        </div>

        <div className="flex flex-wrap gap-2">
          {stack.layers.map((layer, i) => (
            <LayerStrip key={layer.id} channel={channel} stack={stack} layer={layer} index={i} info={info?.layers[layer.id]} dimmed={anySolo && !layer.solo} />
          ))}
          <div className={cn("flex w-[134px] flex-col items-center justify-center gap-1 rounded-md border border-dashed border-line p-3 text-center text-2xs text-faint",
            stack.layers.length === 0 && "w-full py-8")}>
            <Plus size={14} />
            {stack.layers.length === 0
              ? "Drop samples here: one-shots make it a multi-sampler of hits, loops one of loops"
              : `Drop ${loops ? "loops" : "samples"} to add layers`}
          </div>
        </div>

        <div className="flex flex-wrap items-center gap-3 text-2xs text-faint">
          <span className="min-w-0 flex-1">
            {loops
              ? "Loops play at their own tempo (×½ and ×2 for twice as long or as short) and keep going round over the clip; one without a tempo lasts as long as the master. Every layer at 100% is as loud as the others."
              : "The channel's notes play every layer. Every layer at 100% is as loud as the others, and all together never louder than the loudest alone."}
          </span>
          {loops && master && (
            <button type="button" className="rounded-[3px] bg-control px-1.5 py-0.5 text-fg hover:bg-hover"
              onClick={() => void putInArrangement(doc, channel, info?.layers[master.id]?.seconds)}>Put in the arrangement</button>
          )}
        </div>
      </div>
    </FloatingWindow>
  );
}

function formatBars(bars: number) {
  const rounded = Math.round(bars * 100) / 100;
  return `${rounded} bar${rounded === 1 ? "" : "s"}`;
}

/** One layer: its sound, its share of the output, how it lines up or loops, and its window. */
function LayerStrip({ channel, stack, layer, index, info, dimmed }: {
  channel: Channel; stack: StackInstrument; layer: StackLayer; index: number; info?: StackInfo["layers"][string]; dimmed: boolean;
}) {
  const sample = layer.sampler.layers[0];
  const color = palette[index % palette.length];
  const loops = stack.mode === "loops";
  const name = layer.name || (sample ? fileName(sample.sample) : layer.id);
  const [mix, holdMix] = useHeld(layer.mix ?? 1, `${channel.id}/${layer.id}`);
  const liveMix = useLiveControl<number>((v) => ({ type: "updateStackLayer", channel: channel.id, layer: layer.id, changes: { mix: v } }), "Layer mix");
  const update = (changes: Partial<StackLayer>, label: string) => void execute({ type: "updateStackLayer", channel: channel.id, layer: layer.id, changes }, { label });
  const isMaster = stackMaster(stack)?.id === layer.id;
  const open = () => openWindow({ kind: "channel", channel: channel.id, layer: layer.id });

  return (
    <div className={cn("flex w-[134px] flex-col gap-1.5 rounded-md bg-raised p-2", dimmed && "opacity-50")}>
      <div className="flex items-center gap-1">
        <span className="h-2 w-2 shrink-0 rounded-full" style={{ background: color }} />
        <button type="button" className="min-w-0 flex-1 truncate text-left text-2xs text-fg hover:underline" title={`${sample?.sample ?? ""}\nOpen its sampler: filter, envelopes, LFOs, pitch`} onClick={open}>
          {name}
        </button>
        {loops && (
          <button type="button" title={isMaster ? "The master: the others are fitted to it" : "Make it the master"}
            className={cn("shrink-0", isMaster ? "text-accent" : "text-faint hover:text-fg")}
            onClick={() => !isMaster && update({ master: true }, `${name} is the master`)}>
            <Crown size={11} />
          </button>
        )}
      </div>

      <MiniWave path={sample?.sample} start={sample?.start ?? 0} end={sample?.end ?? undefined} color={color}
        onClick={() => sample && void call("preview.play", { path: sample.sample })} />

      <div className="flex items-end justify-between">
        <Knob size={36} label="Mix" color={color} value={mix} min={0} max={1} defaultValue={1}
          automation={{ scope: "channel", channel: channel.id, param: `mix:${layer.id}` }} automationValue={mix}
          title={info?.matchDb ? `Matched in loudness: ${formatDb(info.matchDb)} dB` : "Its share of the output"}
          format={(v) => `${Math.round(v * 100)}%`}
          onChange={(v) => { holdMix(v); liveMix.onChange(Math.round(v * 100) / 100); }}
          onCommit={(v) => { holdMix(v); liveMix.onCommit(Math.round(v * 100) / 100); }} />
        <div className="flex flex-col items-end gap-1">
          <div className="flex gap-px">
            <ToggleChip on={!!layer.mute} tone="dim" label="M" title="Mute this layer" onClick={() => update({ mute: !layer.mute }, layer.mute ? "Unmute layer" : "Mute layer")} />
            <ToggleChip on={!!layer.solo} label="S" title="Only this layer" onClick={() => update({ solo: !layer.solo }, layer.solo ? "Unsolo layer" : "Solo layer")} />
          </div>
          {info?.matchDb ? <span className="text-[9px] text-faint tabular" title="Brought up to the loudest layer's loudness">{formatDb(info.matchDb)} dB</span> : null}
        </div>
      </div>

      {loops ? (
        <div className="flex flex-col gap-1">
          <div className="flex items-center gap-px" title="Speed: ×½ plays it at half speed (twice as long), ×2 at double speed (half as long)">
            {SPEEDS.map((s) => (
              <ToggleChip key={s} on={(layer.speed ?? 1) === s} tone={s === 1 ? "dim" : "accent"} label={s === 0.5 ? "×½" : `×${s}`}
                onClick={() => update({ speed: s }, `${name} at ${s === 0.5 ? "half" : s === 2 ? "double" : "its own"} speed`)} />
            ))}
          </div>
          <span className="text-[10px] text-dim tabular" title={sample?.sourceTempo ? `At its tempo (${sample.sourceTempo} BPM), fitted to the song's` : "No tempo of its own: lasts as long as the master"}>
            {info?.bars ? formatBars(info.bars) : "…"}{sample?.sourceTempo ? "" : " · fits the master"}
          </span>
        </div>
      ) : (
        <label className="flex items-center gap-1 text-[10px] text-dim" title="Plays this much later, to line its attack up with the others'">
          Offset
          <DragNumber value={layer.offsetMs ?? 0} min={-250} max={250} step={0.1} pixelsPerStep={2} defaultValue={0} className="text-fg"
            format={(v) => `${v > 0 ? "+" : ""}${v.toFixed(1)} ms`} onCommit={(v) => update({ offsetMs: Math.round(v * 10) / 10 }, "Layer offset")} />
        </label>
      )}

      <div className="flex items-center justify-between">
        <button type="button" className="flex items-center gap-1 text-2xs text-dim hover:text-fg" onClick={open}><SlidersHorizontal size={11} /> Sampler</button>
        <IconButton size="sm" title="Remove this layer" onClick={() => void execute({ type: "removeStackLayer", channel: channel.id, layer: layer.id }, { label: `Remove ${name}` })}>
          <Trash2 size={11} />
        </IconButton>
      </div>
    </div>
  );
}

function MiniWave({ path, start, end, color, onClick }: { path?: string; start: number; end?: number; color: string; onClick: () => void }) {
  const peaks = usePeaks(path);
  const canvas = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    if (peaks && canvas.current) drawPeaks(canvas.current, peaks, start, end ?? peaks.durationSeconds, color);
  }, [peaks, start, end, color]);

  return (
    <button type="button" title="Play it" onClick={onClick} className="h-8 w-full overflow-hidden rounded-[3px] bg-[#232328] hover:bg-[#2a2a30]">
      <canvas ref={canvas} width={118} height={32} style={{ width: "100%", height: 32 }} />
    </button>
  );
}

/** Adds samples as layers; the first ones decide what the multi-sampler plays (loops or hits). */
export async function addLayers(channel: Channel, stack: StackInstrument, samples: DraggedSample[]) {
  if (!samples.length) return;
  const described = await Promise.all(samples.map((s) => engine.call<Described>("library.describe", { path: s.path }).catch(() => ({} as Described))));
  const isLoop = (i: number) => samples[i].loop ?? described[i].loop ?? false;
  const loops = stack.layers.length === 0 ? described.some((_, i) => isLoop(i)) : stack.mode === "loops";
  const ids = stack.layers.map((l) => ({ id: l.id }));
  const commands: Command[] = [];

  if (stack.layers.length === 0 && (loops ? "loops" : "hits") !== stack.mode) {
    commands.push({ type: "updateInstrument", channel: channel.id, changes: { mode: loops ? "loops" : "hits" } });
    commands.push({ type: "updateChannel", channel: channel.id, changes: { group: loops ? "arrangement" : (null as never) } });
  }

  samples.forEach((s, i) => {
    const id = uniqueId("l1", ids);
    ids.push({ id });
    const tempo = described[i].sourceTempo ?? (isLoop(i) ? s.tempo : undefined);
    const sampler: SamplerInstrument = {
      type: "sampler",
      layers: [{ id: "s", sample: s.path, ...(loops && tempo ? { sourceTempo: tempo } : {}) }],
    };
    commands.push({ type: "addStackLayer", channel: channel.id, layer: { id, sampler, ...(loops && stack.layers.length === 0 && i === 0 ? { master: true } : {}) } });
  });

  await execute(commands, { label: samples.length === 1 ? `Add ${fileName(samples[0].path)} to ${channel.name || channel.id}` : `Add ${samples.length} layers` });
  if (loops && described.some((d, i) => !d.sourceTempo && isLoop(i))) toast("A loop without a tempo lasts as long as the master");
}

/** A clip of the multi-sampler on a row of its own, from the bar the playhead is in, as long as the master. */
async function putInArrangement(doc: Project, channel: Channel, seconds: number | undefined) {
  const bar = ticksPerBar(doc);
  const position = useEngine.getState().transport.position ?? 0;
  const start = Math.floor(position / bar) * bar;
  const length = seconds ? Math.max(bar / 4, Math.round((seconds * doc.tempo) / 60) * 960) : bar * 4;
  const row = doc.playlist.clips.reduce((r, c) => Math.max(r, c.row + 1), 0);
  const id = uniqueId(channel.id, doc.playlist.clips);
  await execute({ type: "addClip", clip: { type: "audio", id, row, start, length, channel: channel.id } }, { label: `Put ${channel.name || channel.id} in the arrangement` });
}
