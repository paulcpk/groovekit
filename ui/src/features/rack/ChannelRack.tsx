import type { Channel, Note, Pattern, Project } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { AudioLines, ChevronDown, Copy, Plus, Trash2, Volume2 } from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import { Button, Empty, IconButton, Knob, ToggleChip } from "@/components/controls";
import { ActivityLed } from "@/components/fader";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger, Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { droppedSample, isSampleDrag } from "@/features/playlist/geometry";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { newPattern } from "@/features/playlist/Playlist";
import { cn } from "@/lib/cn";
import { useLiveControl } from "@/lib/liveControl";
import { useLivePosition } from "@/lib/playhead";
import { colorFor, formatDb, formatPan, palette, patternLength, STEP, ticksPerBar, uniqueId } from "@/lib/model";
import { drawPeaks, usePeaks } from "@/lib/peaks";
import { Automatable } from "@/features/playlist/Automatable";
import { call, execute, useEngine } from "@/state/engine";
import { setUi, useUi, type RackView } from "@/state/ui";
import { openChannel } from "./channels";
import { addEmptySampler, addSamplerChannel, replaceSample, routeToNewInsert, stepKey } from "./channels";
import { InsertItems } from "./ChannelBar";
import { PluginPicker } from "./PluginPicker";

interface Paint {
  channel: string;
  on: boolean;
  steps: Set<number>;
  /** Released and sent: still drawn until the engine's change arrives, so steps don't flick back. */
  committed?: boolean;
}

export function ChannelRack() {
  const doc = useEngine((s) => s.doc)!;
  const { selectedPattern, selectedChannel, rackView } = useUi();
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);
  const channels = doc.channels.filter((c) => rackView === "all" || (c.group ?? "rack") === rackView);
  const [paint, setPaint] = useState<Paint | null>(null);
  const paintRef = useRef<Paint | null>(null);
  paintRef.current = paint;

  // Painting ends wherever the pointer is released.
  useEffect(() => {
    const up = () => {
      const p = paintRef.current;
      if (!p || p.committed || !pattern) return;
      const committed = { ...p, committed: true };
      setPaint(committed);
      // The engine sends the change before it answers, so the document has it by now.
      void commitPaint(doc, pattern, p).finally(() => setPaint((current) => (current === committed ? null : current)));
    };
    window.addEventListener("pointerup", up);
    return () => window.removeEventListener("pointerup", up);
  }, [doc, pattern]);

  const steps = pattern ? Math.round(patternLength(pattern, doc) / STEP) : 16;
  // The step playing now, when this pattern plays in pattern mode.
  const [playingStep, setPlayingStep] = useState(-1);
  useLivePosition(({ ticks, playing }) => {
    const t = useEngine.getState().transport;
    setPlayingStep(playing && t.mode === "pattern" && t.pattern === pattern?.id ? Math.floor(ticks / STEP) : -1);
  }, [pattern?.id]);

  return (
    <div className="flex h-full">
      <div
        className="flex min-w-0 flex-1 flex-col"
        onDragOver={(e) => (isSampleDrag(e) || isFileDrag(e)) && e.preventDefault()}
        onDrop={(e) => {
          if (isFileDrag(e)) {
            e.preventDefault();
            void importDroppedFiles(e).then(async (samples) => {
              for (const sample of samples) await addSamplerChannel(useEngine.getState().doc ?? doc, sample);
            });
            return;
          }
          const sample = droppedSample(e);
          if (!sample) return;
          e.preventDefault();
          void addSamplerChannel(doc, sample);
        }}
      >
        <RackToolbar doc={doc} pattern={pattern} />
        <div className="min-h-0 flex-1 overflow-auto py-1">
          {channels.length === 0 ? (
            <Empty>
              {rackView === "arrangement"
                ? "Samples dropped onto the playlist are listed here, each with its own sampler."
                : "Drop samples from the browser here, or add a channel with +."}
            </Empty>
          ) : (
            channels.map((channel) => (
              <ChannelRow
                key={channel.id}
                doc={doc} channel={channel} pattern={pattern} steps={steps} playingStep={playingStep}
                selected={channel.id === selectedChannel}
                paint={paint?.channel === channel.id ? paint : null}
                onPaintStart={(step, on) => setPaint({ channel: channel.id, on, steps: new Set([step]) })}
                onPaintEnter={(step) => setPaint((p) => (p && !p.committed && p.channel === channel.id && !p.steps.has(step) ? { ...p, steps: new Set([...p.steps, step]) } : p))}
              />
            ))
          )}
        </div>
      </div>
    </div>
  );
}

