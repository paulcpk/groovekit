import type { Clip, Project } from "@groovekit/schema/project";
import { AudioLines, Spline, VolumeX } from "lucide-react";
import { memo } from "react";
import { ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub } from "@/components/menu";
import { targetLabel } from "@/lib/automation";
import { cn } from "@/lib/cn";
import { snapTo } from "@/lib/drag";
import { barRange, barsAt, fitCommands, fitMode, formatBars, suggestedBars, tempoFor, type FitMode } from "@/lib/fit";
import { audioSource, colorFor, uniqueId } from "@/lib/model";
import { usePeaks } from "@/lib/peaks";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { openWindow } from "@/state/windows";
import { AudioContent, AutomationContent, PatternContent } from "./ClipContent";
import { makeAudioUnique, splitAtPlayhead, splitClips } from "./clipOps";
import { CLIP_HEADER_H } from "./geometry";

export function clipColor(clip: Clip, doc: Project) {
  if (clip.type === "pattern") {
    const pattern = doc.patterns.find((p) => p.id === clip.pattern);
    return colorFor(clip.pattern, pattern?.color);
  }
  if (clip.type === "audio") {
    const channel = doc.channels.find((c) => c.id === clip.channel);
    return colorFor(clip.channel, channel?.color);
  }
  return "#9fb0c2";
}

export function clipName(clip: Clip, doc: Project) {
  if (clip.type === "pattern") return doc.patterns.find((p) => p.id === clip.pattern)?.name ?? clip.pattern;
  if (clip.type === "audio") {
    const channel = doc.channels.find((c) => c.id === clip.channel);
    return channel?.name || clip.channel;
  }
  return targetLabel(clip.target, doc);
}

export interface ClipViewProps {
  clip: Clip;
  color: string;
  name: string;
  left: number;
  width: number;
  height: number;
  selected: boolean;
  ghost?: boolean;
  pxPerTick: number;
  snap: number;
  onPointerDown: (e: React.PointerEvent, clip: Clip, edge: "left" | "right" | "body") => void;
  /** A right-click on the clip, with the tick under the pointer: the playlist shows the menu. */
  onMenu: (clip: Clip, tick: number) => void;
}

/** The clip's body, like coloured glass: a soft gradient of its colour over the dark, a thin rim
    of it inside, a faint light along the top and a shadow beneath; selected, a white edge and a
    glow of its colour. */
function glass(color: string, selected: boolean): React.CSSProperties {
  return {
    backgroundImage: [
      "linear-gradient(180deg, rgba(255,255,255,0.06), rgba(255,255,255,0) 55%)",
      `linear-gradient(180deg, color-mix(in srgb, ${color} 30%, #1d1d21), color-mix(in srgb, ${color} 15%, #131316))`,
    ].join(", "),
    boxShadow: [
      `inset 0 0 0 1px color-mix(in srgb, ${color} 42%, transparent)`,
      "inset 0 1px 0 rgba(255,255,255,0.10)",
      selected ? `0 0 0 1.5px var(--color-fg), 0 0 12px color-mix(in srgb, ${color} 45%, transparent)` : "0 1px 3px rgba(0,0,0,0.5)",
    ].join(", "),
  };
}

/** The title bar: the colour with a gloss, lighter at the top. */
function glassHeader(color: string): React.CSSProperties {
  return {
    background: [
      "linear-gradient(180deg, rgba(255,255,255,0.28), rgba(255,255,255,0.06) 55%, rgba(0,0,0,0.08))",
      color,
    ].join(", "),
    boxShadow: "inset 0 -1px 0 rgba(0,0,0,0.22)",
    textShadow: "0 1px 0 rgba(255,255,255,0.25)",
  };
}

/** One clip in a lane: coloured title bar, content preview below. Drawn again only when its own
    props change, so a drag or a selection redraws just the clips it touches. The playlist owns the
    one context menu for all clips (ClipMenu). */
