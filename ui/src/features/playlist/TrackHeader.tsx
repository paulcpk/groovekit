import type { PlaylistTrack, Project } from "@groovekit/schema/project";
import { useState } from "react";
import { ToggleChip } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger } from "@/components/menu";
import { cn } from "@/lib/cn";
import { palette } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import { HEADER_W } from "./geometry";

/** A playlist track's name, mute and solo. Tracks only arrange: where clips play and how loud is
    set by their channels, so a track with nothing on it is just an empty lane. */
export function TrackHeader({ track, index, doc, height }: { track: PlaylistTrack; index: number; doc: Project; height: number }) {
  const [editing, setEditing] = useState(false);
  const touched = useEngine((s) => s.touchedAt[`track:${index}`]);
  const clips = doc.playlist.clips.filter((c) => c.track === index).length;
  const name = track.name || `Track ${index + 1}`;
  const update = (changes: Partial<PlaylistTrack>, label: string) => void execute({ type: "updateTrack", track: index, changes }, { label });

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        <div
          className="sticky left-0 z-10 flex shrink-0 items-center gap-1.5 border-r border-b border-line bg-raised pr-1.5"
          style={{ width: HEADER_W, height }}
        >
          <div className="h-full w-1 shrink-0" style={{ background: track.color ?? "transparent" }} />
          <div className="min-w-0 flex-1">
            {editing ? (
              <input
                autoFocus defaultValue={track.name ?? ""} placeholder={`Track ${index + 1}`}
                className="w-full rounded-[3px] bg-bg px-1 text-sm text-fg outline-none ring-1 ring-accent"
                onBlur={(e) => {
                  setEditing(false);
                  const v = e.target.value.trim();
                  if (v !== (track.name ?? "")) update({ name: v }, "Rename track");
                }}
                onKeyDown={(e) => {
                  if (e.key === "Enter") (e.target as HTMLInputElement).blur();
                  if (e.key === "Escape") setEditing(false);
                  e.stopPropagation();
                }}
              />
            ) : (
              <div className={cn("truncate text-sm", clips || track.name ? "text-fg" : "text-faint")} onDoubleClick={() => setEditing(true)}>{name}</div>
            )}
          </div>
          <ToggleChip
            on={!track.mute} tone="accent" label={String(index + 1)} title={track.mute ? "Unmute track" : "Mute track"}
            onClick={() => update({ mute: !track.mute }, track.mute ? "Unmute track" : "Mute track")}
          />
          <ToggleChip
            on={!!track.solo} label="S" title={track.solo ? "Unsolo track" : "Solo: only soloed tracks play"}
            onClick={() => update({ solo: !track.solo }, track.solo ? "Unsolo track" : "Solo track")}
          />
          {touched !== undefined && Date.now() - touched < 2000 && (
            <span key={touched} className="pointer-events-none absolute inset-0 animate-agent-flash bg-agent/25" />
          )}
        </div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem onSelect={() => setEditing(true)}>Rename</ContextMenuItem>
        <ContextMenuSub label="Colour">
          <div className="grid grid-cols-6 gap-1 p-1">
            {palette.map((c) => (
              <button key={c} type="button" className="h-5 w-5 rounded-[3px]" style={{ background: c }}
                onClick={() => update({ color: c }, "Colour track")} />
            ))}
          </div>
        </ContextMenuSub>
        <ContextMenuSeparator />
        <ContextMenuItem onSelect={() => void execute({ type: "insertTrack", track: index }, { label: "Insert track" })}>Insert track above</ContextMenuItem>
        <ContextMenuItem onSelect={() => void execute({ type: "insertTrack", track: index + 1 }, { label: "Insert track" })}>Insert track below</ContextMenuItem>
        <ContextMenuSeparator />
        <ContextMenuItem danger onSelect={() => void execute({ type: "removeTrack", track: index }, { label: "Delete track" })}>
          Delete track{clips ? ` and ${clips} clip${clips === 1 ? "" : "s"}` : ""}
        </ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}