async function commitPaint(doc: Project, pattern: Pattern, paint: Paint) {
  const channel = doc.channels.find((c) => c.id === paint.channel);
  const key = stepKey(channel);
  const has = (step: number) => pattern.notes.some((n) => n.channel === paint.channel && n.start >= step * STEP && n.start < (step + 1) * STEP);
  const steps = [...paint.steps].filter((s) => has(s) !== paint.on);
  if (steps.length === 0) return;

  if (paint.on) {
    const notes: Note[] = steps.map((s) => ({ channel: paint.channel, start: s * STEP, length: STEP, key, velocity: 100 }));
    await execute({ type: "addNotes", pattern: pattern.id, notes }, { label: steps.length === 1 ? "Add step" : `Add ${steps.length} steps` });
  } else {
    await execute(steps.map((s) => ({ type: "removeNotes" as const, pattern: pattern.id, channel: paint.channel, from: s * STEP, to: (s + 1) * STEP })),
      { label: steps.length === 1 ? "Remove step" : `Remove ${steps.length} steps` });
  }
}

//==============================================================================
/** A step drawn as a pad: lit from above, with a darker lip below and a thin edge, in the
    channel's colour when on (softer at lower velocities) and in greys when off. */
function stepStyle(on: boolean, color: string, velocity: number | undefined, beatGroup: boolean, playing: boolean): React.CSSProperties {
  const off = beatGroup ? "#2e2e35" : "#25252b";

  if (!on) {
    return {
      background: `linear-gradient(to bottom, color-mix(in srgb, ${off} 82%, white), ${off} 55%, color-mix(in srgb, ${off} 85%, black))`,
      border: "1px solid rgb(0 0 0 / 0.45)",
      boxShadow: `inset 0 1px 0 rgb(255 255 255 / 0.07), inset 0 -2px 0 rgb(0 0 0 / 0.28), 0 1px 1px rgb(0 0 0 / 0.35)${playing ? ", 0 0 0 1px rgb(255 255 255 / 0.55)" : ""}`,
    };
  }

  // Quieter steps lean toward the off colour rather than fading out.
  const strength = Math.round(45 + ((velocity ?? 100) / 127) * 55);
  const base = `color-mix(in srgb, ${color} ${strength}%, ${off})`;
  return {
    background: `linear-gradient(to bottom, color-mix(in srgb, ${base} 72%, white), ${base} 50%, color-mix(in srgb, ${base} 82%, black))`,
    border: `1px solid color-mix(in srgb, ${base} 55%, black)`,
    boxShadow: `inset 0 1px 0 rgb(255 255 255 / 0.38), inset 0 -2px 0 rgb(0 0 0 / 0.22), 0 1px 2px rgb(0 0 0 / 0.45), 0 0 8px -2px color-mix(in srgb, ${color} 55%, transparent)${playing ? ", 0 0 0 1px rgb(255 255 255 / 0.7)" : ""}`,
  };
}

/** Delays every second 16th, for the whole song. */
function SwingKnob({ swing }: { swing: number }) {
  const control = useLiveControl((v) => ({ type: "setSwing", swing: Math.round(v) / 100 }), "Swing");
  return (
    <div className="flex items-center gap-1.5 text-2xs text-dim" title="Swing: delays every second 16th note (double-click resets)">
      <Knob value={swing * 100} min={0} max={100} defaultValue={0} size={22} format={(v) => `${Math.round(v)}%`}
        onChange={control.onChange} onCommit={control.onCommit} />
      <span>Swing {Math.round(swing * 100)}%</span>
    </div>
  );
}