export const ClipView = memo(function ClipView({ clip, color, name, left, width, height, selected, ghost, pxPerTick, snap, onPointerDown, onMenu }: ClipViewProps) {
  const touched = useEngine((s) => s.touchedAt[clip.id]);
  const bodyH = height - CLIP_HEADER_H;
  const muted = clip.mute;

  return (
    <div
      data-clip={ghost ? undefined : ""}
      className={cn(
        "group/clip absolute top-px overflow-hidden rounded-[4px]",
        selected && "z-[5]",
        ghost && "opacity-50",
        muted && "opacity-45 saturate-0",
      )}
      style={{ left, width: Math.max(3, width), height, ...glass(color, selected) }}
      onPointerDown={(e) => {
        if (e.button !== 0) return;
        // The keyboards play the sample of the clip last touched.
        if (clip.type === "audio") setUi({ selectedChannel: clip.channel });
        const x = e.clientX - e.currentTarget.getBoundingClientRect().left;
        const edge = x > width - 6 && width > 14 ? "right" : x < 5 && width > 14 ? "left" : "body";
        onPointerDown(e, clip, edge);
      }}
      onContextMenu={(e) => {
        if (ghost) return;
        const x = e.clientX - e.currentTarget.getBoundingClientRect().left;
        onMenu(clip, Math.round(clip.start + snapTo(x / pxPerTick, snap)));
      }}
      onDoubleClick={() => {
        if (clip.type === "audio") openWindow({ kind: "channel", channel: clip.channel });
        if (clip.type === "pattern") setUi({ selectedPattern: clip.pattern, bottomOpen: true, bottomPanel: "rack" });
      }}
    >
      <div className="flex items-center gap-1 truncate px-1 text-2xs font-medium leading-none text-black/80"
        style={{ height: CLIP_HEADER_H, ...glassHeader(color) }}>
        {muted && <VolumeX size={9} />}
        {clip.type === "audio" && <AudioLines size={9} />}
        {clip.type === "automation" && <Spline size={9} />}
        <span className="truncate">{name}</span>
      </div>
      <div className="relative" style={{ height: bodyH }}>
        {clip.type === "pattern" && <PatternContent clip={clip} width={width} height={bodyH - 4} color={color} />}
        {clip.type === "audio" && <AudioContent clip={clip} width={width} height={bodyH} color={color} />}
        {clip.type === "automation" && <AutomationContent clip={clip} width={width} height={bodyH} color={color} pxPerTick={pxPerTick} snap={snap} />}
      </div>
      <div className="absolute top-0 right-0 bottom-0 w-1.5 cursor-ew-resize" />
      <div className="absolute top-0 bottom-0 left-0 w-1 cursor-ew-resize" />
      {touched !== undefined && Date.now() - touched < 2000 && (
        <span key={touched} className="pointer-events-none absolute inset-0 animate-agent-flash rounded-[3px] ring-2 ring-agent ring-inset bg-agent/20" />
      )}
    </div>
  );
});

/** The menu for the clip last right-clicked (by id, so it follows the song's changes), with the
    tick that was under the pointer. The playlist renders it inside its one ContextMenu. */
