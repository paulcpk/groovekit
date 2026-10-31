import type { Channel, Note, Pattern, Project } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { AudioLines, ChevronDown, Copy, Plus, Trash2, Volume2 } from "lucide-react";
import { memo, useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useShallow } from "zustand/react/shallow";
import { Button, Empty, IconButton, Knob, ToggleChip } from "@/components/controls";
import { ActivityLed } from "@/components/fader";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger, Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { droppedSample, isSampleDrag } from "@/features/playlist/geometry";
import { importDroppedFiles, isFileDrag } from "@/features/shell/importFiles";
import { newPattern } from "@/features/playlist/edits";
import { cn } from "@/lib/cn";
import { useAutomated } from "@/lib/automation";
import { useLiveControl } from "@/lib/liveControl";
import { useLivePosition } from "@/lib/playhead";
import { ColorSwatch } from "@/components/color";
import { colorFor, formatDb, formatPan, palette, patternLength, patternLoop, STEP, ticksPerBar, uniqueId } from "@/lib/model";
import { drawPeaks, usePeaks } from "@/lib/peaks";
import { call, execute, useEngine } from "@/state/engine";
import { setUi, useUi, type RackView } from "@/state/ui";
import { openChannel } from "./channels";
import { addSamplerChannel, replaceSample, routeToNewInsert, stepKey } from "./channels";
import { InsertItems } from "./ChannelBar";
import { InstrumentPicker } from "./InstrumentPicker";
import { addInstrument, droppedInstrument, isInstrumentDrag, replaceInstrument } from "./instruments";
import { copyMidiNotes, pasteMidiNotes } from "./midiNotes";
import { keyNamesOf } from "@/features/piano/keyNames";

interface Paint {
  channel: string;
  on: boolean;
  steps: Set<number>;
  /** Released and sent: still drawn until the engine's change arrives, so steps don't flick back. */
  committed?: boolean;
}