function RackToolbar({ doc, pattern }: { doc: Project; pattern: Pattern | undefined }) {
  const [renaming, setRenaming] = useState(false);
  const bar = ticksPerBar(doc);
  const bars = pattern ? patternLength(pattern, doc) / bar : 1;

  return (
    <div className="flex h-8 shrink-0 items-center gap-2 border-b border-line px-2">
      <Dropdown>
        <DropdownTrigger asChild>
          <Button size="sm">
            <span className="h-2.5 w-2.5 rounded-[2px]" style={{ background: pattern ? colorFor(pattern.id, pattern.color) : "#444" }} />
            <span className="max-w-[160px] truncate text-fg">{pattern ? pattern.name ?? pattern.id : "No pattern"}</span>
            <ChevronDown size={11} />
          </Button>
        </DropdownTrigger>
        <DropdownContent>
          {doc.patterns.map((p) => (
            <DropdownItem key={p.id} checked={p.id === pattern?.id} onSelect={() => setUi({ selectedPattern: p.id })}>{p.name ?? p.id}</DropdownItem>
          ))}
          {doc.patterns.length > 0 && <DropdownSeparator />}
          <DropdownItem onSelect={() => void newPattern(doc)}>New pattern</DropdownItem>
        </DropdownContent>
      </Dropdown>

      {pattern && (
        <>
          {renaming ? (
            <input
              autoFocus defaultValue={pattern.name ?? pattern.id}
              className="h-6 w-36 rounded-[3px] bg-bg px-1.5 text-sm outline-none ring-1 ring-accent"
              onBlur={(e) => {
                setRenaming(false);
                const v = e.target.value.trim();
                if (v && v !== pattern.name) void execute({ type: "updatePattern", pattern: pattern.id, changes: { name: v } }, { label: "Rename pattern" });
              }}
              onKeyDown={(e) => {
                if (e.key === "Enter") (e.target as HTMLInputElement).blur();
                if (e.key === "Escape") setRenaming(false);
                e.stopPropagation();
              }}
            />
          ) : (
            <Button size="sm" onClick={() => setRenaming(true)}>Rename</Button>
          )}
          <Dropdown>
            <DropdownTrigger asChild>
              <Button size="sm" title="Pattern length">{bars} bar{bars === 1 ? "" : "s"} <ChevronDown size={11} /></Button>
            </DropdownTrigger>
            <DropdownContent>
              {[1, 2, 4, 8, 16].map((n) => (
                <DropdownItem key={n} checked={n === bars} onSelect={() => void execute({ type: "updatePattern", pattern: pattern.id, changes: { length: n * bar } }, { label: `Pattern length ${n} bars` })}>
                  {n} bar{n === 1 ? "" : "s"}
                </DropdownItem>
              ))}
            </DropdownContent>
          </Dropdown>
          <IconButton size="sm" title="Clone pattern" onClick={() => {
            const id = uniqueId(pattern.id, doc.patterns);
            void execute({ type: "addPattern", pattern: { ...pattern, id, name: `${pattern.name ?? pattern.id} (copy)` } }, { label: "Clone pattern" })
              .then(() => setUi({ selectedPattern: id }));
          }}><Copy size={12} /></IconButton>
          <IconButton size="sm" title="Delete pattern (and its clips)" onClick={() => void execute({ type: "removePattern", pattern: pattern.id }, { label: `Delete ${pattern.name ?? pattern.id}` })}>
            <Trash2 size={12} />
          </IconButton>
        </>
      )}
      <div className="flex-1" />
      <RackViewPicker doc={doc} />
      <SwingKnob swing={doc.swing ?? 0} />
      <Dropdown>
        <DropdownTrigger asChild>
          <Button size="sm"><Plus size={12} /> Channel</Button>
        </DropdownTrigger>
        <DropdownContent align="end">
          <DropdownLabel>Or drop a sample from the browser</DropdownLabel>
          <DropdownItem onSelect={() => void addEmptySampler(doc)}>Sampler</DropdownItem>
          <PluginPicker doc={doc} />
        </DropdownContent>
      </Dropdown>
    </div>
  );
}

/** Which channels the rack lists: the ones patterns play, the samples dropped onto the playlist,
    or both. */
function RackViewPicker({ doc }: { doc: Project }) {
  const view = useUi((s) => s.rackView);
  const count = (v: RackView) => doc.channels.filter((c) => v === "all" || (c.group ?? "rack") === v).length;
  const names: Record<RackView, string> = { rack: "Rack", arrangement: "Arrangement", all: "All channels" };

  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <Button size="sm" title="Which channels to list">{names[view]} <ChevronDown size={11} /></Button>
      </DropdownTrigger>
      <DropdownContent align="end">
        <DropdownItem checked={view === "rack"} onSelect={() => setUi({ rackView: "rack" })}>Rack · played by patterns ({count("rack")})</DropdownItem>
        <DropdownItem checked={view === "arrangement"} onSelect={() => setUi({ rackView: "arrangement" })}>Arrangement · dropped on the playlist ({count("arrangement")})</DropdownItem>
        <DropdownItem checked={view === "all"} onSelect={() => setUi({ rackView: "all" })}>All channels ({count("all")})</DropdownItem>
      </DropdownContent>
    </Dropdown>
  );
}