export function ClipMenu({ clipId, at }: { clipId: string; at: number }) {
  const doc = useEngine((s) => s.doc);
  const clip = doc?.playlist.clips.find((c) => c.id === clipId);
  const selection = useUi((s) => s.selectedClips);
  if (!doc || !clip) return null;
  const targets = selection.includes(clip.id) ? doc.playlist.clips.filter((c) => selection.includes(c.id)) : [clip];
  const plural = targets.length > 1 ? ` ${targets.length} clips` : "";

  const duplicate = () => {
    const taken = new Set(doc.playlist.clips.map((c) => c.id));
    const span = Math.max(...targets.map((c) => c.start + c.length)) - Math.min(...targets.map((c) => c.start));
    const copies = targets.map((c) => {
      const id = uniqueId(c.id.replace(/-\d+$/, ""), taken);
      taken.add(id);
      return { ...c, id, start: c.start + span };
    });
    void execute(copies.map((c) => ({ type: "addClip" as const, clip: c })), { label: `Duplicate${plural || " clip"}` });
    setUi({ selectedClips: copies.map((c) => c.id) });
  };

  const makeUnique = () => {
    if (clip.type !== "pattern") return;
    const pattern = doc.patterns.find((p) => p.id === clip.pattern);
    if (!pattern) return;
    const id = uniqueId(pattern.id, doc.patterns);
    void execute([
      { type: "addPattern", pattern: { ...pattern, id, name: `${pattern.name ?? pattern.id} (copy)` } },
      { type: "updateClip", clip: clip.id, changes: { pattern: id } },
    ], { label: "Make pattern unique" });
    setUi({ selectedPattern: id });
  };

  return (
    <ContextMenuContent>
      {clip.type === "pattern" && (
        <>
          <ContextMenuItem onSelect={() => setUi({ selectedPattern: clip.pattern, bottomOpen: true, bottomPanel: "rack" })}>Open in channel rack</ContextMenuItem>
          <ContextMenuItem onSelect={() => setUi({ selectedPattern: clip.pattern, bottomOpen: true, bottomPanel: "piano" })}>Open in piano roll</ContextMenuItem>
          <ContextMenuItem onSelect={makeUnique}>Make unique</ContextMenuItem>
          <ContextMenuSeparator />
        </>
      )}
      <ContextMenuItem onSelect={duplicate}>Duplicate{plural}</ContextMenuItem>
      <ContextMenuItem onSelect={() => splitClips(at, [clip.id])} disabled={at <= clip.start || at >= clip.start + clip.length}>Split here</ContextMenuItem>
      <ContextMenuItem shortcut="⌘E" onSelect={() => splitAtPlayhead()}>Split at playhead</ContextMenuItem>
      <ContextMenuItem onSelect={() => void execute(targets.map((c) => ({ type: "updateClip" as const, clip: c.id, changes: { mute: !clip.mute } })),
        { label: `${clip.mute ? "Unmute" : "Mute"}${plural || " clip"}` })}>
        {clip.mute ? "Unmute" : "Mute"}{plural}
      </ContextMenuItem>
      {clip.type === "audio" && (
        <>
          <ContextMenuSeparator />
          <ContextMenuItem onSelect={() => openWindow({ kind: "channel", channel: clip.channel })}>Open the channel</ContextMenuItem>
          <FitMenu clip={clip} doc={doc} />
          <ContextMenuItem onSelect={() => void makeAudioUnique(doc, clip)}>Make unique</ContextMenuItem>
          <ContextMenuItem onSelect={() => void execute({ type: "updateClip", clip: clip.id, changes: { fadeIn: null, fadeOut: null } }, { label: "Remove fades" })}
            disabled={!clip.fadeIn && !clip.fadeOut}>Remove fades</ContextMenuItem>
        </>
      )}
      <ContextMenuSeparator />
      <ContextMenuItem danger shortcut="⌫" onSelect={() => {
        void execute(targets.map((c) => ({ type: "removeClip" as const, clip: c.id })), { label: `Delete${plural || " clip"}` });
        setUi({ selectedClips: [] });
      }}>Delete{plural}</ContextMenuItem>
    </ContextMenuContent>
  );
}

/** Quick choices for fitting an audio clip's sample to the song: how, and how many bars. */
function FitMenu({ clip, doc }: { clip: Clip; doc: Project }) {
  const source = clip.type === "audio" ? audioSource(doc, clip) : undefined;
  const peaks = usePeaks(source?.sample);
  if (!source?.channel || !source.layer || !peaks) return null;

  const { channel, layer } = source;
  const seconds = Math.max(0.001, (layer.end ?? peaks.durationSeconds) - (layer.start ?? 0));
  const mode = fitMode(layer), range = barRange(doc, seconds);
  const bars = layer.sourceTempo ? barsAt(doc, seconds, layer.sourceTempo) : suggestedBars(doc, seconds);
  const fit = (to: number, resample: boolean, label: string) =>
    void execute(fitCommands(doc, channel, seconds, { sourceTempo: Math.round(tempoFor(doc, seconds, to) * 1000) / 1000, resample }), { label });
  const modes: { value: FitMode; label: string }[] = [{ value: "stretch", label: "Stretch, pitch kept" }, { value: "resample", label: "Resample, pitch follows" }, { value: "off", label: "As recorded" }];
  const counts = [0.25, 0.5, 1, 2, 4, 8, 16, 32].filter((b) => b >= range.min - 1e-6 && b <= range.max + 1e-6);

  return (
    <ContextMenuSub label={`Fit to song${mode === "off" ? "" : `: ${formatBars(bars)}`}`}>
      {modes.map((m) => (
        <ContextMenuItem key={m.value} onSelect={() => m.value === "off"
          ? void execute(fitCommands(doc, channel, seconds, { sourceTempo: null, resample: false }), { label: "Don't fit to the song" })
          : fit(bars, m.value === "resample", m.value === "resample" ? "Resample to the song" : "Stretch to the song")}>
          {mode === m.value ? "✓ " : ""}{m.label}
        </ContextMenuItem>
      ))}
      <ContextMenuSeparator />
      {counts.map((b) => (
        <ContextMenuItem key={b} onSelect={() => fit(b, !!layer.resample, `Fit to ${formatBars(b)}`)}>
          {mode !== "off" && Math.abs(bars - b) < 1e-3 ? "✓ " : ""}Fill {formatBars(b)}
        </ContextMenuItem>
      ))}
    </ContextMenuSub>
  );
}
