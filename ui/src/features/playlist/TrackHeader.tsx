import type { PlaylistTrack, Project } from "@groovekit/schema/project";
import { ChevronDown } from "lucide-react";
import { useState } from "react";
import { Knob, ToggleChip } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger, Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { useLiveControl } from "@/lib/liveControl";
import { colorFor, formatDb, formatPan, insertName, palette, uniqueId } from "@/lib/model";
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
          className="sticky left-0 z-10 flex shrink-0 items-stretch gap-2 border-r border-b border-line bg-raised pr-1.5"
          style={{ width: HEADER_W, height }}
        >
          <div className="h-full w-1 shrink-0" style={{ background: color }} />
          <div className="flex min-w-0 flex-1 flex-col justify-center gap-0.5">
          <div className="flex items-center gap-1.5">
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
          <ToggleChip
            on={!!track.solo} label="S" title={track.solo ? "Unsolo track" : "Solo: only soloed tracks play"}
            onClick={() => void execute({ type: "updateTrack", track: track.id, changes: { solo: !track.solo } }, { label: track.solo ? "Unsolo track" : "Solo track" })}
          />
          </div>
          {height >= 40 && <TrackMixer track={track} doc={doc} />}
          </div>
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

/** Where the track's audio clips play: an insert, at a level and pan. */
function TrackMixer({ track, doc }: { track: PlaylistTrack; doc: Project }) {
  const volume = useLiveControl((v) => ({ type: "updateTrack", track: track.id, changes: { volumeDb: Math.round(v * 10) / 10 } }), "Track volume");
  const pan = useLiveControl((v) => ({ type: "updateTrack", track: track.id, changes: { pan: Math.round(v * 100) / 100 } }), "Pan track");
  const inserts = doc.mixer.inserts;
  const current = inserts.find((i) => i.id === (track.insert ?? "master"));
  const name = track.name ?? track.id;

  const routeToNew = () => {
    const id = uniqueId(track.id, inserts);
    void execute([
      { type: "addInsert", insert: { id, name } },
      { type: "updateTrack", track: track.id, changes: { insert: id } },
    ], { label: `Route ${name} to a new insert` });
  };

  return (
    <div className="flex items-center gap-1" onPointerDown={(e) => e.stopPropagation()}>
      <Dropdown>
        <DropdownTrigger asChild>
          <button type="button" title="Mixer insert for this track's audio clips (pattern clips play through their channels' inserts)"
            className="flex h-[18px] min-w-0 flex-1 items-center gap-0.5 rounded-[3px] bg-bg px-1 text-[10px] text-dim hover:text-fg">
            <span className="text-faint">→</span>
            <span className="min-w-0 flex-1 truncate text-left">{insertName(current, current ? inserts.indexOf(current) : undefined)}</span>
            <ChevronDown size={9} className="shrink-0" />
          </button>
        </DropdownTrigger>
        <DropdownContent>
          <DropdownLabel>Audio clips play into</DropdownLabel>
          {inserts.map((insert, i) => (
            <DropdownItem key={insert.id} checked={insert.id === (track.insert ?? "master")}
              onSelect={() => void execute({ type: "updateTrack", track: track.id, changes: { insert: insert.id } }, { label: `Route ${name} to ${insertName(insert, i)}` })}>
              {insertName(insert, i)}
            </DropdownItem>
          ))}
          <DropdownSeparator />
          <DropdownItem onSelect={routeToNew}>New insert</DropdownItem>
        </DropdownContent>
      </Dropdown>
      <Knob value={track.volumeDb ?? 0} min={-60} max={12} defaultValue={0} size={18} title="Volume of this track's audio clips"
        format={formatDb} onChange={volume.onChange} onCommit={volume.onCommit} />
      <Knob value={track.pan ?? 0} min={-1} max={1} defaultValue={0} size={18} bipolar title="Pan of this track's audio clips"
        format={formatPan} onChange={pan.onChange} onCommit={pan.onCommit} />
    </div>
  );
}