//==============================================================================
function ChannelRow({ doc, channel, pattern, steps, playingStep, selected, paint, onPaintStart, onPaintEnter }: {
  doc: Project; channel: Channel; pattern: Pattern | undefined; steps: number; playingStep: number; selected: boolean;
  paint: Paint | null; onPaintStart: (step: number, on: boolean) => void; onPaintEnter: (step: number) => void;
}) {
  const level = useEngine((s) => s.levels.channels[channel.id] ?? 0);
  const touched = useEngine((s) => s.touchedAt[channel.id]);
  const color = colorFor(channel.id, channel.color);
  const notes = useMemo(() => pattern?.notes.filter((n) => n.channel === channel.id) ?? [], [pattern, channel.id]);
  const melodic = new Set(notes.map((n) => n.key ?? 60)).size > 1 || notes.some((n) => n.length > STEP * 1.5);
  const insertIndex = doc.mixer.inserts.findIndex((i) => i.id === (channel.insert ?? "master"));
  const arrangement = channel.group === "arrangement";
  const update = (changes: Record<string, unknown>, label: string) => void execute({ type: "updateChannel", channel: channel.id, changes }, { label });
  const volume = useLiveControl((v) => ({ type: "updateChannel", channel: channel.id, changes: { volumeDb: Math.round(v * 10) / 10 } }), "Channel volume");
  const pan = useLiveControl((v) => ({ type: "updateChannel", channel: channel.id, changes: { pan: Math.round(v * 100) / 100 } }), "Pan channel");

  const previewPath = channel.instrument.type === "sampler" ? channel.instrument.layers[0]?.sample : undefined;
  const preview = () => previewPath && void call("preview.play", { path: previewPath });

  return (
    <div className={cn("relative flex h-[30px] items-center gap-2 px-2", selected && "bg-raised")}>
      <ActivityLed level={level} />
      <Automatable target={{ scope: "channel", channel: channel.id, param: "pan" }} value={channel.pan ?? 0}>
        <Knob size={20} value={channel.pan ?? 0} min={-1} max={1} defaultValue={0} bipolar title="Pan" format={formatPan} onChange={pan.onChange} onCommit={pan.onCommit} />
      </Automatable>
      <Automatable target={{ scope: "channel", channel: channel.id, param: "volume" }} value={channel.volumeDb ?? 0}>
        <Knob size={20} value={channel.volumeDb ?? 0} min={-60} max={12} defaultValue={0} title="Volume" format={(v) => `${formatDb(v)} dB`} onChange={volume.onChange} onCommit={volume.onCommit} />
      </Automatable>

      <Dropdown>
        <DropdownTrigger asChild>
          <button type="button" title="Mixer insert" className="h-[18px] w-7 rounded-[3px] bg-control text-2xs text-dim tabular hover:text-fg">
            {insertIndex <= 0 ? "M" : insertIndex}
          </button>
        </DropdownTrigger>
        <DropdownContent>
          <InsertItems doc={doc} channel={channel} />
        </DropdownContent>
      </Dropdown>

      <ContextMenu>
        <ContextMenuTrigger asChild>
          <button
            type="button"
            className={cn(
              "flex h-[22px] w-[150px] shrink-0 items-center gap-1.5 truncate rounded-[3px] px-2 text-left text-xs",
              channel.mute ? "bg-control text-faint" : "bg-hover text-fg",
              selected && "ring-1 ring-accent",
            )}
            title={channel.instrument.type === "plugin" ? "Open the plugin's window" : "Open the sampler"}
            onClick={() => openChannel(channel, true)}
            onDragOver={(e) => channel.instrument.type !== "plugin" && (isSampleDrag(e) || isFileDrag(e)) && (e.preventDefault(), e.stopPropagation())}
            onDrop={(e) => {
              if (channel.instrument.type === "plugin") return;
              e.stopPropagation();
              e.preventDefault();
              const replace = (path: string) => void replaceSample(channel, path);
              if (isFileDrag(e)) void importDroppedFiles(e).then((samples) => samples[0] && replace(samples[0].path));
              else if (droppedSample(e)) replace(droppedSample(e)!.path);
            }}
          >
            <span className="h-3 w-1 shrink-0 rounded-sm" style={{ background: color }} />
            <span className="truncate">{channel.name ?? channel.id}</span>
          </button>
        </ContextMenuTrigger>
        <ContextMenuContent>
          <ContextMenuItem onSelect={() => update({ mute: !channel.mute }, channel.mute ? "Unmute channel" : "Mute channel")}>{channel.mute ? "Unmute" : "Mute"}</ContextMenuItem>
          <ContextMenuItem onSelect={() => openChannel(channel)}>
            {channel.instrument.type === "plugin" ? "Open the plugin's window" : "Open the sampler"}
          </ContextMenuItem>
          <ContextMenuItem onSelect={() => setUi({ selectedChannel: channel.id, bottomPanel: "piano" })}>Open in piano roll</ContextMenuItem>
          <ContextMenuItem onSelect={() => void routeToNewInsert(doc, channel)}>Route to a new insert</ContextMenuItem>
          {channel.instrument.type === "sampler" && (
            <ContextMenuItem onSelect={() => update({ group: arrangement ? null : "arrangement" }, arrangement ? "Move to the rack" : "Move to the arrangement list")}>
              {arrangement ? "Move to the rack (steps)" : "Move to the arrangement list"}
            </ContextMenuItem>
          )}
          <ContextMenuSub label="Colour">
            <div className="grid grid-cols-6 gap-1 p-1">
              {palette.map((c) => (
                <button key={c} type="button" className="h-5 w-5 rounded-[3px]" style={{ background: c }} onClick={() => update({ color: c }, "Colour channel")} />
              ))}
            </div>
          </ContextMenuSub>
          <ContextMenuItem onSelect={() => void duplicateChannel(doc, channel, false)}>Duplicate</ContextMenuItem>
          <ContextMenuItem onSelect={() => void duplicateChannel(doc, channel, true)}>Duplicate with notes</ContextMenuItem>
          <ContextMenuSeparator />
          <ContextMenuItem danger onSelect={() => void execute({ type: "removeChannel", channel: channel.id }, { label: `Delete ${channel.name ?? channel.id}` })}>Delete channel</ContextMenuItem>
        </ContextMenuContent>
      </ContextMenu>

      <button type="button" title="Mute" onClick={() => update({ mute: !channel.mute }, channel.mute ? "Unmute channel" : "Mute channel")}
        className={cn("h-3 w-3 shrink-0 rounded-full border", channel.mute ? "border-faint bg-transparent" : "border-ok bg-ok/80")} />
      <ToggleChip on={!!channel.solo} label="S" tone="dim" title={channel.solo ? "Unsolo" : "Solo: only soloed channels play"}
        className={cn("h-4 min-w-4 px-0.5 text-[9px]", channel.solo && "bg-accent text-black")}
        onClick={() => update({ solo: !channel.solo }, channel.solo ? "Unsolo channel" : "Solo channel")} />
      {previewPath && (
        <IconButton size="sm" title="Preview sample" className="h-5 w-5" onClick={preview}><Volume2 size={11} /></IconButton>
      )}

      {arrangement ? (
        <ArrangementRowInfo doc={doc} channel={channel} color={color} width={steps * 22} />
      ) : pattern && (melodic ? (
        <MiniRoll notes={notes} length={patternLength(pattern, doc)} color={color} width={steps * 22}
          onOpen={() => setUi({ selectedChannel: channel.id, bottomPanel: "piano" })} />
      ) : (
        <div className="flex gap-[3px]">
          {Array.from({ length: steps }, (_, step) => {
            const note = notes.find((n) => n.start >= step * STEP && n.start < (step + 1) * STEP);
            const painted = paint?.steps.has(step);
            const on = painted ? paint!.on : !!note;
            const velocity = painted ? 100 : note?.velocity;
            const beatGroup = Math.floor(step / 4) % 2 === 0;
            return (
              <button
                key={step}
                type="button"
                className={cn(
                  "h-[22px] w-[19px] rounded-[4px] transition-[filter,transform] duration-75 hover:brightness-125 active:translate-y-px",
                  step % 4 === 0 && step > 0 && "ml-1.5",
                  step === playingStep && "brightness-150",
                )}
                style={stepStyle(on, color, velocity, beatGroup, step === playingStep)}
                onPointerDown={(e) => {
                  if (e.button !== 0) return;
                  (e.target as HTMLElement).releasePointerCapture?.(e.pointerId);
                  onPaintStart(step, !note);
                }}
                onPointerEnter={(e) => e.buttons & 1 && onPaintEnter(step)}
              />
            );
          })}
        </div>
      ))}

      {touched !== undefined && Date.now() - touched < 2000 && (
        <span key={touched} className="pointer-events-none absolute inset-0 animate-agent-flash bg-agent/20" />
      )}
    </div>
  );
}