export function ChannelRack() {
  const doc = useEngine((s) => s.doc)!;
  const { selectedPattern, selectedChannel, rackView } = useUi(useShallow((s) => ({ selectedPattern: s.selectedPattern, selectedChannel: s.selectedChannel, rackView: s.rackView })));
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);
  const root = useRef<HTMLDivElement>(null);
  const channels = doc.channels.filter((c) => rackView === "all" || (c.group ?? "rack") === rackView);
  const [paint, setPaint] = useState<Paint | null>(null);
  const paintRef = useRef<Paint | null>(null);
  paintRef.current = paint;

  // Painting ends wherever the pointer is released.
  useEffect(() => {
    const up = () => {
      const p = paintRef.current;
      if (!p || p.committed) return;
      const committed = { ...p, committed: true };
      setPaint(committed);
      // The engine sends the change before it answers, so the document has it by now. A song
      // without a pattern gets one for the steps.
      void (async () => {
        if (!pattern) await newPattern(doc);
        const song = useEngine.getState().doc!;
        const target = pattern ?? song.patterns.find((x) => x.id === useUi.getState().selectedPattern);
        if (target) await commitPaint(song, target, p);
      })().finally(() => setPaint((current) => (current === committed ? null : current)));
    };
    window.addEventListener("pointerup", up);
    return () => window.removeEventListener("pointerup", up);
  }, [doc, pattern]);

  const steps = pattern ? Math.round(patternLength(pattern, doc) / STEP) : 16;
  // The step playing now, when this pattern plays in pattern mode: lit by marking its pads, not
  // by drawing the rack again sixteen times a bar.
  const litStep = useRef(-1);
  useLivePosition(({ ticks, playing }) => {
    const t = useEngine.getState().transport;
    const step = playing && t.mode === "pattern" && t.pattern === pattern?.id ? Math.floor(ticks / STEP) : -1;
    if (step === litStep.current || !root.current) return;
    litStep.current = step;
    for (const pad of root.current.querySelectorAll("[data-playing]")) pad.removeAttribute("data-playing");
    if (step >= 0) for (const pad of root.current.querySelectorAll(`[data-step="${step}"]`)) pad.setAttribute("data-playing", "");
  }, [pattern?.id]);

  // The channel last right-clicked, for the one context menu all the rows share.
  const [menuChannel, setMenuChannel] = useState<string | null>(null);

  // The instrument picker: for a new channel, or replacing a channel's instrument.
  const [picking, setPicking] = useState<{ replace?: string } | null>(null);
  const pickerChannel = picking?.replace ? doc.channels.find((c) => c.id === picking.replace) : undefined;

  // Plugins' key names, asked once so a kit's steps go on its bass drum.
  const pluginChannels = doc.channels.filter((c) => c.instrument.type === "plugin").map((c) => c.id).join();
  useEffect(() => {
    for (const c of useEngine.getState().doc?.channels ?? []) if (c.instrument.type === "plugin") void keyNamesOf(c);
  }, [pluginChannels]);

  const onPaintStart = useCallback((channel: string, step: number, on: boolean) => setPaint({ channel, on, steps: new Set([step]) }), []);
  const onPaintEnter = useCallback((channel: string, step: number) =>
    setPaint((p) => (p && !p.committed && p.channel === channel && !p.steps.has(step) ? { ...p, steps: new Set([...p.steps, step]) } : p)), []);

  return (
    <div ref={root} className="flex h-full">
      <div
        className="flex min-w-0 flex-1 flex-col"
        onDragOver={(e) => (isSampleDrag(e) || isFileDrag(e) || isInstrumentDrag(e)) && e.preventDefault()}
        onDrop={(e) => {
          if (isInstrumentDrag(e)) {
            e.preventDefault();
            const choice = droppedInstrument(e);
            if (choice) void addInstrument(doc, choice);
            return;
          }
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
        <RackToolbar doc={doc} pattern={pattern} onAdd={() => setPicking({})} />
        <ContextMenu>
        <ContextMenuTrigger asChild>
        <div className="min-h-0 flex-1 overflow-auto py-1"
          onContextMenu={(e) => {
            const row = (e.target as Element).closest<HTMLElement>("[data-channel]");
            if (row) setMenuChannel(row.dataset.channel!);
            else e.preventDefault();
          }}>
          {channels.length === 0 && rackView === "arrangement" ? (
            <Empty>Samples dropped onto the playlist are listed here, each with its own sampler.</Empty>
          ) : (
            <>
              {channels.map((channel) => (
                <ChannelRow
                  key={channel.id}
                  channel={channel} pattern={pattern} steps={steps}
                  selected={channel.id === selectedChannel}
                  paint={paint?.channel === channel.id ? paint : null}
                  onPaintStart={onPaintStart} onPaintEnter={onPaintEnter}
                />
              ))}
              <AddArea first={channels.length === 0} onClick={() => setPicking({})} />
            </>
          )}
        </div>
        </ContextMenuTrigger>
        {menuChannel && <ChannelMenu channelId={menuChannel} onReplace={() => setPicking({ replace: menuChannel })} />}
        </ContextMenu>
        <InstrumentPicker open={picking !== null} onOpenChange={(open) => !open && setPicking(null)}
          title={pickerChannel ? `Replace ${pickerChannel.name ?? pickerChannel.id}'s instrument` : "Add an instrument"}
          onPick={(choice) => {
            const song = useEngine.getState().doc!;
            if (pickerChannel) void replaceInstrument(song, pickerChannel, choice);
            else void addInstrument(song, choice);
          }} />
      </div>
    </div>
  );
}

/** Below the channels, always, with room to scroll to it: a field to drop samples on (each
    becomes a sampler) or to click for the instrument picker. */
function AddArea({ first, onClick }: { first: boolean; onClick: () => void }) {
  return (
    <div className={cn("px-2 pt-2", first ? "h-full min-h-[140px] pb-2" : "pb-[100px]")}>
      <button type="button" onClick={onClick}
        className={cn("flex w-full flex-col items-center justify-center gap-1.5 rounded-lg border border-dashed border-line text-faint transition-colors hover:border-accent hover:bg-accent/5 hover:text-dim",
          first ? "h-full min-h-[124px]" : "h-[76px]")}>
        <Plus size={first ? 26 : 18} strokeWidth={1.5} />
        <span className="text-xs">Drop samples here, or choose an instrument</span>
      </button>
    </div>
  );
}

/** The menu for the channel last right-clicked (by id, so it follows the song's changes). */
function ChannelMenu({ channelId, onReplace }: { channelId: string; onReplace: () => void }) {
  const channel = useEngine((s) => s.doc?.channels.find((c) => c.id === channelId));
  const pattern = useEngine((s) => s.doc?.patterns.find((p) => p.id === useUi.getState().selectedPattern));
  if (!channel) return null;
  const arrangement = channel.group === "arrangement";
  const song = () => useEngine.getState().doc!;
  const update = (changes: Record<string, unknown>, label: string) => void execute({ type: "updateChannel", channel: channel.id, changes }, { label });

  return (
    <ContextMenuContent>
      <ContextMenuItem onSelect={() => update({ mute: !channel.mute }, channel.mute ? "Unmute channel" : "Mute channel")}>{channel.mute ? "Unmute" : "Mute"}</ContextMenuItem>
      <ContextMenuItem onSelect={() => update({ solo: !channel.solo }, channel.solo ? "Unsolo channel" : "Solo channel")}>{channel.solo ? "Unsolo" : "Solo"}</ContextMenuItem>
      <ContextMenuItem onSelect={() => openChannel(channel)}>
        {channel.instrument.type === "plugin" ? "Open the plugin's window" : "Open the sampler"}
      </ContextMenuItem>
      <ContextMenuItem onSelect={() => setUi({ selectedChannel: channel.id, bottomPanel: "piano" })}>Open in piano roll</ContextMenuItem>
      <ContextMenuItem onSelect={() => void routeToNewInsert(song(), channel)}>Route to a new insert</ContextMenuItem>
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
      <ContextMenuItem onSelect={() => update({ volumeDb: null, pan: null }, "Reset channel volume and pan")} disabled={!channel.volumeDb && !channel.pan}>
        Reset volume and pan
      </ContextMenuItem>
      <ContextMenuItem onSelect={() => void duplicateChannel(song(), channel, false)}>Duplicate</ContextMenuItem>
      <ContextMenuItem onSelect={() => void duplicateChannel(song(), channel, true)}>Duplicate with notes</ContextMenuItem>
      <ContextMenuItem onSelect={onReplace}>Replace instrument…</ContextMenuItem>
      <ContextMenuSeparator />
      <ContextMenuItem disabled={!pattern} onSelect={() => pattern && copyMidiNotes(pattern, channel)}>Copy MIDI notes</ContextMenuItem>
      <ContextMenuItem disabled={!pattern} onSelect={() => pattern && void pasteMidiNotes(song(), pattern, channel, false)}>Paste MIDI notes</ContextMenuItem>
      <ContextMenuItem disabled={!pattern} onSelect={() => pattern && void pasteMidiNotes(song(), pattern, channel, true)}>Paste MIDI notes (add)</ContextMenuItem>
      <ContextMenuSeparator />
      <ContextMenuItem danger onSelect={() => void execute({ type: "removeChannel", channel: channel.id }, { label: `Delete ${channel.name ?? channel.id}` })}>Delete channel</ContextMenuItem>
    </ContextMenuContent>
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
    channel's colour when on (softer at lower velocities) and in greys when off. (The step
    playing is marked by the rack with data-playing, styled in index.css.) */
function stepStyle(on: boolean, color: string, velocity: number | undefined, beatGroup: boolean): React.CSSProperties {
  const off = beatGroup ? "#2e2e35" : "#25252b";

  if (!on) {
    return {
      background: `linear-gradient(to bottom, color-mix(in srgb, ${off} 82%, white), ${off} 55%, color-mix(in srgb, ${off} 85%, black))`,
      border: "1px solid rgb(0 0 0 / 0.45)",
      boxShadow: "inset 0 1px 0 rgb(255 255 255 / 0.07), inset 0 -2px 0 rgb(0 0 0 / 0.28), 0 1px 1px rgb(0 0 0 / 0.35)",
    };
  }

  // Quieter steps lean toward the off colour rather than fading out.
  const strength = Math.round(45 + ((velocity ?? 100) / 127) * 55);
  const base = `color-mix(in srgb, ${color} ${strength}%, ${off})`;
  return {
    background: `linear-gradient(to bottom, color-mix(in srgb, ${base} 72%, white), ${base} 50%, color-mix(in srgb, ${base} 82%, black))`,
    border: `1px solid color-mix(in srgb, ${base} 55%, black)`,
    boxShadow: `inset 0 1px 0 rgb(255 255 255 / 0.38), inset 0 -2px 0 rgb(0 0 0 / 0.22), 0 1px 2px rgb(0 0 0 / 0.45), 0 0 8px -2px color-mix(in srgb, ${color} 55%, transparent)`,
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

function RackToolbar({ doc, pattern, onAdd }: { doc: Project; pattern: Pattern | undefined; onAdd: () => void }) {
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
          <ColorSwatch color={colorFor(pattern.id, pattern.color)} chosen={!!pattern.color} title="Pattern colour" className="mx-1"
            onChange={(c) => void execute({ type: "updatePattern", pattern: pattern.id, changes: { color: c } }, { label: c ? "Colour pattern" : "Reset pattern colour" })} />
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
      <Button size="sm" title="Add a channel with an instrument (or drop samples onto the rack)" onClick={onAdd}><Plus size={12} /> Instrument</Button>
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
/** The channel's activity light, on its own so the levels (thirty times a second) redraw only it. */
function ChannelLed({ channel }: { channel: string }) {
  const level = useEngine((s) => s.levels.channels[channel] ?? 0);
  return <ActivityLed level={level} />;
}

/** The channel's pan and volume knobs, turning with their automation as the song plays. */
function ChannelKnobs({ channel }: { channel: Channel }) {
  const volume = useLiveControl((v) => ({ type: "updateChannel", channel: channel.id, changes: { volumeDb: Math.round(v * 10) / 10 } }), "Channel volume");
  const pan = useLiveControl((v) => ({ type: "updateChannel", channel: channel.id, changes: { pan: Math.round(v * 100) / 100 } }), "Pan channel");
  const volumeShown = useAutomated({ scope: "channel", channel: channel.id, param: "volume" }, channel.volumeDb ?? 0);
  const panShown = useAutomated({ scope: "channel", channel: channel.id, param: "pan" }, channel.pan ?? 0);

  return (
    <>
      <Knob size={20} value={panShown} min={-1} max={1} defaultValue={0} bipolar title="Pan" format={formatPan} onChange={pan.onChange} onCommit={pan.onCommit}
        automation={{ scope: "channel", channel: channel.id, param: "pan" }} automationValue={channel.pan ?? 0} />
      <Knob size={20} value={volumeShown} min={-60} max={12} defaultValue={0} title="Volume" format={(v) => `${formatDb(v)} dB`} onChange={volume.onChange} onCommit={volume.onCommit}
        automation={{ scope: "channel", channel: channel.id, param: "volume" }} automationValue={channel.volumeDb ?? 0} />
    </>
  );
}

/** Drawn again only when its own props change: its channel, the pattern, the selection or the
    steps being painted on it. What it needs of the rest of the song it reads itself. */
const ChannelRow = memo(function ChannelRow({ channel, pattern, steps, selected, paint, onPaintStart, onPaintEnter }: {
  channel: Channel; pattern: Pattern | undefined; steps: number; selected: boolean;
  paint: Paint | null; onPaintStart: (channel: string, step: number, on: boolean) => void; onPaintEnter: (channel: string, step: number) => void;
}) {
  const touched = useEngine((s) => s.touchedAt[channel.id]);
  const color = colorFor(channel.id, channel.color);
  const notes = useMemo(() => pattern?.notes.filter((n) => n.channel === channel.id) ?? [], [pattern, channel.id]);
  const melodic = new Set(notes.map((n) => n.key ?? 60)).size > 1 || notes.some((n) => n.length > STEP * 1.5);
  const insertIndex = useEngine((s) => s.doc?.mixer.inserts.findIndex((i) => i.id === (channel.insert ?? "master")) ?? -1);
  const bar = useEngine((s) => ticksPerBar(s.doc));
  const arrangement = channel.group === "arrangement";
  const update = (changes: Record<string, unknown>, label: string) => void execute({ type: "updateChannel", channel: channel.id, changes }, { label });

  const previewPath = channel.instrument.type === "sampler" ? channel.instrument.layers[0]?.sample : undefined;
  const preview = () => previewPath && void call("preview.play", { path: previewPath });

  return (
    <div className={cn("relative flex h-[30px] items-center gap-2 px-2", selected && "bg-raised")} data-channel={channel.id}>
      <ChannelLed channel={channel.id} />
      <ChannelKnobs channel={channel} />

      <Dropdown>
        <DropdownTrigger asChild>
          <button type="button" title="Mixer insert" className="h-[18px] w-7 rounded-[3px] bg-control text-2xs text-dim tabular hover:text-fg">
            {insertIndex <= 0 ? "M" : insertIndex}
          </button>
        </DropdownTrigger>
        <DropdownContent>
          <InsertItems channel={channel} />
        </DropdownContent>
      </Dropdown>

      <button
            type="button"
            className={cn(
              "flex h-[22px] w-[150px] shrink-0 items-center gap-1.5 truncate rounded-[3px] px-2 text-left text-xs",
              channel.mute ? "bg-control text-faint" : "bg-hover text-fg",
              selected && "ring-1 ring-accent",
            )}
            title={channel.instrument.type === "plugin" ? "Open the plugin's window" : "Open the sampler"}
            onClick={() => openChannel(channel, true)}
            onDragOver={(e) => (isInstrumentDrag(e) || (channel.instrument.type !== "plugin" && (isSampleDrag(e) || isFileDrag(e)))) && (e.preventDefault(), e.stopPropagation())}
            onDrop={(e) => {
              // An instrument dropped on a channel's name replaces its instrument; a sample
              // replaces a sampler's sample (on a plugin channel it makes a new sampler below).
              if (isInstrumentDrag(e)) {
                e.preventDefault();
                e.stopPropagation();
                const choice = droppedInstrument(e);
                if (choice) void replaceInstrument(useEngine.getState().doc!, channel, choice);
                return;
              }
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

      <button type="button" title="Mute" onClick={() => update({ mute: !channel.mute }, channel.mute ? "Unmute channel" : "Mute channel")}
        className={cn("h-3 w-3 shrink-0 rounded-full border", channel.mute ? "border-faint bg-transparent" : "border-ok bg-ok/80")} />
      <ToggleChip on={!!channel.solo} label="S" tone="dim" title={channel.solo ? "Unsolo" : "Solo: only soloed channels play"}
        className={cn("h-4 min-w-4 px-0.5 text-[9px]", channel.solo && "bg-accent text-black")}
        onClick={() => update({ solo: !channel.solo }, channel.solo ? "Unsolo channel" : "Solo channel")} />
      {previewPath && (
        <IconButton size="sm" title="Preview sample" className="h-5 w-5" onClick={preview}><Volume2 size={11} /></IconButton>
      )}

      {arrangement ? (
        <ArrangementRowInfo channel={channel} color={color} width={steps * 22} />
      ) : pattern && (melodic ? (
        <MiniRoll notes={notes} length={patternLoop(pattern, bar)} color={color} width={steps * 22}
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
                data-step={step}
                className={cn(
                  "step-pad h-[22px] w-[19px] rounded-[4px] transition-[filter,transform] duration-75 hover:brightness-125 active:translate-y-px",
                  step % 4 === 0 && step > 0 && "ml-1.5",
                )}
                style={stepStyle(on, color, velocity, beatGroup)}
                onPointerDown={(e) => {
                  if (e.button !== 0) return;
                  (e.target as HTMLElement).releasePointerCapture?.(e.pointerId);
                  onPaintStart(channel.id, step, !note);
                }}
                onPointerEnter={(e) => e.buttons & 1 && onPaintEnter(channel.id, step)}
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
});

/** A sample dropped onto the playlist has no steps (a loop would make no sense as 16 hits):
    its row shows the sample and how many clips play it. "Move to the rack" gives it steps. */
function ArrangementRowInfo({ channel, color, width }: { channel: Channel; color: string; width: number }) {
  const clips = useEngine((s) => s.doc?.playlist.clips.filter((c) => c.type === "audio" && c.channel === channel.id).length ?? 0);
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
