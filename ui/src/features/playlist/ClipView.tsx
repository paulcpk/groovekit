import type { Clip, Project } from "@groovekit/schema/project";
import { AudioLines, Spline, VolumeX } from "lucide-react";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuTrigger } from "@/components/menu";
import { targetLabel } from "@/lib/automation";
import { cn } from "@/lib/cn";
import { baseName, colorFor, uniqueId } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { useState } from "react";
import { snapTo } from "@/lib/drag";
import { AudioContent, AutomationContent, PatternContent } from "./ClipContent";
import { splitAtPlayhead, splitClips } from "./clipOps";
import { CLIP_HEADER_H } from "./geometry";

export function clipColor(clip: Clip, doc: Project) {
  if (clip.type === "pattern") {
    const pattern = doc.patterns.find((p) => p.id === clip.pattern);
    return colorFor(clip.pattern, pattern?.color);
  }
  if (clip.type === "audio") return colorFor(clip.sample);
  return "#9fb0c2";
}

export function clipName(clip: Clip, doc: Project) {
  if (clip.type === "pattern") return doc.patterns.find((p) => p.id === clip.pattern)?.name ?? clip.pattern;
  if (clip.type === "audio") return baseName(clip.sample);
  return targetLabel(clip.target, doc);
}

export interface ClipViewProps {
  clip: Clip;
  doc: Project;
  left: number;
  width: number;
  height: number;
  selected: boolean;
  ghost?: boolean;
  pxPerTick: number;
  snap: number;
  onPointerDown: (e: React.PointerEvent, clip: Clip, edge: "left" | "right" | "body") => void;
}

/** One clip in a lane: coloured title bar, content preview below. */
export function ClipView({ clip, doc, left, width, height, selected, ghost, pxPerTick, snap, onPointerDown }: ClipViewProps) {
  const color = clipColor(clip, doc);
  const touched = useEngine((s) => s.touchedAt[clip.id]);
  const bodyH = height - CLIP_HEADER_H;
  const muted = clip.mute;
  const [menuTick, setMenuTick] = useState(clip.start);

  const body = (
    <div
      className={cn(
        "absolute top-px overflow-hidden rounded-[3px]",
        selected ? "ring-1 ring-fg" : "ring-1 ring-black/40",
        ghost && "opacity-50",
        muted && "opacity-45 saturate-0",
      )}
      style={{ left, width: Math.max(3, width), height, background: `color-mix(in srgb, ${color} 22%, #18181b)` }}
      onPointerDown={(e) => {
        if (e.button !== 0) return;
        const x = e.clientX - e.currentTarget.getBoundingClientRect().left;
        const edge = x > width - 6 && width > 14 ? "right" : x < 5 && width > 14 ? "left" : "body";
        onPointerDown(e, clip, edge);
      }}
      onContextMenu={(e) => {
        const x = e.clientX - e.currentTarget.getBoundingClientRect().left;
        setMenuTick(Math.round(clip.start + snapTo(x / pxPerTick, snap)));
      }}
      onDoubleClick={() => {
        if (clip.type === "audio") setUi({ clipEditor: clip.id });
        if (clip.type === "pattern") setUi({ selectedPattern: clip.pattern, bottomOpen: true, bottomPanel: "rack" });
      }}
    >
      <div className="flex items-center gap-1 truncate px-1 text-2xs font-medium leading-none text-black/80" style={{ height: CLIP_HEADER_H, background: color }}>
        {muted && <VolumeX size={9} />}
        {clip.type === "audio" && <AudioLines size={9} />}
        {clip.type === "automation" && <Spline size={9} />}
        <span className="truncate">{clipName(clip, doc)}</span>
      </div>
      <div className="relative" style={{ height: bodyH }}>
        {clip.type === "pattern" && <PatternContent clip={clip} doc={doc} width={width} height={bodyH - 4} color={color} />}
        {clip.type === "audio" && <AudioContent clip={clip} doc={doc} width={width} height={bodyH} color={color} />}
        {clip.type === "automation" && <AutomationContent clip={clip} doc={doc} width={width} height={bodyH} color={color} pxPerTick={pxPerTick} snap={snap} />}
      </div>
      <div className="absolute top-0 right-0 bottom-0 w-1.5 cursor-ew-resize" />
      <div className="absolute top-0 bottom-0 left-0 w-1 cursor-ew-resize" />
      {touched !== undefined && Date.now() - touched < 2000 && (
        <span key={touched} className="pointer-events-none absolute inset-0 animate-agent-flash rounded-[3px] ring-2 ring-agent ring-inset bg-agent/20" />
      )}
    </div>
  );

  if (ghost) return body;

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>{body}</ContextMenuTrigger>
      <ClipMenu clip={clip} doc={doc} at={menuTick} />
    </ContextMenu>
  );
}

function ClipMenu({ clip, doc, at }: { clip: Clip; doc: Project; at: number }) {
  const selection = useUi((s) => s.selectedClips);
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
          <ContextMenuItem onSelect={() => setUi({ clipEditor: clip.id })}>Gain, fades and stretch…</ContextMenuItem>
          <ContextMenuItem onSelect={() => void execute(targets.filter((c) => c.type === "audio").map((c) => ({ type: "updateClip" as const, clip: c.id, changes: { reverse: !clip.reverse } })),
            { label: clip.reverse ? "Play forwards" : "Reverse" })}>
            {clip.reverse ? "Play forwards" : "Reverse"}{plural}
          </ContextMenuItem>
          <ContextMenuItem onSelect={() => void execute({ type: "updateClip", clip: clip.id, changes: { gainDb: null } }, { label: "Reset clip gain" })} disabled={clip.gainDb === undefined}>
            Reset gain{clip.gainDb !== undefined ? ` (${clip.gainDb > 0 ? "+" : ""}${clip.gainDb.toFixed(1)} dB)` : ""}
          </ContextMenuItem>
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