/** A sample dropped onto the playlist has no steps (a loop would make no sense as 16 hits):
    its row shows the sample and how many clips play it. "Move to the rack" gives it steps. */
function ArrangementRowInfo({ doc, channel, color, width }: { doc: Project; channel: Channel; color: string; width: number }) {
  const clips = doc.playlist.clips.filter((c) => c.type === "audio" && c.channel === channel.id).length;
  const layer = channel.instrument.type === "sampler" ? channel.instrument.layers[0] : undefined;
  const peaks = usePeaks(layer?.sample);
  const canvas = useRef<HTMLCanvasElement>(null);
  const w = Math.max(120, Math.min(width, 420));

  useEffect(() => {
    if (!peaks || !canvas.current || !layer) return;
    drawPeaks(canvas.current, peaks, layer.start ?? 0, layer.end ?? peaks.durationSeconds, `color-mix(in srgb, ${color} 70%, #888)`);
  }, [peaks, layer, color]);

  return (
    <button type="button" onClick={() => openChannel(channel)} title="Open the sampler"
      className="flex h-[22px] items-center gap-2 overflow-hidden rounded-[3px] bg-[#232328] pr-2 text-2xs text-faint hover:bg-[#2a2a30] hover:text-dim">
      <canvas ref={canvas} width={w} height={22} style={{ width: w, height: 22, transform: layer?.reverse ? "scaleX(-1)" : undefined }} />
      <AudioLines size={11} className="shrink-0" />
      <span className="shrink-0">{clips} clip{clips === 1 ? "" : "s"}</span>
    </button>
  );
}

