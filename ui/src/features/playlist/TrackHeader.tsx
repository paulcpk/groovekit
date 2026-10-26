import type { PlaylistTrack, Project } from "@groovekit/schema/project";
import { useState } from "react";
import { ToggleChip } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger } from "@/components/menu";
import { colorFor, palette, uniqueId } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import { HEADER_W } from "./geometry";

export function TrackHeader({ track, index, doc, height }: { track: PlaylistTrack; index: number; doc: Project; height: number }) {
  const [editing, setEditing] = useState(false);
  const touched = useEngine((s) => s.touchedAt[track.id]);
  const color = colorFor(track.id, track.color);
  const name = track.name ?? `Track ${index + 1}`;
  const clips = doc.playlist.clips.filter((c) => c.track === track.id).length;

  const addTrack = (at: number) => {
    const id = uniqueId("track", doc.playlist.tracks);
    void execute({ type: "addTrack", track: { id, name: `Track ${doc.playlist.tracks.length + 1}` }, index: at }, { label: "Add track" });
  };

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        <div
          className="sticky left-0 z-10 flex shrink-0 items-center gap-2 border-r border-b border-line bg-raised pr-1.5"
          style={{ width: HEADER_W, height }}
        >
          <div className="h-full w-1 shrink-0" style={{ background: color }} />
          <div className="min-w-0 flex-1">
            {editing ? (
              <input
                autoFocus defaultValue={name}
                className="w-full rounded-[3px] bg-bg px-1 text-sm text-fg outline-none ring-1 ring-accent"
                onBlur={(e) => {
                  setEditing(false);
                  const v = e.target.value.trim();
                  if (v && v !== track.name) void execute({ type: "updateTrack", track: track.id, changes: { name: v } }, { label: "Rename track" });
                }}
                onKeyDown={(e) => {
                  if (e.key === "Enter") (e.target as HTMLInputElement).blur();
                  if (e.key === "Escape") setEditing(false);
                  e.stopPropagation();
                }}
              />
            ) : (
              <div className="truncate text-sm text-fg" onDoubleClick={() => setEditing(true)}>{name}</div>
            )}
          </div>
          <ToggleChip
            on={!track.mute} tone="accent" label={String(index + 1)} title={track.mute ? "Unmute track" : "Mute track"}
            onClick={() => void execute({ type: "updateTrack", track: track.id, changes: { mute: !track.mute } }, { label: track.mute ? "Unmute track" : "Mute track" })}
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
                onClick={() => void execute({ type: "updateTrack", track: track.id, changes: { color: c } }, { label: "Colour track" })} />
            ))}
          </div>
        </ContextMenuSub>
        <ContextMenuSeparator />
        <ContextMenuItem onSelect={() => addTrack(index)}>Insert track above</ContextMenuItem>
        <ContextMenuItem onSelect={() => addTrack(index + 1)}>Insert track below</ContextMenuItem>
        <ContextMenuSeparator />
        <ContextMenuItem danger onSelect={() => void execute({ type: "removeTrack", track: track.id }, { label: "Delete track" })}>
          Delete track{clips ? ` and ${clips} clip${clips === 1 ? "" : "s"}` : ""}
        </ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}