/** A channel's notes when they're melodic rather than steps; opens the piano roll. */
function MiniRoll({ notes, length, color, width, onOpen }: { notes: Note[]; length: number; color: string; width: number; onOpen: () => void }) {
  const keys = notes.map((n) => n.key ?? 60);
  const lo = Math.min(...keys), hi = Math.max(...keys);
  const h = 22;
  return (
    <button type="button" title="Open in the piano roll" onClick={onOpen} className="rounded-[3px] bg-[#232328] hover:bg-[#2a2a30]" style={{ width, height: h }}>
      <svg width={width} height={h}>
        {notes.map((n, i) => (
          <rect key={i} x={(n.start / length) * width} y={2 + (hi === lo ? 8 : ((hi - (n.key ?? 60)) / (hi - lo)) * (h - 7))}
            width={Math.max(2, (n.length / length) * width - 1)} height={3} fill={color} rx={1} />
        ))}
      </svg>
    </button>
  );
}

/** A copy of a channel (sampler layers, filter, modulation, routing) below it; with its notes,
    the copy plays the same notes in every pattern. */
function duplicateChannel(doc: Project, channel: Channel, withNotes: boolean) {
  const id = uniqueId(channel.id, doc.channels);
  const name = `${channel.name ?? channel.id} 2`;
  const notes: Command[] = withNotes
    ? doc.patterns.flatMap((p) => {
      const mine = p.notes.filter((n) => n.channel === channel.id).map((n) => ({ ...n, channel: id }));
      return mine.length ? [{ type: "addNotes" as const, pattern: p.id, notes: mine }] : [];
    })
    : [];
  return execute([{ type: "addChannel", channel: { ...channel, id, name }, index: doc.channels.indexOf(channel) + 1 }, ...notes],
    { label: `Duplicate ${channel.name ?? channel.id}` });
}
